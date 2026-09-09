#pragma once

#include "FuturisticLookAndFeel.h"
#include "Widgets/NeonKnob.h"
#include "Widgets/AutoKeyDisplay.h"
#include "../PluginProcessor.h"

namespace helix::ui
{

/** Harmony mode: Auto-Key, the harmony bus, and one strip per voice.

    The live chord readout at the top is the important part - diatonic
    intervals change size depending on where in the scale the lead is, so
    "+3rd" alone does not tell you what you are about to hear. Showing the
    actual notes closes that gap.
*/
class HarmonyPanel : public juce::Component
{
public:
    explicit HarmonyPanel (HelixTuneProcessor& p);

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

    void paintChordReadout (juce::Graphics&, juce::Rectangle<int> area);

    HelixTuneProcessor& processor;
    AutoKeyDisplay autoKey;

    NeonKnob levelKnob  { "Level",  colours::violet };
    NeonKnob spreadKnob { "Spread", colours::violet };

    juce::OwnedArray<NeonToggle> voiceEnable;
    juce::OwnedArray<NeonKnob>   voiceInterval, voiceLevel, voicePan, voiceFormant, voiceDetune;

    std::vector<Section> sections;
    juce::Rectangle<int> chordBounds;
    juce::Array<juce::Rectangle<int>> voiceRows, voiceReadouts;

    float liveMidi = 0.0f;
    bool  liveVoiced = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HarmonyPanel)
};

} // namespace helix::ui
