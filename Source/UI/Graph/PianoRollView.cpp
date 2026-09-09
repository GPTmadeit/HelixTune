#include "PianoRollView.h"
#include "../../DSP/ScaleQuantizer.h"
#include <algorithm>

namespace helix::ui
{

static const char* kNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

static bool isBlackKey (int pc) { return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; }

PianoRollView::PianoRollView (HelixTuneProcessor& p) : processor (p)
{
    setWantsKeyboardFocus (true);
    setMouseCursor (juce::MouseCursor::NormalCursor);
    startTimerHz (30);
}

PianoRollView::~PianoRollView() { stopTimer(); }

// ---------------------------------------------------------------------------
// coordinates
// ---------------------------------------------------------------------------

juce::Rectangle<float> PianoRollView::gridArea() const noexcept
{
    return getLocalBounds().toFloat().withTrimmedLeft (keyWidth).withTrimmedTop (rulerHeight);
}

double PianoRollView::xToTime (float x) const noexcept
{
    return viewStartTime + (double) (x - gridArea().getX()) / pixelsPerSecond;
}

float PianoRollView::timeToX (double t) const noexcept
{
    return gridArea().getX() + (float) ((t - viewStartTime) * pixelsPerSecond);
}

float PianoRollView::yToPitch (float y) const noexcept
{
    return viewTopPitch - (y - gridArea().getY()) / pixelsPerSemitone;
}

float PianoRollView::pitchToY (float p) const noexcept
{
    return gridArea().getY() + (viewTopPitch - p) * pixelsPerSemitone;
}

juce::Rectangle<float> PianoRollView::noteBounds (const GraphNote& n) const noexcept
{
    const float x1 = timeToX (n.startTime);
    const float x2 = timeToX (n.endTime);
    const float y = pitchToY (n.pitchMidi);
    const float h = juce::jmax (5.0f, pixelsPerSemitone * 0.72f);

    return { x1, y - h * 0.5f, juce::jmax (2.0f, x2 - x1), h };
}

void PianoRollView::clampView()
{
    pixelsPerSecond   = juce::jlimit (8.0, 2000.0, pixelsPerSecond);
    pixelsPerSemitone = juce::jlimit (4.0f, 44.0f, pixelsPerSemitone);
    viewStartTime     = juce::jmax (0.0, viewStartTime);

    const float visibleSemis = gridArea().getHeight() / pixelsPerSemitone;
    viewTopPitch = juce::jlimit (visibleSemis + 4.0f, 127.0f, viewTopPitch);
}

bool PianoRollView::isInScale (int pc) const
{
    const int key = (int) *processor.apvts.getRawParameterValue (params::key);
    const int scaleIdx = (int) *processor.apvts.getRawParameterValue (params::scale);

    const auto* table = getScaleTable();
    const auto mask = table[juce::jlimit (0, getNumScales() - 1, scaleIdx)].mask;

    return ((mask >> (((pc - key) % 12 + 12) % 12)) & 1) != 0;
}

// ---------------------------------------------------------------------------
// data
// ---------------------------------------------------------------------------

void PianoRollView::pushFrames (const std::vector<PitchFrame>& frames)
{
    track.appendAll (frames);
}

void PianoRollView::timerCallback()
{
    if (autoScroll && processor.isTransportPlaying())
    {
        const double t = processor.getPlayheadSeconds();
        const double visible = (double) gridArea().getWidth() / pixelsPerSecond;

        // Page rather than centre-lock: a display that slides continuously is
        // much harder to read note shapes off than one that jumps a screen.
        if (t < viewStartTime || t > viewStartTime + visible * 0.85)
            viewStartTime = juce::jmax (0.0, t - visible * 0.15);
    }

    repaint();
}

void PianoRollView::commitModel()
{
    processor.getGraphModel().commit();

    if (onModelChanged)
        onModelChanged();

    repaint();
}

void PianoRollView::makeNotes (bool withCurves)
{
    auto& model = processor.getGraphModel();
    model.beginTransaction();

    ScaleQuantizer q;
    q.setKey ((int) *processor.apvts.getRawParameterValue (params::key));
    q.setScale ((int) *processor.apvts.getRawParameterValue (params::scale));

    for (int pc = 0; pc < 12; ++pc)
        q.setNoteState (pc, processor.getNoteState (pc));

    if (withCurves)
        model.makeCurvesFromTrack (track, q);
    else
        model.makeNotesFromTrack (track, q);

    commitModel();
}

void PianoRollView::clearNotes()
{
    auto& model = processor.getGraphModel();
    model.beginTransaction();
    model.clear();
    commitModel();
}

void PianoRollView::clearTrack()
{
    track.clear();
    repaint();
}

void PianoRollView::undo() { processor.getGraphModel().undo(); commitModel(); }
void PianoRollView::redo() { processor.getGraphModel().redo(); commitModel(); }

void PianoRollView::deleteSelection()
{
    auto& model = processor.getGraphModel();
    if (model.getNumSelected() == 0)
        return;

    model.beginTransaction();
    model.removeSelected();
    commitModel();
}

void PianoRollView::snapSelectionToScale()
{
    auto& model = processor.getGraphModel();
    if (model.getNumSelected() == 0)
        return;

    ScaleQuantizer q;
    q.setKey ((int) *processor.apvts.getRawParameterValue (params::key));
    q.setScale ((int) *processor.apvts.getRawParameterValue (params::scale));
    for (int pc = 0; pc < 12; ++pc)
        q.setNoteState (pc, processor.getNoteState (pc));

    model.beginTransaction();
    model.snapSelectedToScale (q);
    commitModel();
}

void PianoRollView::zoomToFit()
{
    float lo = 0.0f, hi = 0.0f;

    if (track.getVoicedPitchRange (lo, hi))
    {
        const float span = juce::jmax (12.0f, (hi - lo) + 6.0f);
        pixelsPerSemitone = gridArea().getHeight() / span;
        viewTopPitch = hi + 3.0f;
    }

    const double duration = track.getEndTime() - track.getStartTime();
    if (duration > 0.05)
    {
        pixelsPerSecond = (double) gridArea().getWidth() / duration;
        viewStartTime = track.getStartTime();
    }

    clampView();
    repaint();
}

void PianoRollView::setTool (GraphTool t)
{
    tool = t;

    switch (tool)
    {
        case GraphTool::hand:     setMouseCursor (juce::MouseCursor::DraggingHandCursor); break;
        case GraphTool::zoom:     setMouseCursor (juce::MouseCursor::CrosshairCursor); break;
        case GraphTool::note:
        case GraphTool::curve:
        case GraphTool::line:     setMouseCursor (juce::MouseCursor::CrosshairCursor); break;
        case GraphTool::scissors:
        case GraphTool::eraser:   setMouseCursor (juce::MouseCursor::PointingHandCursor); break;
        default:                  setMouseCursor (juce::MouseCursor::NormalCursor); break;
    }
}

void PianoRollView::ensureCurve (GraphNote& n)
{
    if (! n.curve.empty())
        return;

    n.curve.reserve ((size_t) NoteSpan::curveResolution);
    for (int i = 0; i < NoteSpan::curveResolution; ++i)
        n.curve.push_back ({ (float) i / (float) (NoteSpan::curveResolution - 1), 0.0f });
}

void PianoRollView::applyCurveAt (double time, float pitch)
{
    auto& model = processor.getGraphModel();
    auto* note = model.getNote (activeNoteId);

    if (note == nullptr || ! note->contains (time))
        return;

    ensureCurve (*note);

    const double len = juce::jmax (1.0e-6, note->length());
    const float u = (float) juce::jlimit (0.0, 1.0, (time - note->startTime) / len);
    const float offset = pitch - note->pitchMidi;

    // Paint a small neighbourhood so a fast drag does not leave gaps between
    // sampled mouse positions.
    const float radius = 1.5f / (float) (NoteSpan::curveResolution - 1);

    for (auto& p : note->curve)
        if (std::abs (p.x - u) <= radius)
            p.y = offset;
}

// ---------------------------------------------------------------------------
// painting
// ---------------------------------------------------------------------------

void PianoRollView::paint (juce::Graphics& g)
{
    const auto area = gridArea();

    g.fillAll (colours::bgSunken);

    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (area.toNearestInt());

        drawGrid (g, area);
        drawTrack (g, area);
        drawNotes (g, area);
        drawPlayhead (g, area);

        if (drag == Drag::rubberBand || drag == Drag::zoomBox)
        {
            const auto r = juce::Rectangle<float> (dragStart, dragCurrent);
            const auto c = (drag == Drag::zoomBox) ? colours::violet : colours::cyan;

            g.setColour (c.withAlpha (0.12f));
            g.fillRect (r);
            g.setColour (c.withAlpha (0.7f));
            g.drawRect (r, 1.0f);
        }

        if (drag == Drag::drawLine)
        {
            g.setColour (colours::lime.withAlpha (0.9f));
            g.drawLine ({ dragStart, dragCurrent }, 1.6f);
        }
    }

