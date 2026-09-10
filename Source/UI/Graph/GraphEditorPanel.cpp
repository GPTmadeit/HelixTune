#include "GraphEditorPanel.h"

namespace helix::ui
{

// ---------------------------------------------------------------------------
// ToolButton
// ---------------------------------------------------------------------------

ToolButton::ToolButton (GraphTool t, const juce::String& tooltip, const juce::String& shortcut)
    : juce::Button (tooltip), tool (t), shortcutText (shortcut)
{
    setTooltip (tooltip + "  [" + shortcut + "]");
    setClickingTogglesState (false);
}

juce::Path ToolButton::glyphFor (GraphTool t, juce::Rectangle<float> a)
{
    juce::Path p;
    const float x = a.getX(), y = a.getY(), w = a.getWidth(), h = a.getHeight();
    const auto c = a.getCentre();

    switch (t)
    {
        case GraphTool::arrow:
            p.startNewSubPath (x + w * 0.28f, y + h * 0.12f);
            p.lineTo (x + w * 0.28f, y + h * 0.86f);
            p.lineTo (x + w * 0.46f, y + h * 0.66f);
            p.lineTo (x + w * 0.60f, y + h * 0.94f);
            p.lineTo (x + w * 0.74f, y + h * 0.86f);
            p.lineTo (x + w * 0.60f, y + h * 0.58f);
            p.lineTo (x + w * 0.80f, y + h * 0.54f);
            p.closeSubPath();
            break;

        case GraphTool::note:
            p.addRoundedRectangle (x + w * 0.12f, y + h * 0.40f, w * 0.52f, h * 0.20f, 2.0f);
            p.addRoundedRectangle (x + w * 0.70f, y + h * 0.18f, w * 0.18f, h * 0.20f, 2.0f);
            break;

        case GraphTool::curve:
            p.startNewSubPath (x + w * 0.12f, y + h * 0.74f);
            p.cubicTo (x + w * 0.34f, y + h * 0.74f,
                       x + w * 0.34f, y + h * 0.26f,
                       x + w * 0.54f, y + h * 0.26f);
            p.cubicTo (x + w * 0.74f, y + h * 0.26f,
                       x + w * 0.74f, y + h * 0.62f,
                       x + w * 0.90f, y + h * 0.62f);
            break;

        case GraphTool::line:
            p.startNewSubPath (x + w * 0.14f, y + h * 0.82f);
            p.lineTo (x + w * 0.86f, y + h * 0.20f);
            break;

        case GraphTool::scissors:
            p.startNewSubPath (x + w * 0.20f, y + h * 0.16f);
            p.lineTo (x + w * 0.74f, y + h * 0.70f);
            p.startNewSubPath (x + w * 0.74f, y + h * 0.16f);
            p.lineTo (x + w * 0.20f, y + h * 0.70f);
            p.addEllipse (x + w * 0.14f, y + h * 0.68f, w * 0.20f, h * 0.20f);
            p.addEllipse (x + w * 0.62f, y + h * 0.68f, w * 0.20f, h * 0.20f);
            break;

        case GraphTool::eraser:
            p.startNewSubPath (x + w * 0.20f, y + h * 0.70f);
            p.lineTo (x + w * 0.50f, y + h * 0.24f);
            p.lineTo (x + w * 0.84f, y + h * 0.46f);
            p.lineTo (x + w * 0.54f, y + h * 0.86f);
            p.closeSubPath();
            break;

        case GraphTool::zoom:
            p.addEllipse (x + w * 0.16f, y + h * 0.14f, w * 0.52f, h * 0.52f);
            p.startNewSubPath (x + w * 0.62f, y + h * 0.60f);
            p.lineTo (x + w * 0.88f, y + h * 0.88f);
            break;

        case GraphTool::hand:
            // A four-way move cross reads as "pan" far more directly than a
            // literal hand at 16 px.
            p.startNewSubPath (c.x, y + h * 0.12f);
            p.lineTo (c.x, y + h * 0.88f);
            p.startNewSubPath (x + w * 0.12f, c.y);
            p.lineTo (x + w * 0.88f, c.y);
            p.startNewSubPath (c.x - w * 0.10f, y + h * 0.24f);
            p.lineTo (c.x, y + h * 0.12f);
            p.lineTo (c.x + w * 0.10f, y + h * 0.24f);
            p.startNewSubPath (c.x - w * 0.10f, y + h * 0.76f);
            p.lineTo (c.x, y + h * 0.88f);
            p.lineTo (c.x + w * 0.10f, y + h * 0.76f);
            break;
    }

    return p;
}

void ToolButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto area = getLocalBounds().toFloat().reduced (1.0f);
    const bool on = getToggleState();

