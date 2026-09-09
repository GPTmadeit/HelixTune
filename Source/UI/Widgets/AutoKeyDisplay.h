#pragma once

#include "../FuturisticLookAndFeel.h"
#include "../../PluginProcessor.h"

namespace helix::ui
{

/** Auto-Key readout: the detected key, how sure the detector is, and the
    pitch-class histogram it reached that conclusion from.

    Showing the histogram rather than just the answer matters - when the
    detector picks the relative minor instead of the major, the weighting is
    right there to explain why, and the user can override with one click
    instead of wondering whether the plugin is broken.
*/
class AutoKeyDisplay : public juce::Component,
                       private juce::Timer
{
public:
    explicit AutoKeyDisplay (HelixTuneProcessor& p);
    ~AutoKeyDisplay() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void applyDetectedKey();

    HelixTuneProcessor& processor;

    juce::ToggleButton followButton { "Follow" };
    juce::TextButton   applyButton  { "APPLY" };

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> followAttachment;

    float smoothedChroma[12] { };
    KeyDetector::Result last;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutoKeyDisplay)
};

} // namespace helix::ui