    drawKeyboard (g);
    drawRuler (g);

    g.setColour (colours::outline);
    g.drawRect (getLocalBounds(), 1);
}

void PianoRollView::drawGrid (juce::Graphics& g, juce::Rectangle<float> area)
{
    const int lowNote = (int) std::floor (yToPitch (area.getBottom()));
    const int highNote = (int) std::ceil (yToPitch (area.getY()));

    for (int n = lowNote; n <= highNote; ++n)
    {
        const int pc = ((n % 12) + 12) % 12;
        const float y = pitchToY ((float) n);
        const float rowTop = y - pixelsPerSemitone * 0.5f;

        // Row shading carries scale membership so the user can aim at a legal
        // pitch without reading the keyboard on the left.
        if (isInScale (pc))
            g.setColour (colours::cyan.withAlpha (pc == 0 ? 0.075f : 0.038f));
        else
            g.setColour (isBlackKey (pc) ? juce::Colours::black.withAlpha (0.28f)
                                         : juce::Colours::transparentBlack);

        g.fillRect (area.getX(), rowTop, area.getWidth(), pixelsPerSemitone);

        g.setColour (pc == 0 ? colours::gridBold : colours::grid.withAlpha (0.6f));
        g.fillRect (area.getX(), rowTop, area.getWidth(), pc == 0 ? 1.0f : 0.5f);
    }

    // Time grid: pick a step that keeps lines at least 60 px apart.
    const double candidates[] = { 0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 30.0, 60.0 };
    double step = candidates[0];
    for (double c : candidates)
    {
        step = c;
        if (c * pixelsPerSecond >= 60.0)
            break;
    }

    const double first = std::floor (viewStartTime / step) * step;
    for (double t = first; timeToX (t) < area.getRight(); t += step)
    {
        const float x = timeToX (t);
        if (x < area.getX())
            continue;

        const bool major = std::abs (std::fmod (t, step * 4.0)) < step * 0.5;
        g.setColour (major ? colours::gridBold : colours::grid.withAlpha (0.55f));
        g.fillRect (x, area.getY(), major ? 1.0f : 0.5f, area.getHeight());
    }
}

