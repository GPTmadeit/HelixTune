#include "PluginEditor.h"

namespace helix
{

using namespace ui;

static constexpr int headerHeight = 52;

HelixTuneEditor::HelixTuneEditor (HelixTuneProcessor& p)
    : AudioProcessorEditor (&p), processor (p), autoPanel (p), harmonyPanel (p), graphPanel (p),
      presets (p.apvts)
{
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (autoPanel);
    addAndMakeVisible (harmonyPanel);
    addAndMakeVisible (graphPanel);

    for (auto* b : { &autoModeButton, &harmonyModeButton, &graphModeButton })
    {
        b->setColour (juce::TextButton::buttonOnColourId, colours::cyan);
        b->setClickingTogglesState (false);
        addAndMakeVisible (b);
    }

    autoModeButton.onClick    = [this] { setView (0); };
    harmonyModeButton.onClick = [this] { setView (1); };
    graphModeButton.onClick   = [this] { setView (2); };

    bypassButton.setColour (juce::TextButton::buttonOnColourId, colours::danger);
    addAndMakeVisible (bypassButton);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, params::bypass, bypassButton);

    mixSlider.setColour (juce::Slider::thumbColourId, colours::cyan);
    outputSlider.setColour (juce::Slider::thumbColourId, colours::violet);
    addAndMakeVisible (mixSlider);
    addAndMakeVisible (outputSlider);

    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, params::mix, mixSlider);
    outputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, params::outputGain, outputSlider);

    presetBox.setTextWhenNothingSelected ("PRESET");
    presetBox.onChange = [this]
    {
        const int index = presetBox.getSelectedItemIndex();
        if (index >= 0)
            presets.load (index);
    };
    addAndMakeVisible (presetBox);

    savePresetButton.setColour (juce::TextButton::buttonOnColourId, colours::lime);
    savePresetButton.onClick = [this] { promptSavePreset(); };
    addAndMakeVisible (savePresetButton);

    refreshPresetList();

    // The version readout doubles as the update control: one affordance, and
    // it is always there rather than appearing only when there is news.
    versionButton.setColour (juce::TextButton::buttonOnColourId, colours::lime);
    versionButton.setButtonText ("v" + UpdateChecker::getCurrentVersion());
    versionButton.onClick = [this] { showUpdateMenu(); };
    addAndMakeVisible (versionButton);

    updater.checkInBackground();

    setResizable (true, true);
    setResizeLimits (1000, 620, 2400, 1500);
    setSize (1140, 680);

    updateMode();
    startTimerHz (60);
}

HelixTuneEditor::~HelixTuneEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void HelixTuneEditor::refreshPresetList (const juce::String& select)
{
    presetBox.clear (juce::dontSendNotification);

    const auto names = presets.getAllNames();
    const int numFactory = presets.getNumFactory();

    for (int i = 0; i < names.size(); ++i)
    {
        if (i == numFactory)
            presetBox.addSeparator();

        presetBox.addItem (names[i], i + 1);
    }

    if (select.isNotEmpty())
    {
        const int index = names.indexOf (select);
        if (index >= 0)
            presetBox.setSelectedItemIndex (index, juce::dontSendNotification);
    }
}

void HelixTuneEditor::refreshUpdateButton()
{
    const auto st = updater.getStatus();

    if (updater.isDownloading())
    {
        const float p = updater.getDownloadProgress();
        versionButton.setButtonText (p < 0.0f ? "FAILED"
                                              : "DOWNLOADING " + juce::String ((int) (p * 100.0f)) + "%");
        versionButton.setToggleState (true, juce::dontSendNotification);
        return;
    }

    if (st.updateAvailable)
    {
        versionButton.setButtonText (st.latestVersion + " AVAILABLE");
        versionButton.setToggleState (true, juce::dontSendNotification);
    }
    else
    {
        versionButton.setButtonText ("v" + UpdateChecker::getCurrentVersion());
        versionButton.setToggleState (false, juce::dontSendNotification);
    }
}

void HelixTuneEditor::showUpdateMenu()
{
    const auto st = updater.getStatus();

    juce::PopupMenu menu;
    menu.addSectionHeader ("HELIX Tune v" + UpdateChecker::getCurrentVersion());

    if (st.updateAvailable)
    {
        menu.addItem (1, "Download and install " + st.latestVersion,
                      st.installerUrl.isNotEmpty() && ! updater.isDownloading());
        menu.addItem (2, "Open release page");
        menu.addSeparator();
    }
    else if (st.checked)
    {
        menu.addItem (99, "Up to date", false);
        menu.addSeparator();
    }

    menu.addItem (3, "Check for updates now", ! updater.isDownloading());
    menu.addItem (4, "Check automatically", true, updater.areChecksEnabled());

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (versionButton),
        [this] (int result)
        {
            const auto s = updater.getStatus();

            switch (result)
            {
                case 1: updater.downloadAndLaunchInstaller(); break;
                case 2: juce::URL (s.releaseUrl.isNotEmpty() ? s.releaseUrl
                                                             : UpdateChecker::getReleasesPageUrl())
                            .launchInDefaultBrowser();
                        break;
                case 3: updater.checkInBackground (true); break;
                case 4: updater.setChecksEnabled (! updater.areChecksEnabled()); break;
                default: break;
            }
        });
}

