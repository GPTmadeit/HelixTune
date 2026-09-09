#include "AutoModePanel.h"

namespace helix::ui
{

static constexpr int sectionHeader = 20;
static constexpr int gap = 8;

AutoModePanel::AutoModePanel (HelixTuneProcessor& p)
    : processor (p), scope (p), keyboard (p)
{
    addAndMakeVisible (scope);
    addAndMakeVisible (keyboard);

    auto& state = processor.apvts;

    keySelector  .attach (state, params::key);
    scaleSelector.attach (state, params::scale);
    inputSelector.attach (state, params::inputType);
    shapeSelector.attach (state, params::vibShape);

    retuneKnob   .attach (state, params::retuneSpeed);
    flexKnob     .attach (state, params::flexTune);
    humanizeKnob .attach (state, params::humanize);
    natVibKnob   .attach (state, params::naturalVibrato);
    transposeKnob.attach (state, params::transpose);
    detuneKnob   .attach (state, params::detune);

    trackingKnob .attach (state, params::tracking);
    stabilityKnob.attach (state, params::pitchSmooth);
    sibilanceKnob.attach (state, params::sibilance);
    throatKnob   .attach (state, params::throatLength);

    vibRateKnob   .attach (state, params::vibRate);
    vibVarKnob    .attach (state, params::vibVariation);
    vibDelayKnob  .attach (state, params::vibOnsetDelay);
    vibOnsetKnob  .attach (state, params::vibOnsetRate);
    vibPitchKnob  .attach (state, params::vibPitchAmount);
    vibAmpKnob    .attach (state, params::vibAmpAmount);
    vibFormantKnob.attach (state, params::vibFormantAmount);

    ignoreVibToggle.attach (state, params::targetIgnoresVib);
    classicToggle  .attach (state, params::classicMode);
    formantToggle  .attach (state, params::formantCorrect);
    midiOutToggle  .attach (state, params::midiOut);

    for (auto* c : std::initializer_list<juce::Component*> {
             &keySelector, &scaleSelector, &inputSelector, &shapeSelector,
             &retuneKnob, &flexKnob, &humanizeKnob, &natVibKnob, &transposeKnob, &detuneKnob,
             &trackingKnob, &stabilityKnob, &sibilanceKnob, &throatKnob,
             &vibRateKnob, &vibVarKnob, &vibDelayKnob, &vibOnsetKnob,
             &vibPitchKnob, &vibAmpKnob, &vibFormantKnob,
             &ignoreVibToggle, &classicToggle, &formantToggle, &midiOutToggle })
    {
        addAndMakeVisible (c);
    }

    // Deliberately not hooking ComboBox::onChange here - the parameter
    // attachments own those callbacks in some JUCE versions, and stealing one
    // silently breaks the parameter. Both dependent views already repaint on
    // their own timers, so a key or scale change shows up either way.
    keyboard.onStateChanged = [this] { repaint(); };
}

void AutoModePanel::pushFrames (const std::vector<PitchFrame>& frames)
{
    scope.pushFrames (frames);

    if (! frames.empty())
        keyboard.setLiveNote (frames.back().detectedMidi, frames.back().voiced);
}

void AutoModePanel::resized()
{
    sections.clear();
    auto area = getLocalBounds().reduced (gap);

    // --- row 1: scale + scope --------------------------------------------
    auto row1 = area.removeFromTop (208);
    auto scaleArea = row1.removeFromLeft (410);
    row1.removeFromLeft (gap);

    sections.push_back ({ scaleArea, "KEY & SCALE", colours::cyan });
    sections.push_back ({ row1, "PITCH", colours::cyan });

    {
        auto inner = scaleArea.reduced (10);
        inner.removeFromTop (sectionHeader);

        auto selectors = inner.removeFromTop (42);
        keySelector.setBounds (selectors.removeFromLeft (110));
        selectors.removeFromLeft (gap);
        scaleSelector.setBounds (selectors);

        inner.removeFromTop (gap);
        keyboard.setBounds (inner);
    }

    scope.setBounds (row1.reduced (2));

    area.removeFromTop (gap);

    // --- row 2: correction ------------------------------------------------
    auto row2 = area.removeFromTop (168);
    sections.push_back ({ row2, "CORRECTION", colours::magenta });

    {
        auto inner = row2.reduced (10);
        inner.removeFromTop (sectionHeader);

        auto toggles = inner.removeFromBottom (24);
        ignoreVibToggle.setBounds (toggles.removeFromLeft (230));
        toggles.removeFromLeft (gap * 2);
        classicToggle.setBounds (toggles.removeFromLeft (150));

        inner.removeFromBottom (4);

        NeonKnob* knobs[] = { &retuneKnob, &flexKnob, &humanizeKnob,
                              &natVibKnob, &transposeKnob, &detuneKnob };
        const int n = (int) (sizeof (knobs) / sizeof (knobs[0]));
        const int w = inner.getWidth() / n;

        for (int i = 0; i < n; ++i)
            knobs[i]->setBounds (inner.removeFromLeft (i == n - 1 ? inner.getWidth() : w).reduced (4, 0));
    }

    area.removeFromTop (gap);

    // --- row 3: detection / formant / vibrato -----------------------------
    auto row3 = area;
    auto detectArea = row3.removeFromLeft (312);
    row3.removeFromLeft (gap);
    auto formantArea = row3.removeFromLeft (190);
    row3.removeFromLeft (gap);

    sections.push_back ({ detectArea, "DETECTION", colours::violet });
    sections.push_back ({ formantArea, "FORMANT", colours::lime });
    sections.push_back ({ row3, "VIBRATO", colours::amber });

    {
        auto inner = detectArea.reduced (10);
        inner.removeFromTop (sectionHeader);

        auto top = inner.removeFromTop (42);
        inputSelector.setBounds (top.removeFromLeft (168));
        top.removeFromLeft (gap);
        midiOutToggle.setBounds (top.withSizeKeepingCentre (top.getWidth(), 24));

        inner.removeFromTop (gap);

        NeonKnob* knobs[] = { &trackingKnob, &stabilityKnob, &sibilanceKnob };
        const int w = inner.getWidth() / 3;
        for (int i = 0; i < 3; ++i)
            knobs[i]->setBounds (inner.removeFromLeft (w).reduced (3, 0));
    }

    {
        auto inner = formantArea.reduced (10);
        inner.removeFromTop (sectionHeader);
        formantToggle.setBounds (inner.removeFromTop (24));
        inner.removeFromTop (gap);
        throatKnob.setBounds (inner.removeFromLeft (86));
    }

    {
        auto inner = row3.reduced (10);
        inner.removeFromTop (sectionHeader);

        auto top = inner.removeFromTop (42);
        shapeSelector.setBounds (top.removeFromLeft (140));

        inner.removeFromTop (2);

        NeonKnob* knobs[] = { &vibRateKnob, &vibVarKnob, &vibDelayKnob, &vibOnsetKnob,
                              &vibPitchKnob, &vibAmpKnob, &vibFormantKnob };
        const int n = (int) (sizeof (knobs) / sizeof (knobs[0]));
        const int w = inner.getWidth() / n;

        for (int i = 0; i < n; ++i)
            knobs[i]->setBounds (inner.removeFromLeft (i == n - 1 ? inner.getWidth() : w).reduced (3, 0));
    }
}

void AutoModePanel::paint (juce::Graphics& g)
{
    for (const auto& s : sections)
    {
        drawGlassPanel (g, s.bounds.toFloat(), s.accent, 7.0f);

        auto header = s.bounds.reduced (10, 0).withHeight (sectionHeader).translated (0, 8);

        g.setColour (s.accent.withAlpha (0.85f));
        g.setFont (FuturisticLookAndFeel::uiFont (10.0f, true));
        g.drawText (s.title, header, juce::Justification::centredLeft, false);

        // Hairline running from the title to the panel edge - a cheap way to
        // make a group read as one object without boxing it in.
        const float textW = juce::GlyphArrangement::getStringWidth (
                                FuturisticLookAndFeel::uiFont (10.0f, true), s.title) + 8.0f;
        g.setColour (s.accent.withAlpha (0.18f));
        g.fillRect (juce::Rectangle<float> ((float) header.getX() + textW,
                                            (float) header.getCentreY(),
                                            (float) header.getWidth() - textW, 1.0f));
    }
}

} // namespace helix::ui