void PianoRollView::drawTrack (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto& frames = track.getFrames();
    if (frames.size() < 2)
        return;

    const double endTime = xToTime (area.getRight());
    int i = track.indexAtOrAfter (viewStartTime);
    i = juce::jmax (0, i - 1);

    juce::Path detected, output;
    bool detOpen = false, outOpen = false;

    for (; i < (int) frames.size(); ++i)
    {
        const auto& f = frames[(size_t) i];
        if (f.timeSeconds > endTime)
            break;

        if (! f.voiced)
        {
            detOpen = outOpen = false;
            continue;
        }

        const float x = timeToX (f.timeSeconds);

        const float yd = pitchToY (f.detectedMidi);
        if (! detOpen) { detected.startNewSubPath (x, yd); detOpen = true; }
        else           { detected.lineTo (x, yd); }

        if (showOutput)
        {
            const float yo = pitchToY (f.outputMidi);
            if (! outOpen) { output.startNewSubPath (x, yo); outOpen = true; }
            else           { output.lineTo (x, yo); }
        }
    }

    glowPath (g, detected, colours::cyan, 1.5f, 0.8f);

    if (showOutput)
        glowPath (g, output, colours::magenta, 1.7f, 0.9f);
}

void PianoRollView::drawNotes (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto& notes = processor.getGraphModel().getNotes();

    for (const auto& n : notes)
    {
        const auto r = noteBounds (n);
        if (r.getRight() < area.getX() || r.getX() > area.getRight())
            continue;

        const bool sel = n.selected;
        const bool hover = (n.id == hoverNoteId);

        g.setColour (colours::lime.withAlpha (sel ? 0.34f : (hover ? 0.24f : 0.16f)));
        g.fillRoundedRectangle (r, 3.0f);

        if (sel)
            glowPath (g, [&] { juce::Path p; p.addRoundedRectangle (r, 3.0f); return p; }(),
                      colours::lime, 1.4f);
        else
        {
            g.setColour (colours::lime.withAlpha (hover ? 0.85f : 0.55f));
            g.drawRoundedRectangle (r, 3.0f, 1.0f);
        }

        // Curve, drawn across the note's own time span.
        if (! n.curve.empty())
        {
            juce::Path p;
            bool open = false;

            for (const auto& cp : n.curve)
            {
                const float x = timeToX (n.startTime + (double) cp.x * n.length());
                const float y = pitchToY (n.pitchMidi + cp.y);

                if (! open) { p.startNewSubPath (x, y); open = true; }
                else        { p.lineTo (x, y); }
            }

            glowPath (g, p, colours::lime.brighter (0.4f), 1.5f);
        }

        // Per-note overrides get a marker so they are not invisible state.
        if (n.retuneMs >= 0.0f || n.vibratoScale >= 0.0f)
        {
            g.setColour (colours::amber);
            g.fillEllipse (r.getX() + 3.0f, r.getY() + 2.0f, 3.5f, 3.5f);
        }

        if (r.getWidth() > 34.0f && pixelsPerSemitone > 10.0f)
        {
            const int nearest = (int) std::lround (n.pitchMidi);
            const int pc = ((nearest % 12) + 12) % 12;
            const float cents = (n.pitchMidi - (float) nearest) * 100.0f;

            juce::String label (kNoteNames[pc]);
            label << (nearest / 12 - 1);

            if (std::abs (cents) > 1.0f)
                label << (cents > 0 ? "+" : "") << juce::String (cents, 0);

            g.setColour (colours::text.withAlpha (sel ? 0.95f : 0.6f));
            g.setFont (FuturisticLookAndFeel::monoFont (juce::jmin (10.0f, r.getHeight() - 1.0f)));
            g.drawText (label, r.reduced (5.0f, 0.0f), juce::Justification::centredLeft, false);
        }
    }
}

