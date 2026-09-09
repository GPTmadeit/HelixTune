#pragma once

#include "../FuturisticLookAndFeel.h"
#include "../../PluginProcessor.h"

namespace helix::ui
{

/** One-octave keyboard for editing the scale.

    Every pitch class shows three things at once: whether it belongs to the
    current scale, what the user has done to it (removed / bypassed), and
    whether the singer is on it right now. Auto-Tune splits these across
    separate Remove and Bypass button columns; folding them onto the keys
    themselves keeps the cause and the effect in the same place.

    Click cycles a key through Normal, Removed and Bypassed. Right-click
    resets it.
*/
class ScaleKeyboard : public juce::Component,
                      private juce::Timer
{
public:
    explicit ScaleKeyboard (HelixTuneProcessor& p);
    ~ScaleKeyboard() override;

    /** Live detected pitch, for the "you are here" highlight. */
    void setLiveNote (float midiNote, bool voiced);

    std::function<void()> onStateChanged;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    int  pitchClassAt (juce::Point<int> pos) const;
    juce::Rectangle<float> keyBounds (int pitchClass) const;
    bool isInScale (int pitchClass) const;
    static bool isBlackKey (int pitchClass);

    HelixTuneProcessor& processor;

    float liveNote = -1.0f;
    bool  liveVoiced = false;
    float livePulse = 0.0f;      // decays so a stopped note fades rather than snapping off
    int   hoveredClass = -1;

    static constexpr float legendHeight = 16.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScaleKeyboard)
};

} // namespace helix::ui