    if (on)
    {
        juce::Path glow;
        glow.addRoundedRectangle (area, 5.0f);
        g.setColour (colours::cyan.withAlpha (0.18f));
        g.strokePath (glow, juce::PathStrokeType (4.0f));
    }

    if (on)
        g.setGradientFill (juce::ColourGradient (colours::cyan.withAlpha (0.38f), area.getCentreX(), area.getY(),
                                                 colours::cyan.withAlpha (0.18f), area.getCentreX(), area.getBottom(), false));
    else if (down)
        g.setColour (colours::bgSunken);
    else
        g.setGradientFill (juce::ColourGradient (colours::bgRaised.brighter (highlighted ? 0.30f : 0.14f),
                                                 area.getCentreX(), area.getY(),
                                                 colours::bgRaised.darker (0.16f), area.getCentreX(), area.getBottom(), false));

    g.fillRoundedRectangle (area, 5.0f);

    g.setColour (on ? colours::cyan
                    : (highlighted ? colours::cyan.withAlpha (0.8f) : colours::outline));
    g.drawRoundedRectangle (area, 5.0f, on ? 1.6f : 1.2f);

    if (! down)
    {
        g.setColour (juce::Colours::white.withAlpha (on ? 0.14f : 0.07f));
        g.drawLine (area.getX() + 5.0f, area.getY() + 1.2f,
                    area.getRight() - 5.0f, area.getY() + 1.2f, 1.0f);
    }

    const auto glyph = glyphFor (tool, area.reduced (area.getWidth() * 0.26f));

    // An icon-only control has no label to fall back on, so the glyph itself
    // has to carry full contrast in every state.
    const auto colour = on ? juce::Colours::white
                           : (highlighted ? juce::Colours::white : colours::text.withAlpha (0.82f));

    g.setColour (colour);