void PianoRollView::drawPlayhead (juce::Graphics& g, juce::Rectangle<float> area)
{
    const float x = timeToX (processor.getPlayheadSeconds());
    if (x < area.getX() || x > area.getRight())
        return;

    juce::Path p;
    p.startNewSubPath (x, area.getY());
    p.lineTo (x, area.getBottom());
    glowPath (g, p, colours::amber, 1.2f, 0.9f);
}

void PianoRollView::drawKeyboard (juce::Graphics& g)
{
    const auto area = gridArea();
    const auto keys = juce::Rectangle<float> (0.0f, area.getY(), keyWidth, area.getHeight());

    g.setColour (colours::bgPanel);
    g.fillRect (keys);

    juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (keys.toNearestInt());

    const int lowNote = (int) std::floor (yToPitch (area.getBottom()));
    const int highNote = (int) std::ceil (yToPitch (area.getY()));

    for (int n = lowNote; n <= highNote; ++n)
    {
        const int pc = ((n % 12) + 12) % 12;
        const float y = pitchToY ((float) n);
        const auto r = juce::Rectangle<float> (0.0f, y - pixelsPerSemitone * 0.5f,
                                               keyWidth - 2.0f, pixelsPerSemitone);

        const bool black = isBlackKey (pc);

        g.setColour (black ? juce::Colour (0xff0a0f18) : juce::Colour (0xff1a2536));
        g.fillRect (r.reduced (0.0f, 0.5f));

        if (isInScale (pc))
        {
            g.setColour (colours::cyan.withAlpha (0.22f));
            g.fillRect (r.withWidth (3.0f).reduced (0.0f, 1.0f));
        }

        if (pixelsPerSemitone >= 9.0f && (pc == 0 || ! black))
        {
            g.setColour (pc == 0 ? colours::text.withAlpha (0.8f) : colours::textFaint);
            g.setFont (FuturisticLookAndFeel::monoFont (juce::jmin (9.5f, pixelsPerSemitone - 2.0f)));
            g.drawText (juce::String (kNoteNames[pc]) + juce::String (n / 12 - 1),
                        juce::Rectangle<float> (5.0f, y - pixelsPerSemitone * 0.5f,
                                                keyWidth - 8.0f, pixelsPerSemitone),
                        juce::Justification::centredRight, false);
        }
    }
}

