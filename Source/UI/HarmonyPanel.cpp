#include "HarmonyPanel.h"
#include "../DSP/ScaleQuantizer.h"

namespace helix::ui
{

static constexpr int sectionHeader = 20;
static constexpr int gap = 8;

static const char* kNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

static const juce::Colour kVoiceColours[4] = { colours::cyan, colours::lime,
                                               colours::amber, colours::magenta };

HarmonyPanel::HarmonyPanel (HelixTuneProcessor& p)
    : processor (p), autoKey (p)
{
    addAndMakeVisible (autoKey);

    auto& state = processor.apvts;

    masterToggle.attach (state, params::harmOn);
    addAndMakeVisible (masterToggle);

    levelKnob.attach (state, params::harmLevel);
    spreadKnob.attach (state, params::harmSpread);
    addAndMakeVisible (levelKnob);
    addAndMakeVisible (spreadKnob);

    for (int v = 0; v < params::numHarmonyVoices; ++v)
    {
        const auto c = kVoiceColours[v];

        auto* toggle = voiceEnable.add (new NeonToggle ("Voice " + juce::String (v + 1), c));
        toggle->attach (state, params::harmonyID (params::harmEnable, v));
        addAndMakeVisible (toggle);

        auto add = [&] (juce::OwnedArray<NeonKnob>& list, const char* caption, const char* base)
        {
            auto* k = list.add (new NeonKnob (caption, c));
            k->attach (state, params::harmonyID (base, v));
            addAndMakeVisible (k);
        };

        add (voiceInterval, "Interval", params::harmDegrees);
        add (voiceLevel,    "Level",    params::harmVoiceLevel);
        add (voicePan,      "Pan",      params::harmPan);
        add (voiceFormant,  "Formant",  params::harmFormant);
        add (voiceDetune,   "Detune",   params::harmDetune);
    }

    refreshMasterState();
}

void HarmonyPanel::refreshMasterState()
{
    const int on = *processor.apvts.getRawParameterValue (params::harmOn) > 0.5f ? 1 : 0;

    if (on == masterState)
        return;

    masterState = on;
    masterToggle.getButton().setButtonText (on ? "Harmony On" : "Harmony Off");

    // Dimmed rather than disabled: the voices stay editable while the bus is
    // off, so a part can be set up before it is switched in.
    const float alpha = on ? 1.0f : 0.38f;

    levelKnob.setAlpha (alpha);
    spreadKnob.setAlpha (alpha);

    for (auto* t : voiceEnable)
        t->setAlpha (alpha);

    for (auto* list : { &voiceInterval, &voiceLevel, &voicePan, &voiceFormant, &voiceDetune })
        for (auto* k : *list)
            k->setAlpha (alpha);

    repaint();
}

void HarmonyPanel::pushFrames (const std::vector<PitchFrame>& frames)
{
    if (frames.empty())
        return;

    const auto& f = frames.back();
    liveVoiced = f.voiced;

    if (f.voiced)
        liveMidi = f.outputMidi;

    // Only the live readouts move, so repaint those rather than the whole
    // panel sixty times a second.
    repaint (chordBounds);

    for (const auto& r : voiceReadouts)
        repaint (r);
}

void HarmonyPanel::resized()
{
    sections.clear();
    voiceRows.clearQuick();
    voiceReadouts.clearQuick();

    auto area = getLocalBounds().reduced (gap);

    // --- row 1: auto-key + harmony bus ------------------------------------
    auto row1 = area.removeFromTop (150);
    auto keyArea = row1.removeFromLeft (400);
    row1.removeFromLeft (gap);

    sections.push_back ({ keyArea, "AUTO-KEY", colours::violet });
    sections.push_back ({ row1, "HARMONY BUS", colours::violet });

    autoKey.setBounds (keyArea);

    {
        auto inner = row1.reduced (10);
        inner.removeFromTop (sectionHeader);

        // The master switch leads the row: it is the one control that decides
        // whether anything else on this page is heard at all.
        masterToggle.setBounds (inner.removeFromLeft (140).withSizeKeepingCentre (140, 28));
        inner.removeFromLeft (gap * 2);

        levelKnob.setBounds (inner.removeFromLeft (86));
        inner.removeFromLeft (gap);
        spreadKnob.setBounds (inner.removeFromLeft (86));
        inner.removeFromLeft (gap * 2);

        chordBounds = inner;
    }

    area.removeFromTop (gap);

    // --- voices -----------------------------------------------------------
    voicesArea = area;
    sections.push_back ({ area, "VOICES", colours::cyan });

    auto inner = area.reduced (10);
    inner.removeFromTop (sectionHeader);

    const int rowHeight = juce::jmax (72, inner.getHeight() / params::numHarmonyVoices);

    for (int v = 0; v < params::numHarmonyVoices; ++v)
    {
        auto row = inner.removeFromTop (rowHeight);
        voiceRows.add (row);

        auto cells = row.reduced (0, 2);
        voiceEnable[v]->setBounds (cells.removeFromLeft (108).withSizeKeepingCentre (108, 24));
        cells.removeFromLeft (gap);

        NeonKnob* knobs[] = { voiceInterval[v], voiceLevel[v], voicePan[v],
                              voiceFormant[v], voiceDetune[v] };

        // Leave a column on the right for the live target readout.
        auto readout = cells.removeFromRight (juce::jmin (150, cells.getWidth() / 4));
        voiceReadouts.add (readout);

        const int n = (int) (sizeof (knobs) / sizeof (knobs[0]));
        const int w = juce::jmin (118, cells.getWidth() / n);

        for (int i = 0; i < n; ++i)
            knobs[i]->setBounds (cells.removeFromLeft (w).reduced (3, 0));
    }
}

void HarmonyPanel::paintChordReadout (juce::Graphics& g, juce::Rectangle<int> area)
{
    if (area.getWidth() < 60)
        return;

    g.setColour (colours::textFaint);
    g.setFont (FuturisticLookAndFeel::uiFont (9.0f, true));
    g.drawText ("LIVE CHORD", area.removeFromTop (12), juce::Justification::centredLeft, false);

    if (! isMasterOn())
    {
        g.setColour (colours::textDim);
        g.setFont (FuturisticLookAndFeel::uiFont (13.0f));
        g.drawText ("Harmony is off - only the lead is heard", area,
                    juce::Justification::centredLeft, true);
        return;
    }

    if (! liveVoiced || liveMidi <= 0.0f)
    {
        g.setColour (colours::textFaint);
        g.setFont (FuturisticLookAndFeel::uiFont (13.0f));
        g.drawText ("--", area, juce::Justification::centredLeft, false);
        return;
    }

    // Rebuild the same scale the engine is using, so the readout agrees with
    // what is actually being sung.
    ScaleQuantizer q;
    q.setKey ((int) *processor.apvts.getRawParameterValue (params::key));
    q.setScale ((int) *processor.apvts.getRawParameterValue (params::scale));
    for (int pc = 0; pc < 12; ++pc)
        q.setNoteState (pc, processor.getNoteState (pc));

    auto noteName = [] (float midi)
    {
        const int n = juce::jlimit (0, 127, (int) std::lround (midi));
        return juce::String (kNoteNames[((n % 12) + 12) % 12]) + juce::String (n / 12 - 1);
    };

    auto cell = area.withHeight (juce::jmin (26, area.getHeight()));
    const int chipW = juce::jmin (58, area.getWidth() / (params::numHarmonyVoices + 1));

    // Lead first, then each enabled voice in its own colour.
    {
        auto c = cell.removeFromLeft (chipW);
        g.setColour (colours::text.withAlpha (0.14f));
        g.fillRoundedRectangle (c.toFloat().reduced (2.0f), 3.0f);
        g.setColour (colours::text);
        g.setFont (FuturisticLookAndFeel::monoFont (12.0f, true));
        g.drawText (noteName (liveMidi), c, juce::Justification::centred, false);
    }

    for (int v = 0; v < params::numHarmonyVoices; ++v)
    {
        const bool on = *processor.apvts.getRawParameterValue (
                            params::harmonyID (params::harmEnable, v)) > 0.5f;

        if (! on)
            continue;

        const int degrees = (int) *processor.apvts.getRawParameterValue (
                                params::harmonyID (params::harmDegrees, v));

        const float note = q.transposeByScaleDegrees (liveMidi, degrees);

        auto c = cell.removeFromLeft (chipW);
        if (c.getWidth() < 10)
            break;

        g.setColour (kVoiceColours[v].withAlpha (0.20f));
        g.fillRoundedRectangle (c.toFloat().reduced (2.0f), 3.0f);
        g.setColour (kVoiceColours[v]);
        g.setFont (FuturisticLookAndFeel::monoFont (12.0f, true));
        g.drawText (noteName (note), c, juce::Justification::centred, false);
    }
}

void HarmonyPanel::paint (juce::Graphics& g)
{
    const auto headerFont = FuturisticLookAndFeel::uiFont (10.0f, true);

    for (const auto& s : sections)
    {
        drawGlassPanel (g, s.bounds.toFloat(), s.accent, 7.0f);

        auto header = s.bounds.reduced (10, 0).withHeight (sectionHeader).translated (0, 8);

        g.setColour (s.accent.withAlpha (0.85f));
        g.setFont (headerFont);
        g.drawText (s.title, header, juce::Justification::centredLeft, false);

        float lineEnd = (float) header.getRight();

        // With the bus off, the voices header says so in words - dimmed
        // controls alone read as "disabled", not as "switched off here".
        if (s.bounds == voicesArea && ! isMasterOn())
        {
            const juce::String tag ("HARMONY OFF - NOT RENDERED");
            const float tagW = juce::GlyphArrangement::getStringWidth (headerFont, tag);

            g.setColour (colours::textDim);
            g.drawText (tag, header, juce::Justification::centredRight, false);
            lineEnd -= tagW + 10.0f;
        }

        const float textW = juce::GlyphArrangement::getStringWidth (headerFont, s.title) + 8.0f;
        const float lineStart = (float) header.getX() + textW;

        g.setColour (s.accent.withAlpha (0.18f));
        if (lineEnd > lineStart)
            g.fillRect (juce::Rectangle<float> (lineStart, (float) header.getCentreY(),
                                                lineEnd - lineStart, 1.0f));
    }

    // A tint down the left edge of each voice row ties the strip to its colour
    // in the chord readout.
    for (int v = 0; v < voiceRows.size(); ++v)
    {
        auto r = voiceRows.getReference (v).toFloat();
        const bool on = isMasterOn()
                     && *processor.apvts.getRawParameterValue (
                            params::harmonyID (params::harmEnable, v)) > 0.5f;

        g.setColour (kVoiceColours[v].withAlpha (on ? 0.55f : 0.15f));
        g.fillRoundedRectangle (r.withWidth (3.0f).reduced (0.0f, 6.0f), 1.5f);
    }

    paintChordReadout (g, chordBounds);

    // --- per-voice live target --------------------------------------------
    ScaleQuantizer q;
    q.setKey ((int) *processor.apvts.getRawParameterValue (params::key));
    q.setScale ((int) *processor.apvts.getRawParameterValue (params::scale));
    for (int pc = 0; pc < 12; ++pc)
        q.setNoteState (pc, processor.getNoteState (pc));

    for (int v = 0; v < voiceReadouts.size(); ++v)
    {
        auto r = voiceReadouts.getReference (v);
        if (r.getWidth() < 40)
            continue;

        const bool on = isMasterOn()
                     && *processor.apvts.getRawParameterValue (
                            params::harmonyID (params::harmEnable, v)) > 0.5f;

        g.setColour (colours::textFaint);
        g.setFont (FuturisticLookAndFeel::uiFont (9.0f, true));
        g.drawText ("SINGING", r.removeFromTop (13), juce::Justification::centred, false);

        if (! on)
        {
            g.setColour (colours::textFaint);
            g.setFont (FuturisticLookAndFeel::uiFont (12.0f));
            g.drawText ("off", r, juce::Justification::centred, false);
            continue;
        }

        if (! liveVoiced || liveMidi <= 0.0f)
        {
            g.setColour (colours::textFaint);
            g.setFont (FuturisticLookAndFeel::monoFont (15.0f));
            g.drawText ("--", r, juce::Justification::centred, false);
            continue;
        }

        const int degrees = (int) *processor.apvts.getRawParameterValue (
                                params::harmonyID (params::harmDegrees, v));

        const float note = q.transposeByScaleDegrees (liveMidi, degrees);
        const int rounded = juce::jlimit (0, 127, (int) std::lround (note));

        auto chip = r.withSizeKeepingCentre (juce::jmin (r.getWidth(), 78), 28);
        g.setColour (kVoiceColours[v].withAlpha (0.18f));
        g.fillRoundedRectangle (chip.toFloat(), 4.0f);
        g.setColour (kVoiceColours[v].withAlpha (0.55f));
        g.drawRoundedRectangle (chip.toFloat(), 4.0f, 1.0f);

        g.setColour (kVoiceColours[v]);
        g.setFont (FuturisticLookAndFeel::monoFont (16.0f, true));
        g.drawText (juce::String (kNoteNames[((rounded % 12) + 12) % 12])
                        + juce::String (rounded / 12 - 1),
                    chip, juce::Justification::centred, false);
    }
}

} // namespace helix::ui