    // Solid shapes want filling, stroked ones want stroking; the pointer, the
    // note and the eraser are the closed glyphs.
    if (tool == GraphTool::arrow || tool == GraphTool::eraser || tool == GraphTool::note)
        g.fillPath (glyph);
    else
        g.strokePath (glyph, juce::PathStrokeType (1.7f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
}

// ---------------------------------------------------------------------------
// GraphEditorPanel
// ---------------------------------------------------------------------------

GraphEditorPanel::GraphEditorPanel (HelixTuneProcessor& p)
    : processor (p), roll (p)
{
    addAndMakeVisible (roll);

    struct ToolSpec { GraphTool tool; const char* name; const char* key; };
    static const ToolSpec specs[] = {
        { GraphTool::arrow,    "Select / Move",  "1" },
        { GraphTool::note,     "Draw Note",      "2" },
        { GraphTool::curve,    "Draw Curve",     "3" },
        { GraphTool::line,     "Pitch Line",     "4" },
        { GraphTool::scissors, "Split Note",     "5" },
        { GraphTool::eraser,   "Erase Note",     "6" },
        { GraphTool::zoom,     "Zoom",           "7" },
        { GraphTool::hand,     "Pan",            "8" },
    };

    for (const auto& s : specs)
    {
        auto* b = toolButtons.add (new ToolButton (s.tool, s.name, s.key));
        b->onClick = [this, t = s.tool] { roll.setTool (t); refreshState(); };
        addAndMakeVisible (b);
    }

    auto styleAction = [this] (juce::TextButton& b, juce::Colour accent, std::function<void()> action)
    {
        b.setColour (juce::TextButton::buttonOnColourId, accent);
        b.onClick = std::move (action);
        addAndMakeVisible (b);
    };

    styleAction (makeNotesButton,  colours::lime,   [this] { roll.makeNotes (false); refreshState(); });
    styleAction (makeCurvesButton, colours::lime,   [this] { roll.makeNotes (true);  refreshState(); });
    styleAction (clearNotesButton, colours::danger, [this] { roll.clearNotes(); refreshState(); });
    styleAction (clearTrackButton, colours::danger, [this] { roll.clearTrack(); });
    styleAction (fitButton,        colours::violet, [this] { roll.zoomToFit(); });
    styleAction (undoButton,       colours::cyan,   [this] { roll.undo(); refreshState(); });
    styleAction (redoButton,       colours::cyan,   [this] { roll.redo(); refreshState(); });
    styleAction (snapScaleButton,  colours::cyan,   [this] { roll.snapSelectionToScale(); refreshState(); });

    styleAction (clearOverrideButton, colours::amber, [this]
    {
        auto& model = processor.getGraphModel();
        model.beginTransaction();
        model.setSelectedRetune (-1.0f);
        model.setSelectedVibrato (-1.0f);
        model.commit();
        refreshState();
    });

    for (auto* t : { &snapToggle, &autoScrollToggle, &showOutputToggle })
    {
        t->setColour (juce::TextButton::buttonOnColourId, colours::cyan);
        addAndMakeVisible (t);
    }

    snapToggle.setToggleState (true, juce::dontSendNotification);
    autoScrollToggle.setToggleState (true, juce::dontSendNotification);
    showOutputToggle.setToggleState (true, juce::dontSendNotification);

    snapToggle.onClick       = [this] { roll.setSnapToSemitone (snapToggle.getToggleState()); };
    autoScrollToggle.onClick = [this] { roll.setAutoScroll (autoScrollToggle.getToggleState()); };
    showOutputToggle.onClick = [this] { roll.setShowOutput (showOutputToggle.getToggleState()); };

    // Per-note overrides. -1 means "inherit the global setting", which is why
    // the ranges start below zero rather than at it.
    retuneOverride.setRange (-1.0, 400.0, 1.0);
    retuneOverride.setValue (-1.0, juce::dontSendNotification);
    retuneOverride.setColour (juce::Slider::thumbColourId, colours::magenta);
    retuneOverride.textFromValueFunction = [] (double v)
    {
        return v < 0.0 ? juce::String ("Global") : juce::String (v, 0) + " ms";
    };
    retuneOverride.onValueChange = [this] { applyOverrides(); };
    retuneOverride.updateText();
    addAndMakeVisible (retuneOverride);

    vibratoOverride.setRange (-1.0, 2.0, 0.01);
    vibratoOverride.setValue (-1.0, juce::dontSendNotification);
    vibratoOverride.setColour (juce::Slider::thumbColourId, colours::amber);
    vibratoOverride.textFromValueFunction = [] (double v)
    {
        return v < 0.0 ? juce::String ("Global") : juce::String (v * 100.0, 0) + " %";
    };
    vibratoOverride.onValueChange = [this] { applyOverrides(); };
    vibratoOverride.updateText();
    addAndMakeVisible (vibratoOverride);

    selectionLabel.setColour (juce::Label::textColourId, colours::textDim);
    selectionLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (selectionLabel);

    roll.onModelChanged = [this] { refreshState(); };
    roll.setTool (GraphTool::arrow);
    refreshState();
}

void GraphEditorPanel::pushFrames (const std::vector<PitchFrame>& frames)
{
    roll.pushFrames (frames);
}

void GraphEditorPanel::applyOverrides()
{
    auto& model = processor.getGraphModel();
    if (model.getNumSelected() == 0)
        return;

    model.setSelectedRetune ((float) retuneOverride.getValue());
    model.setSelectedVibrato ((float) vibratoOverride.getValue());
    model.commit();
    repaint();
}

void GraphEditorPanel::refreshState()
{
    for (auto* b : toolButtons)
        b->setToggleState (b->getTool() == roll.getTool(), juce::dontSendNotification);

    auto& model = processor.getGraphModel();
    const int selected = model.getNumSelected();
    const int total = (int) model.getNotes().size();

    selectionLabel.setText (juce::String (total) + " notes  -  "
                                + juce::String (selected) + " selected",
                            juce::dontSendNotification);

    undoButton.setEnabled (model.canUndo());
    redoButton.setEnabled (model.canRedo());
    snapScaleButton.setEnabled (selected > 0);
    clearOverrideButton.setEnabled (selected > 0);
    retuneOverride.setEnabled (selected > 0);
    vibratoOverride.setEnabled (selected > 0);

    repaint();
}

void GraphEditorPanel::resized()
{
    auto area = getLocalBounds().reduced (8);

    toolbarBounds = area.removeFromTop (36);
    area.removeFromTop (6);

    statusBounds = area.removeFromBottom (34);
    area.removeFromBottom (6);

    roll.setBounds (area);

    // --- toolbar ----------------------------------------------------------
    auto bar = toolbarBounds.reduced (6, 4);

    for (auto* b : toolButtons)
    {
        b->setBounds (bar.removeFromLeft (28));
        bar.removeFromLeft (2);
    }

    bar.removeFromLeft (12);

    auto action = [&bar] (juce::Button& b, int w)
    {
        b.setBounds (bar.removeFromLeft (w));
        bar.removeFromLeft (4);
    };

    action (makeNotesButton, 100);
    action (makeCurvesButton, 108);
    action (clearNotesButton, 104);
    action (clearTrackButton, 96);

    bar.removeFromLeft (10);
    action (undoButton, 48);
    action (redoButton, 48);
    action (fitButton, 40);

    bar.removeFromLeft (10);
    showOutputToggle.setBounds (bar.removeFromRight (94));
    autoScrollToggle.setBounds (bar.removeFromRight (94));
    snapToggle.setBounds (bar.removeFromRight (78));

    // --- status strip -----------------------------------------------------
    auto status = statusBounds.reduced (8, 5);

    selectionLabel.setBounds (status.removeFromLeft (150));
    status.removeFromLeft (8);

    snapScaleButton.setBounds (status.removeFromLeft (108));
    status.removeFromLeft (4);
    clearOverrideButton.setBounds (status.removeFromLeft (148));
    status.removeFromLeft (14);

    const int sliderWidth = juce::jmax (140, (status.getWidth() - 20) / 2);
    retuneOverride.setBounds (status.removeFromLeft (sliderWidth));
    status.removeFromLeft (20);
    vibratoOverride.setBounds (status.removeFromLeft (sliderWidth));
}

void GraphEditorPanel::paint (juce::Graphics& g)
{
    drawGlassPanel (g, toolbarBounds.toFloat(), colours::cyan, 6.0f);
    drawGlassPanel (g, statusBounds.toFloat(), colours::amber, 6.0f);

    g.setColour (colours::textFaint);
    g.setFont (FuturisticLookAndFeel::uiFont (9.0f, true));

    g.drawText ("RETUNE", retuneOverride.getBounds().translated (0, -13).withHeight (12),
                juce::Justification::centredLeft, false);
    g.drawText ("VIBRATO", vibratoOverride.getBounds().translated (0, -13).withHeight (12),
                juce::Justification::centredLeft, false);
}

} // namespace helix::ui
