#pragma once

#include "FuturisticLookAndFeel.h"
#include "Widgets/NeonKnob.h"
#include "Widgets/ScaleKeyboard.h"
#include "Widgets/PitchScope.h"
#include "Widgets/AutoKeyDisplay.h"
#include "../PluginProcessor.h"

namespace helix::ui
{

/** Everything the classic automatic mode exposes, grouped the way the signal
    actually flows: what we are listening to, what we are aiming at, how hard
    we pull, and what we do to the timbre on the way out. */
class AutoModePanel : public juce::Component
{
public:
    explicit AutoModePanel (HelixTuneProcessor& p);

    void pushFrames (const std::vector<PitchFrame>& frames);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Section
    {
        juce::Rectangle<int> bounds;
        juce::String title;
        juce::Colour accent;
    };

    /** Readout for the correction header: the tempo Note Transition is locked
        to, and what the selected note value works out to at that tempo. */
    juce::String tempoTag() const;

    HelixTuneProcessor& processor;

    PitchScope     scope;
    ScaleKeyboard  keyboard;
    AutoKeyDisplay inputKey;

    NeonSelector keySelector    { "Key",   colours::cyan };
    NeonSelector scaleSelector  { "Scale", colours::cyan };
    NeonSelector inputSelector  { "Source", colours::violet };
    NeonSelector shapeSelector  { "Shape", colours::amber };

    NeonKnob amountKnob     { "Amount",     colours::magenta };
    NeonKnob retuneKnob     { "Retune",     colours::magenta };
    NeonKnob transitionKnob { "Transition", colours::magenta };
    NeonKnob flexKnob       { "Flex-Tune",  colours::magenta };
    NeonKnob humanizeKnob   { "Humanize",   colours::magenta };
    NeonKnob natVibKnob     { "Nat Vib",    colours::magenta };
    NeonKnob transposeKnob  { "Transpose",  colours::magenta };
    NeonKnob detuneKnob     { "Detune",     colours::magenta };

    NeonKnob trackingKnob  { "Tracking", colours::violet };
    NeonKnob stabilityKnob { "Stability", colours::violet };
    NeonKnob sibilanceKnob { "Sibilance", colours::violet };
    NeonKnob throatKnob    { "Throat",   colours::lime };

    NeonKnob vibRateKnob    { "Rate",     colours::amber };
    NeonKnob vibVarKnob     { "Variation", colours::amber };
    NeonKnob vibDelayKnob   { "Delay",    colours::amber };
    NeonKnob vibOnsetKnob   { "Onset",    colours::amber };
    NeonKnob vibPitchKnob   { "Pitch",    colours::amber };
    NeonKnob vibAmpKnob     { "Amplitude", colours::amber };
    NeonKnob vibFormantKnob { "Formant",  colours::amber };

    NeonToggle ignoreVibToggle { "Targeting Ignores Vibrato", colours::magenta };
    NeonToggle classicToggle   { "Classic Mode", colours::magenta };
    NeonToggle formantToggle   { "Formant Correction", colours::lime };
    NeonToggle midiOutToggle   { "MIDI Out", colours::violet };

    std::vector<Section> sections;
    juce::Rectangle<int> correctionArea, tempoBounds;
    juce::String shownTempoTag;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutoModePanel)
};

} // namespace helix::ui
