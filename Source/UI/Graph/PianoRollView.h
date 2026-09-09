#pragma once

#include "../FuturisticLookAndFeel.h"
#include "../../PluginProcessor.h"

namespace helix::ui
{

enum class GraphTool
{
    arrow = 0,   // select, move, resize
    note,        // draw new notes
    curve,       // freehand pitch curve inside a note
    line,        // straight pitch ramp across notes
    scissors,    // split a note
    eraser,      // delete
    zoom,
    hand         // pan
};

/** The piano-roll editing surface.

    Draws three layers of pitch on one axis: what was sung (captured contour),
    what has been asked for (note objects), and what is coming out. Editing is
    direct - notes are dragged on the same grid the contour is drawn on, so
    there is never a question of which part of the performance a note governs.
*/
class PianoRollView : public juce::Component,
                      private juce::Timer
{
public:
    explicit PianoRollView (HelixTuneProcessor& p);
    ~PianoRollView() override;

    void pushFrames (const std::vector<PitchFrame>& frames);

    void setTool (GraphTool t);
    GraphTool getTool() const noexcept { return tool; }

    void setSnapToSemitone (bool s) { snapToSemitone = s; repaint(); }
    bool getSnapToSemitone() const noexcept { return snapToSemitone; }

    void setAutoScroll (bool s) { autoScroll = s; }
    bool getAutoScroll() const noexcept { return autoScroll; }

    void setShowOutput (bool s) { showOutput = s; repaint(); }

    void makeNotes (bool withCurves);
    void clearNotes();
    void clearTrack();
    void undo();
    void redo();
    void zoomToFit();
    void snapSelectionToScale();
    void deleteSelection();

    PitchTrack& getTrack() noexcept { return track; }

    /** Fired whenever the note model changed, so the toolbar can refresh. */
    std::function<void()> onModelChanged;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    enum class Drag
    {
        none, moveNotes, resizeLeft, resizeRight,
        createNote, rubberBand, drawCurve, drawLine, pan, zoomBox
    };

    void timerCallback() override;

    // coordinate mapping
    double xToTime (float x) const noexcept;
    float  timeToX (double t) const noexcept;
    float  yToPitch (float y) const noexcept;
    float  pitchToY (float p) const noexcept;

    juce::Rectangle<float> gridArea() const noexcept;
    juce::Rectangle<float> noteBounds (const GraphNote&) const noexcept;

    void drawGrid (juce::Graphics&, juce::Rectangle<float> area);
    void drawKeyboard (juce::Graphics&);
    void drawRuler (juce::Graphics&);
    void drawTrack (juce::Graphics&, juce::Rectangle<float> area);
    void drawNotes (juce::Graphics&, juce::Rectangle<float> area);
    void drawPlayhead (juce::Graphics&, juce::Rectangle<float> area);

    static void ensureCurve (GraphNote&);
    void applyCurveAt (double time, float pitch);
    void commitModel();
    bool isInScale (int pitchClass) const;
    void clampView();

    HelixTuneProcessor& processor;
    PitchTrack track;

    GraphTool tool = GraphTool::arrow;
    bool snapToSemitone = true;
    bool autoScroll = true;
    bool showOutput = true;

    // view
    double viewStartTime = 0.0;
    double pixelsPerSecond = 90.0;
    float  viewTopPitch = 76.0f;
    float  pixelsPerSemitone = 13.0f;

    // interaction
    Drag drag = Drag::none;
    juce::Point<float> dragStart, dragCurrent;
    double dragStartTime = 0.0;
    float  dragStartPitch = 0.0f;
    int    activeNoteId = 0;
    int    hoverNoteId = 0;
    GraphModel::NoteList dragSnapshot;
    double pendingNoteStart = 0.0;
    float  pendingNotePitch = 0.0f;

    static constexpr float keyWidth = 48.0f;
    static constexpr float rulerHeight = 22.0f;
    static constexpr float edgeGrab = 6.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRollView)
};

} // namespace helix::ui