void PianoRollView::drawRuler (juce::Graphics& g)
{
    const auto area = gridArea();
    const auto ruler = juce::Rectangle<float> (area.getX(), 0.0f, area.getWidth(), rulerHeight);

    g.setColour (colours::bgPanel);
    g.fillRect (juce::Rectangle<float> (0.0f, 0.0f, (float) getWidth(), rulerHeight));

    juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (ruler.toNearestInt());

    const double candidates[] = { 0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 30.0, 60.0 };
    double step = candidates[0];
    for (double c : candidates)
    {
        step = c;
        if (c * pixelsPerSecond >= 60.0)
            break;
    }

    g.setFont (FuturisticLookAndFeel::monoFont (9.5f));

    const double first = std::floor (viewStartTime / step) * step;
    for (double t = first; timeToX (t) < ruler.getRight(); t += step)
    {
        const float x = timeToX (t);
        if (x < ruler.getX())
            continue;

        g.setColour (colours::outline);
        g.fillRect (x, rulerHeight - 6.0f, 1.0f, 6.0f);

        const int mins = (int) (t / 60.0);
        const double secs = t - mins * 60.0;
        juce::String label;

        if (mins > 0)
            label << mins << ":" << juce::String (secs, secs < 10.0 ? 2 : 1).paddedLeft ('0', 4);
        else
            label << juce::String (t, step < 1.0 ? 2 : 1) << "s";

        g.setColour (colours::textFaint);
        g.drawText (label, juce::Rectangle<float> (x + 3.0f, 1.0f, 60.0f, rulerHeight - 7.0f),
                    juce::Justification::centredLeft, false);
    }

    g.setColour (colours::outline);
    g.fillRect (0.0f, rulerHeight - 1.0f, (float) getWidth(), 1.0f);
}

void PianoRollView::resized()
{
    clampView();
}

// ---------------------------------------------------------------------------
// interaction
// ---------------------------------------------------------------------------

