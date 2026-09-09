#pragma once

#include "../FuturisticLookAndFeel.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace helix::ui
{

/** Labelled rotary control.

    The caption and readout are painted rather than built from child Labels so
    the three elements can share one baseline grid - with stacked Labels the
    text drifts by a pixel or two at different heights and a row of knobs stops
    looking like a row.
*/
class NeonKnob : public juce::Component
{
public:
    NeonKnob (const juce::String& caption, juce::Colour accent = colours::cyan);
    ~NeonKnob() override;

    void attach (juce::AudioProcessorValueTreeState& state, const juce::String& parameterID);

    void setAccent (juce::Colour c);
    void setCaption (const juce::String& c) { caption = c; repaint(); }

    juce::Slider& getSlider() noexcept { return slider; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    juce::String caption;
    juce::Colour accent;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeonKnob)
};

/** Small labelled combo box that matches the knob's caption grid. */
class NeonSelector : public juce::Component
{
public:
    NeonSelector (const juce::String& caption, juce::Colour accent = colours::cyan);
    ~NeonSelector() override;

    void attach (juce::AudioProcessorValueTreeState& state, const juce::String& parameterID);
    juce::ComboBox& getBox() noexcept { return box; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::ComboBox box;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
    juce::String caption;
    juce::Colour accent;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeonSelector)
};

/** Pill toggle with a caption, attached to a bool parameter. */
class NeonToggle : public juce::Component
{
public:
    NeonToggle (const juce::String& text, juce::Colour accent = colours::cyan);
    ~NeonToggle() override;

    void attach (juce::AudioProcessorValueTreeState& state, const juce::String& parameterID);
    juce::ToggleButton& getButton() noexcept { return button; }

    void resized() override;

private:
    juce::ToggleButton button;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeonToggle)
};

} // namespace helix::ui
