#pragma once

#include "../FuturisticLookAndFeel.h"
#include "../../PluginProcessor.h"
#include <deque>

namespace helix::ui
{

/** Scrolling real-time pitch display.

    Shows the sung pitch and the corrected pitch on the same axis with the
    correction shaded between them, so the amount of work the plugin is doing
    is visible rather than inferred. That readout is the main reason to look at
    a pitch corrector at all while tracking.
*/
class PitchScope : public juce::Component,
                   private juce::Timer
{
public:
    explicit PitchScope (HelixTuneProcessor& p);
    ~PitchScope() override;

    void pushFrames (const std::vector<PitchFrame>& frames);
    void clear();

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    float pitchToY (float midi, juce::Rectangle<float> plot) const;
    bool  isInScale (int pitchClass) const;

    HelixTuneProcessor& processor;

    static constexpr size_t historyLength = 900;
    std::deque<PitchFrame> history;

    // Smoothed view window, so the axis glides rather than jumping every time
    // a new extreme arrives.
    float viewLow = 55.0f, viewHigh = 72.0f;
    float targetLow = 55.0f, targetHigh = 72.0f;

    float lastCents = 0.0f;
    float lastNote = -1.0f;
    bool  lastVoiced = false;

    static constexpr float gutterWidth = 34.0f;
    static constexpr float meterWidth  = 92.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitchScope)
};

} // namespace helix::ui