void PianoRollView::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();

    const auto pos = e.position;
    const auto area = gridArea();

    dragStart = dragCurrent = pos;
    dragStartTime = xToTime (pos.x);
    dragStartPitch = yToPitch (pos.y);

    // The ruler and the keyboard gutter always pan, whatever tool is active.
    if (! area.contains (pos))
    {
        drag = Drag::pan;
        return;
    }

    auto& model = processor.getGraphModel();
    auto* hit = model.hitTest (dragStartTime, dragStartPitch, 0.55f);

    switch (tool)
    {
        case GraphTool::hand:
            drag = Drag::pan;
            return;

        case GraphTool::zoom:
            drag = Drag::zoomBox;
            return;

        case GraphTool::eraser:
            if (hit != nullptr)
            {
                model.beginTransaction();
                model.removeNote (hit->id);
                commitModel();
            }
            drag = Drag::none;
            return;

        case GraphTool::scissors:
            if (hit != nullptr)
            {
                model.beginTransaction();
                model.splitNoteAt (hit->id, dragStartTime);
                commitModel();
            }
            drag = Drag::none;
            return;

        case GraphTool::note:
        {
            model.beginTransaction();
            const float pitch = snapToSemitone ? std::round (dragStartPitch) : dragStartPitch;
            activeNoteId = model.addNote (dragStartTime, dragStartTime + 0.03, pitch);
            model.selectAll (false);
            model.setSelected (activeNoteId, true, false);
            pendingNoteStart = dragStartTime;
            pendingNotePitch = pitch;
            drag = Drag::createNote;
            commitModel();
            return;
        }

        case GraphTool::curve:
            if (hit != nullptr)
            {
                model.beginTransaction();
                activeNoteId = hit->id;
                drag = Drag::drawCurve;
                applyCurveAt (dragStartTime, dragStartPitch);
                commitModel();
            }
            return;

        case GraphTool::line:
            model.beginTransaction();
            drag = Drag::drawLine;
            return;

        case GraphTool::arrow:
        default:
            break;
    }

    if (hit == nullptr)
    {
        if (! e.mods.isShiftDown())
            model.selectAll (false);

        drag = Drag::rubberBand;
        repaint();
        return;
    }

    if (e.mods.isShiftDown())
        model.setSelected (hit->id, ! hit->selected, false);
    else if (! hit->selected)
        model.setSelected (hit->id, true, true);

    const auto r = noteBounds (*hit);
    activeNoteId = hit->id;

    if (std::abs (pos.x - r.getX()) <= edgeGrab)
        drag = Drag::resizeLeft;
    else if (std::abs (pos.x - r.getRight()) <= edgeGrab)
        drag = Drag::resizeRight;
    else
        drag = Drag::moveNotes;

    model.beginTransaction();
    dragSnapshot = model.getNotes();
    repaint();
}

void PianoRollView::mouseDrag (const juce::MouseEvent& e)
{
    dragCurrent = e.position;

    auto& model = processor.getGraphModel();
    const double time = xToTime (e.position.x);
    const float pitch = yToPitch (e.position.y);

    switch (drag)
    {
        case Drag::pan:
        {
            const auto delta = e.position - dragStart;
            viewStartTime -= (double) delta.x / pixelsPerSecond;
            viewTopPitch  += delta.y / pixelsPerSemitone;
            dragStart = e.position;
            clampView();
            repaint();
            return;
        }

        case Drag::moveNotes:
        {
            model.getNotesForEdit() = dragSnapshot;

            double dt = time - dragStartTime;
            float dp = pitch - dragStartPitch;

            if (snapToSemitone)
            {
                // Snap the dragged note's resulting pitch to a semitone rather
                // than snapping the delta, so a note that started off-grid
                // lands on the grid instead of staying offset from it.
                if (const auto* anchor = model.getNote (activeNoteId))
                    dp = std::round (anchor->pitchMidi + dp) - anchor->pitchMidi;
            }

            if (e.mods.isAltDown())
                dt = 0.0;   // constrain to pitch only

            model.moveSelected (dt, dp);
            commitModel();
            return;
        }

        case Drag::resizeLeft:
            model.getNotesForEdit() = dragSnapshot;
            model.resizeSelected (time - dragStartTime, 0.0);
            commitModel();
            return;

        case Drag::resizeRight:
            model.getNotesForEdit() = dragSnapshot;
            model.resizeSelected (0.0, time - dragStartTime);
            commitModel();
            return;

        case Drag::createNote:
        {
            if (auto* n = model.getNote (activeNoteId))
            {
                n->startTime = juce::jmax (0.0, std::min (pendingNoteStart, time));
                n->endTime   = juce::jmax (n->startTime + 0.02, std::max (pendingNoteStart, time));

                if (! snapToSemitone)
                    n->pitchMidi = pitch;
            }

            commitModel();
            return;
        }

        case Drag::drawCurve:
            applyCurveAt (time, pitch);
            commitModel();
            return;

        case Drag::rubberBand:
        case Drag::zoomBox:
        case Drag::drawLine:
            repaint();
            return;

        default:
            return;
    }
}