void HelixTuneEditor::promptSavePreset()
{
    auto* window = new juce::AlertWindow ("Save Preset",
                                          "Name this preset:",
                                          juce::MessageBoxIconType::NoIcon);

    window->addTextEditor ("name", "My Preset");
    window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<HelixTuneEditor> safeThis (this);

    window->enterModalState (true, juce::ModalCallbackFunction::create (
        [safeThis, window] (int result)
        {
            // Read the field before the window tears itself down.
            const auto name = window->getTextEditorContents ("name");

            if (result == 1 && safeThis != nullptr && name.trim().isNotEmpty())
                if (safeThis->presets.saveUser (name))
                    safeThis->refreshPresetList (juce::File::createLegalFileName (name.trim()));
        }), true);
}

void HelixTuneEditor::setView (int view)
{
    currentView = juce::jlimit (0, 2, view);

    if (auto* p = processor.apvts.getParameter (params::graphMode))
        p->setValueNotifyingHost (currentView == 2 ? 1.0f : 0.0f);

    updateMode();
}

void HelixTuneEditor::updateMode()
{
    const bool graph = *processor.apvts.getRawParameterValue (params::graphMode) > 0.5f;

    // The host owns graph mode, so it wins if it was automated behind our back.
    if (graph)
        currentView = 2;
    else if (currentView == 2)
        currentView = 0;

    autoPanel.setVisible (currentView == 0);
    harmonyPanel.setVisible (currentView == 1);
    graphPanel.setVisible (currentView == 2);

    autoModeButton.setToggleState (currentView == 0, juce::dontSendNotification);
    harmonyModeButton.setToggleState (currentView == 1, juce::dontSendNotification);
    graphModeButton.setToggleState (currentView == 2, juce::dontSendNotification);

    repaint();
}

void HelixTuneEditor::timerCallback()
{
    scratch.clear();
    processor.getPitchFifo().drain (scratch);

    if (! scratch.empty())
    {
        // Both panels always get the data: the graph editor has to keep
        // capturing the contour while auto mode is on screen, otherwise
        // switching to graph mode would show an empty timeline.
        autoPanel.pushFrames (scratch);
        harmonyPanel.pushFrames (scratch);
        graphPanel.pushFrames (scratch);
    }

    bannerPhase += 0.012f;
    if (bannerPhase > 1.0f)
        bannerPhase -= 1.0f;

    if (++updatePollCounter >= 15)
    {
        updatePollCounter = 0;
        refreshUpdateButton();
    }

    // Keep the mode buttons honest if the host automates the parameter.
    const bool graph = *processor.apvts.getRawParameterValue (params::graphMode) > 0.5f;
    if (graph != graphPanel.isVisible())
        updateMode();

    harmonyPanel.refreshMasterState();

    // Only the sweep along the header's bottom edge moves. Repainting the whole
    // header here redrew every control in it on every tick.
    repaint (headerBounds.withTop (headerBounds.getBottom() - 2));

    // The latency readout changes only when the input type does.
    const int latencyNow = processor.getLatencySamples();
    if (latencyNow != shownLatency)
    {
        shownLatency = latencyNow;
        repaint (latencyBounds);
    }
}

void HelixTuneEditor::resized()
{
    auto area = getLocalBounds();
    headerBounds = area.removeFromTop (headerHeight);

    autoPanel.setBounds (area);
    harmonyPanel.setBounds (area);
    graphPanel.setBounds (area);

    auto bar = headerBounds.reduced (14, 10);
    bar.removeFromLeft (168);   // wordmark

    autoModeButton.setBounds (bar.removeFromLeft (68));
    bar.removeFromLeft (4);
    harmonyModeButton.setBounds (bar.removeFromLeft (100));   // room for the status lamp
    bar.removeFromLeft (4);
    graphModeButton.setBounds (bar.removeFromLeft (68));

    bypassButton.setBounds (bar.removeFromRight (86));
    bar.removeFromRight (14);

    // The captions are painted above each slider, so leave room for them
    // rather than letting them clip against the top of the header.
    outputSlider.setBounds (bar.removeFromRight (110).withTrimmedTop (11));
    bar.removeFromRight (36);
    mixSlider.setBounds (bar.removeFromRight (110).withTrimmedTop (11));

    // Presets take the middle of the bar; the latency readout tucks under the
    // wordmark, which is the only space left that nothing else wants.
    bar.removeFromLeft (18);
    presetBox.setBounds (bar.removeFromLeft (juce::jmin (180, juce::jmax (0, bar.getWidth() - 210))));
    bar.removeFromLeft (6);
    savePresetButton.setBounds (bar.removeFromLeft (54));

    bar.removeFromLeft (10);
    versionButton.setBounds (bar.removeFromLeft (juce::jmin (132, juce::jmax (0, bar.getWidth()))));

    latencyBounds = headerBounds.reduced (14, 0).withWidth (168)
                        .removeFromBottom (13).translated (0, -5);
}

