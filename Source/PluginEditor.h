#pragma once

#include "PluginProcessor.h"
#include "UI/FuturisticLookAndFeel.h"
#include "UI/AutoModePanel.h"
#include "UI/Graph/GraphEditorPanel.h"

namespace helix
{

class HelixTuneEditor : public juce::AudioProcessorEditor,
                        private juce::Timer
{
public:
    explicit HelixTuneEditor (HelixTuneProcessor&);
    ~HelixTuneEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void updateMode();

    HelixTuneProcessor& processor;
    ui::FuturisticLookAndFeel lookAndFeel;

    ui::AutoModePanel    autoPanel;
    ui::GraphEditorPanel graphPanel;

    juce::TextButton autoModeButton  { "AUTO" };
    juce::TextButton graphModeButton { "GRAPH" };
    juce::ToggleButton bypassButton  { "Bypass" };

    juce::Slider mixSlider    { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Slider outputSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment, outputAttachment;

    juce::Rectangle<int> headerBounds, latencyBounds;

    // Drained from the audio thread each tick and fanned out to both panels,
    // so the graph editor keeps capturing even while auto mode is on screen.
    std::vector<PitchFrame> scratch;

    float bannerPhase = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HelixTuneEditor)
};

} // namespace helix