void PianoRollView::mouseUp (const juce::MouseEvent& e)
{
    auto& model = processor.getGraphModel();

    if (drag == Drag::rubberBand)
    {
        const auto r = juce::Rectangle<float> (dragStart, dragCurrent);
        const double t1 = xToTime (r.getX());
        const double t2 = xToTime (r.getRight());
        const float p1 = yToPitch (r.getBottom());
        const float p2 = yToPitch (r.getY());

        for (auto& n : model.getNotesForEdit())
            if (n.endTime >= t1 && n.startTime <= t2 && n.pitchMidi >= p1 && n.pitchMidi <= p2)
                n.selected = true;

        commitModel();
    }
    else if (drag == Drag::zoomBox)
    {
        const auto r = juce::Rectangle<float> (dragStart, dragCurrent);

        if (r.getWidth() > 8.0f && r.getHeight() > 8.0f)
        {
            const double t1 = xToTime (r.getX());
            const double t2 = xToTime (r.getRight());
            const float pTop = yToPitch (r.getY());
            const float pBottom = yToPitch (r.getBottom());

            pixelsPerSecond   = (double) gridArea().getWidth() / juce::jmax (0.05, t2 - t1);
            pixelsPerSemitone = gridArea().getHeight() / juce::jmax (1.0f, pTop - pBottom);
            viewStartTime = t1;
            viewTopPitch = pTop;
        }
        else
        {
            // A plain click zooms about the cursor.
            const double anchorTime = xToTime (dragStart.x);
            const float anchorPitch = yToPitch (dragStart.y);
            const double factor = e.mods.isAltDown() ? 0.6 : 1.6;

            pixelsPerSecond *= factor;
            pixelsPerSemitone *= (float) factor;
            clampView();

            viewStartTime = anchorTime - (double) (dragStart.x - gridArea().getX()) / pixelsPerSecond;
            viewTopPitch  = anchorPitch + (dragStart.y - gridArea().getY()) / pixelsPerSemitone;
        }

        clampView();
        repaint();
    }
    else if (drag == Drag::drawLine)
    {
        const double t1 = xToTime (juce::jmin (dragStart.x, dragCurrent.x));
        const double t2 = xToTime (juce::jmax (dragStart.x, dragCurrent.x));
        const bool forward = dragCurrent.x >= dragStart.x;
        const float pStart = yToPitch (forward ? dragStart.y : dragCurrent.y);
        const float pEnd   = yToPitch (forward ? dragCurrent.y : dragStart.y);

        // Write the ramp into every note the drag crossed.
        for (auto& n : model.getNotesForEdit())
        {
            if (n.endTime < t1 || n.startTime > t2)
                continue;

            ensureCurve (n);

            for (auto& cp : n.curve)
            {
                const double t = n.startTime + (double) cp.x * n.length();
                if (t < t1 || t > t2)
                    continue;

                const float u = (float) ((t - t1) / juce::jmax (1.0e-6, t2 - t1));
                cp.y = (pStart + u * (pEnd - pStart)) - n.pitchMidi;
            }
        }

        commitModel();
    }
    else if (drag == Drag::createNote)
    {
        // Discard accidental zero-length notes from a click that never dragged.
        if (auto* n = model.getNote (activeNoteId))
            if (n->length() < 0.04)
                n->endTime = n->startTime + 0.15;

        commitModel();
    }

    drag = Drag::none;
    dragSnapshot.clear();
    repaint();
}