void HelixTuneEditor::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();

    // Base gradient plus a faint grid: enough texture that the panels have
    // something to sit on, quiet enough that nothing competes with the traces.
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0a1018), area.getCentreX(), 0.0f,
                                             colours::bg, area.getCentreX(), area.getBottom(), false));
    g.fillAll();

    g.setColour (colours::grid.withAlpha (0.30f));
    for (float x = 0.0f; x < area.getWidth(); x += 32.0f)
        g.fillRect (x, 0.0f, 1.0f, area.getHeight());
    for (float y = 0.0f; y < area.getHeight(); y += 32.0f)
        g.fillRect (0.0f, y, area.getWidth(), 1.0f);

    // --- header -----------------------------------------------------------
    const auto header = headerBounds.toFloat();

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff101a2a), header.getCentreX(), header.getY(),
                                             juce::Colour (0xff070c14), header.getCentreX(), header.getBottom(), false));
    g.fillRect (header);

    // Animated hairline under the header - the only moving decoration, so it
    // reads as "running" without pulling the eye off the pitch display.
    const float sweepW = header.getWidth() * 0.28f;
    const float sweepX = -sweepW + bannerPhase * (header.getWidth() + sweepW);

    g.setColour (colours::outline);
    g.fillRect (header.getX(), header.getBottom() - 1.0f, header.getWidth(), 1.0f);

    g.setGradientFill (juce::ColourGradient (colours::cyan.withAlpha (0.0f), sweepX, 0.0f,
                                             colours::cyan.withAlpha (0.55f), sweepX + sweepW * 0.5f, 0.0f, false));
    g.fillRect (sweepX, header.getBottom() - 1.5f, sweepW * 0.5f, 1.5f);
    g.setGradientFill (juce::ColourGradient (colours::cyan.withAlpha (0.55f), sweepX + sweepW * 0.5f, 0.0f,
                                             colours::cyan.withAlpha (0.0f), sweepX + sweepW, 0.0f, false));
    g.fillRect (sweepX + sweepW * 0.5f, header.getBottom() - 1.5f, sweepW * 0.5f, 1.5f);

    // wordmark
    auto mark = headerBounds.reduced (14, 0).withWidth (168).translated (0, -6);

    g.setColour (colours::cyan);
    g.setFont (FuturisticLookAndFeel::uiFont (23.0f, true));
    g.drawText ("HELIX", mark.withWidth (72), juce::Justification::centredLeft, false);

    g.setColour (colours::text);
    g.setFont (FuturisticLookAndFeel::uiFont (23.0f));
    g.drawText ("TUNE", mark.withTrimmedLeft (70).withWidth (64), juce::Justification::centredLeft, false);

    // slider captions and the latency readout
    g.setColour (colours::textFaint);
    g.setFont (FuturisticLookAndFeel::uiFont (8.5f, true));
    g.drawText ("MIX", mixSlider.getBounds().translated (0, -12).withHeight (12),
                juce::Justification::centredLeft, false);
    g.drawText ("OUTPUT", outputSlider.getBounds().translated (0, -12).withHeight (12),
                juce::Justification::centredLeft, false);

    const double sr = processor.getCurrentSampleRate();
    const double ms = sr > 0.0 ? (double) processor.getLatencySamples() * 1000.0 / sr : 0.0;

    g.setColour (colours::textFaint);
    g.setFont (FuturisticLookAndFeel::monoFont (9.0f));
    g.drawText ("LATENCY " + juce::String (ms, 1) + " ms",
                latencyBounds, juce::Justification::centredLeft, false);
}

void HelixTuneEditor::paintOverChildren (juce::Graphics& g)
{
    // A status lamp on the HARMONY tab, so whether extra voices are being
    // added is visible from every view, not only from the harmony page.
    const bool on = *processor.apvts.getRawParameterValue (params::harmOn) > 0.5f;
    const auto b = harmonyModeButton.getBounds().toFloat();
    const auto lamp = juce::Rectangle<float> (6.0f, 6.0f)
                          .withCentre ({ b.getRight() - 8.0f, b.getCentreY() });

    if (on)
    {
        g.setColour (colours::violet.withAlpha (0.35f));
        g.fillEllipse (lamp.expanded (2.5f));
        g.setColour (colours::violet.brighter (0.5f));
        g.fillEllipse (lamp);
    }
    else
    {
        g.setColour (colours::textFaint.withAlpha (0.7f));
        g.drawEllipse (lamp.reduced (0.5f), 1.0f);
    }
}

} // namespace helix
