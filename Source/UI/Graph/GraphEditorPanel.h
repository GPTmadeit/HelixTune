#pragma once

#include "PianoRollView.h"

namespace helix::ui
{

/** Icon-only toolbar button that paints a vector glyph, so the toolbar stays
    crisp at any host scaling factor and needs no image assets. */
class ToolButton : public juce::Button
{
public:
    ToolButton (GraphTool t, const juce::String& tooltip, const juce::String& shortcut);

    GraphTool getTool() const noexcept { return tool; }

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    static juce::Path glyphFor (GraphTool, juce::Rectangle<float> area);

    GraphTool tool;
    juce::String shortcutText;
};

/** Graph mode: the piano roll plus the tools and actions that drive it. */
class GraphEditorPanel : public juce::Component
{
public:
    explicit GraphEditorPanel (HelixTuneProcessor& p);

    void pushFrames (const std::vector<PitchFrame>& frames);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void refreshState();
    void applyOverrides();

    HelixTuneProcessor& processor;
    PianoRollView roll;

    juce::OwnedArray<ToolButton> toolButtons;

    juce::TextButton makeNotesButton   { "MAKE NOTES" };
    juce::TextButton makeCurvesButton  { "MAKE CURVES" };
    juce::TextButton clearNotesButton  { "CLEAR NOTES" };
    juce::TextButton clearTrackButton  { "CLEAR SCAN" };
    juce::TextButton fitButton         { "FIT" };
    juce::TextButton undoButton        { "UNDO" };
    juce::TextButton redoButton        { "REDO" };
    juce::TextButton snapScaleButton   { "SNAP TO SCALE" };
    juce::TextButton clearOverrideButton { "CLEAR OVERRIDES" };

    juce::ToggleButton snapToggle       { "Snap" };
    juce::ToggleButton autoScrollToggle { "Follow" };
    juce::ToggleButton showOutputToggle { "Output" };

    juce::Slider retuneOverride  { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider vibratoOverride { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label  selectionLabel;

    juce::TooltipWindow tooltips { this, 600 };

    juce::Rectangle<int> toolbarBounds, statusBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GraphEditorPanel)
};

} // namespace helix::ui