void PianoRollView::mouseMove (const juce::MouseEvent& e)
{
    auto& model = processor.getGraphModel();
    auto* hit = model.hitTest (xToTime (e.position.x), yToPitch (e.position.y), 0.55f);
    const int id = hit != nullptr ? hit->id : 0;

    if (id != hoverNoteId)
    {
        hoverNoteId = id;
        repaint();
    }

    if (tool == GraphTool::arrow)
    {
        if (hit != nullptr)
        {
            const auto r = noteBounds (*hit);
            const bool onEdge = std::abs (e.position.x - r.getX()) <= edgeGrab
                             || std::abs (e.position.x - r.getRight()) <= edgeGrab;

            setMouseCursor (onEdge ? juce::MouseCursor::LeftRightResizeCursor
                                   : juce::MouseCursor::DraggingHandCursor);
        }
        else
        {
            setMouseCursor (juce::MouseCursor::NormalCursor);
        }
    }
}

void PianoRollView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (tool != GraphTool::arrow)
        return;

    auto& model = processor.getGraphModel();

    if (auto* hit = model.hitTest (xToTime (e.position.x), yToPitch (e.position.y), 0.55f))
    {
        model.beginTransaction();
        model.setSelected (hit->id, true, true);
        snapSelectionToScale();
    }
}

void PianoRollView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCommandDown())
    {
        // Zoom about the cursor so the thing under the pointer stays put.
        const double anchorTime = xToTime (e.position.x);
        const float anchorPitch = yToPitch (e.position.y);
        const double factor = std::pow (1.25, (double) w.deltaY * 3.0);

        if (e.mods.isShiftDown())
            pixelsPerSemitone *= (float) factor;
        else
            pixelsPerSecond *= factor;

        clampView();

        viewStartTime = anchorTime - (double) (e.position.x - gridArea().getX()) / pixelsPerSecond;
        viewTopPitch  = anchorPitch + (e.position.y - gridArea().getY()) / pixelsPerSemitone;
    }
    else if (e.mods.isShiftDown())
    {
        viewStartTime -= (double) w.deltaY * 220.0 / pixelsPerSecond;
    }
    else
    {
        viewTopPitch += w.deltaY * 5.0f;
        viewStartTime -= (double) w.deltaX * 220.0 / pixelsPerSecond;
    }

    clampView();
    repaint();
}

bool PianoRollView::keyPressed (const juce::KeyPress& k)
{
    auto& model = processor.getGraphModel();

    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey)
    {
        deleteSelection();
        return true;
    }

    if (k == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0))       { undo(); return true; }
    if (k == juce::KeyPress ('y', juce::ModifierKeys::commandModifier, 0))       { redo(); return true; }
    if (k == juce::KeyPress ('z', juce::ModifierKeys::commandModifier
                                  | juce::ModifierKeys::shiftModifier, 0))       { redo(); return true; }

    if (k == juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0))
    {
        model.selectAll (true);
        commitModel();
        return true;
    }

    if (model.getNumSelected() > 0)
    {
        const bool fine = k.getModifiers().isShiftDown();

        if (k.getKeyCode() == juce::KeyPress::upKey)
        {
            model.beginTransaction();
            model.nudgeSelectedCents (fine ? 1.0f : 100.0f);
            commitModel();
            return true;
        }

        if (k.getKeyCode() == juce::KeyPress::downKey)
        {
            model.beginTransaction();
            model.nudgeSelectedCents (fine ? -1.0f : -100.0f);
            commitModel();
            return true;
        }
    }

    // Tool shortcuts, matching the toolbar order.
    static const std::pair<int, GraphTool> shortcuts[] = {
        { '1', GraphTool::arrow }, { '2', GraphTool::note },  { '3', GraphTool::curve },
        { '4', GraphTool::line },  { '5', GraphTool::scissors }, { '6', GraphTool::eraser },
        { '7', GraphTool::zoom },  { '8', GraphTool::hand }
    };

    for (const auto& s : shortcuts)
    {
        if (k.getKeyCode() == s.first)
        {
            setTool (s.second);
            if (onModelChanged)
                onModelChanged();
            return true;
        }
    }

    return false;
}

} // namespace helix::ui
