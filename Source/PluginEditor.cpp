#include "PluginEditor.h"

namespace helix
{

using namespace ui;

static constexpr int headerHeight = 52;

HelixTuneEditor::HelixTuneEditor (HelixTuneProcessor& p)
    : AudioProcessorEditor (&p), processor (p), autoPanel (p), graphPanel (p)
{
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (autoPanel);
    addAndMakeVisible (graphPanel);

    for (auto* b : { &autoModeButton, &graphModeButton })
    {
        b->setColour (juce::TextButton::buttonOnColourId, colours::cyan);
        b->setClickingTogglesState (false);
        addAndMakeVisible (b);
    }

    autoModeButton.onClick = [this]
    {
        processor.apvts.getParameter (params::graphMode)->setValueNotifyingHost (0.0f);
        updateMode();
    };

    graphModeButton.onClick = [this]
    {
        processor.apvts.getParameter (params::graphMode)->setValueNotifyingHost (1.0f);
        updateMode();
    };

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

void HelixTuneEditor::updateMode()
{
    const bool graph = *processor.apvts.getRawParameterValue (params::graphMode) > 0.5f;

    autoPanel.setVisible (! graph);
    graphPanel.setVisible (graph);

    autoModeButton.setToggleState (! graph, juce::dontSendNotification);
    graphModeButton.setToggleState (graph, juce::dontSendNotification);

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
        graphPanel.pushFrames (scratch);
    }

    bannerPhase += 0.012f;
    if (bannerPhase > 1.0f)
        bannerPhase -= 1.0f;

    // Keep the mode buttons honest if the host automates the parameter.
    const bool graph = *processor.apvts.getRawParameterValue (params::graphMode) > 0.5f;
    if (graph != graphPanel.isVisible())
        updateMode();

    repaint (headerBounds);
}

void HelixTuneEditor::resized()
{
    auto area = getLocalBounds();
    headerBounds = area.removeFromTop (headerHeight);

    autoPanel.setBounds (area);
    graphPanel.setBounds (area);

    auto bar = headerBounds.reduced (14, 10);
    bar.removeFromLeft (168);   // wordmark

    autoModeButton.setBounds (bar.removeFromLeft (74));
    bar.removeFromLeft (4);
    graphModeButton.setBounds (bar.removeFromLeft (74));

    bypassButton.setBounds (bar.removeFromRight (86));
    bar.removeFromRight (14);

    // The captions are painted above each slider, so leave room for them
    // rather than letting them clip against the top of the header.
    outputSlider.setBounds (bar.removeFromRight (110).withTrimmedTop (11));
    bar.removeFromRight (36);
    mixSlider.setBounds (bar.removeFromRight (110).withTrimmedTop (11));

    // Whatever is left between the mode buttons and the mix slider. Drawing
    // the latency readout right-aligned in the header instead put it straight
    // on top of the output slider.
    latencyBounds = bar.reduced (24, 0);
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
    auto mark = headerBounds.reduced (14, 0).withWidth (168);

    g.setColour (colours::cyan);
    g.setFont (FuturisticLookAndFeel::uiFont (23.0f, true));
    g.drawText ("HELIX", mark.withWidth (72), juce::Justification::centredLeft, false);

    g.setColour (colours::text);
    g.setFont (FuturisticLookAndFeel::uiFont (23.0f));
    g.drawText ("TUNE", mark.withTrimmedLeft (70).withWidth (64), juce::Justification::centredLeft, false);

    g.setColour (colours::textFaint);
    g.setFont (FuturisticLookAndFeel::monoFont (8.5f));
    g.drawText (juce::String ("v") + HELIX_VERSION,
                mark.withTrimmedLeft (134), juce::Justification::centredLeft, false);

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

} // namespace helix
