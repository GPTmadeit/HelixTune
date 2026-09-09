#include "GraphModel.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace helix
{

// ---------------------------------------------------------------------------
// NoteSpan / NoteSnapshot
// ---------------------------------------------------------------------------

float NoteSpan::pitchAt (double t) const noexcept
{
    if (! hasCurve)
        return pitch;

    const double len = end - start;
    if (len <= 1.0e-9)
        return pitch;

    const float u = (float) juce::jlimit (0.0, 1.0, (t - start) / len);
    const float x = u * (float) (curveResolution - 1);
    const int   i = juce::jlimit (0, curveResolution - 2, (int) x);
    const float f = x - (float) i;

    return pitch + curve[(size_t) i] + f * (curve[(size_t) (i + 1)] - curve[(size_t) i]);
}

void NoteSnapshot::publish (const std::vector<GraphNote>& notes)
{
    const int inactive = 1 - active.load (std::memory_order_relaxed);
    auto& buf = buffers[(size_t) inactive];

    buf.count = 0;

    for (const auto& n : notes)
    {
        if (buf.count >= maxNotes)
            break;

        auto& s = buf.spans[(size_t) buf.count];
        s.start = n.startTime;
        s.end   = n.endTime;
        s.pitch = n.pitchMidi;
        s.retuneMs = n.retuneMs;
        s.vibratoScale = n.vibratoScale;
        s.hasCurve = ! n.curve.empty();

        if (s.hasCurve)
        {
            // Resample whatever the editor holds onto the fixed grid the audio
            // thread expects.
            for (int i = 0; i < NoteSpan::curveResolution; ++i)
            {
                const float u = (float) i / (float) (NoteSpan::curveResolution - 1);

                // The editor keeps curve points sorted by x.
                float y = n.curve.front().y;
                for (size_t k = 1; k < n.curve.size(); ++k)
                {
                    const auto& a = n.curve[k - 1];
                    const auto& b = n.curve[k];

                    if (u <= b.x)
                    {
                        const float span = std::max (1.0e-6f, b.x - a.x);
                        const float t = juce::jlimit (0.0f, 1.0f, (u - a.x) / span);
                        y = a.y + t * (b.y - a.y);
                        break;
                    }

                    y = b.y;
                }

                s.curve[(size_t) i] = y;
            }
        }

        ++buf.count;
    }

    active.store (inactive, std::memory_order_release);
}

const NoteSpan* NoteSnapshot::find (double timeSeconds) const noexcept
{
    const auto& buf = buffers[(size_t) active.load (std::memory_order_acquire)];

    if (buf.count == 0)
        return nullptr;

    // Spans are published sorted by start time and are non-overlapping.
    int lo = 0, hi = buf.count - 1, found = -1;
    while (lo <= hi)
    {
        const int mid = (lo + hi) / 2;
        if (buf.spans[(size_t) mid].start > timeSeconds)
            hi = mid - 1;
        else
        {
            found = mid;
            lo = mid + 1;
        }
    }

    if (found < 0)
        return nullptr;

    const auto& s = buf.spans[(size_t) found];
    return (timeSeconds < s.end) ? &s : nullptr;
}

// ---------------------------------------------------------------------------
// GraphModel
// ---------------------------------------------------------------------------

void GraphModel::sortNotes()
{
    std::sort (notes.begin(), notes.end(),
               [] (const GraphNote& a, const GraphNote& b) { return a.startTime < b.startTime; });
}

int GraphModel::addNote (double start, double end, float pitch)
{
    GraphNote n;
    n.id        = nextId++;
    n.startTime = std::min (start, end);
    n.endTime   = std::max (start, end);
    n.pitchMidi = pitch;
    notes.push_back (n);
    sortNotes();
    return n.id;
}

void GraphModel::removeNote (int id)
{
    notes.erase (std::remove_if (notes.begin(), notes.end(),
                                 [id] (const GraphNote& n) { return n.id == id; }),
                 notes.end());
}

void GraphModel::removeSelected()
{
    notes.erase (std::remove_if (notes.begin(), notes.end(),
                                 [] (const GraphNote& n) { return n.selected; }),
                 notes.end());
}

void GraphModel::clear()
{
    notes.clear();
}

GraphNote* GraphModel::getNote (int id) noexcept
{
    for (auto& n : notes)
        if (n.id == id)
            return &n;

    return nullptr;
}

const GraphNote* GraphModel::getNote (int id) const noexcept
{
    for (const auto& n : notes)
        if (n.id == id)
            return &n;

    return nullptr;
}

GraphNote* GraphModel::hitTest (double time, float pitch, float pitchTolerance) noexcept
{
    // Walk backwards so the most recently added note wins where notes stack.
    for (auto it = notes.rbegin(); it != notes.rend(); ++it)
        if (it->contains (time) && std::abs (it->pitchMidi - pitch) <= pitchTolerance)
            return &(*it);

    return nullptr;
}

void GraphModel::selectAll (bool shouldBeSelected)
{
    for (auto& n : notes)
        n.selected = shouldBeSelected;
}

void GraphModel::setSelected (int id, bool shouldBeSelected, bool exclusive)
{
    if (exclusive)
        selectAll (false);

    if (auto* n = getNote (id))
        n->selected = shouldBeSelected;
}

int GraphModel::getNumSelected() const noexcept
{
    return (int) std::count_if (notes.begin(), notes.end(),
                                [] (const GraphNote& n) { return n.selected; });
}

void GraphModel::moveSelected (double deltaSeconds, float deltaSemitones)
{
    for (auto& n : notes)
    {
        if (! n.selected)
            continue;

        n.startTime = std::max (0.0, n.startTime + deltaSeconds);
        n.endTime   = std::max (n.startTime + 0.01, n.endTime + deltaSeconds);
        n.pitchMidi = juce::jlimit (0.0f, 127.0f, n.pitchMidi + deltaSemitones);
    }

    sortNotes();
}

void GraphModel::resizeSelected (double deltaStart, double deltaEnd)
{
    for (auto& n : notes)
    {
        if (! n.selected)
            continue;

        n.startTime = std::max (0.0, n.startTime + deltaStart);
        n.endTime   = n.endTime + deltaEnd;

        if (n.endTime < n.startTime + 0.01)
            n.endTime = n.startTime + 0.01;
    }

    sortNotes();
}

void GraphModel::nudgeSelectedCents (float cents)
{
    for (auto& n : notes)
        if (n.selected)
            n.pitchMidi = juce::jlimit (0.0f, 127.0f, n.pitchMidi + cents * 0.01f);
}

void GraphModel::snapSelectedToScale (const ScaleQuantizer& scale)
{
    for (auto& n : notes)
    {
        if (! n.selected)
            continue;

        const auto t = scale.findTarget (n.pitchMidi);
        if (t.valid)
            n.pitchMidi = t.midiNote;
    }
}

void GraphModel::setSelectedRetune (float ms)
{
    for (auto& n : notes)
        if (n.selected)
            n.retuneMs = ms;
}

void GraphModel::setSelectedVibrato (float s)
{
    for (auto& n : notes)
        if (n.selected)
            n.vibratoScale = s;
}

void GraphModel::flattenSelectedCurves()
{
    for (auto& n : notes)
        if (n.selected)
            n.curve.clear();
}

void GraphModel::splitNoteAt (int id, double time)
{
    auto* n = getNote (id);
    if (n == nullptr || ! n->contains (time))
        return;

    // Both halves keep the shape they had, so cutting a note is non-destructive.
    GraphNote right = *n;
    right.id = nextId++;

    const double len = n->length();
    const double u = (len > 1.0e-9) ? (time - n->startTime) / len : 0.5;

    if (! n->curve.empty())
    {
        std::vector<CurvePoint> left, rightCurve;

        for (const auto& p : n->curve)
        {
            if (p.x <= (float) u)
                left.push_back ({ (float) (p.x / std::max (1.0e-6, u)), p.y });
            else
                rightCurve.push_back ({ (float) ((p.x - u) / std::max (1.0e-6, 1.0 - u)), p.y });
        }

        n->curve = std::move (left);
        right.curve = std::move (rightCurve);
    }

    right.startTime = time;
    n->endTime = time;

    notes.push_back (right);
    sortNotes();
}

void GraphModel::makeNotesFromTrack (const PitchTrack& track, const ScaleQuantizer& scale,
                                     double minDurationSec, float splitSemitones)
{
    const auto segments = segmentTrack (track, minDurationSec, splitSemitones);

    for (const auto& seg : segments)
    {
        const auto t = scale.findTarget (seg.medianMidi);
        const float pitch = t.valid ? t.midiNote : std::round (seg.medianMidi);
        addNote (seg.startTime, seg.endTime, pitch);
    }
}

void GraphModel::makeCurvesFromTrack (const PitchTrack& track, const ScaleQuantizer& scale,
                                      double minDurationSec, float splitSemitones)
{
    const auto segments = segmentTrack (track, minDurationSec, splitSemitones);
    const auto& frames = track.getFrames();

    for (const auto& seg : segments)
    {
        const auto t = scale.findTarget (seg.medianMidi);
        const float pitch = t.valid ? t.midiNote : std::round (seg.medianMidi);
        const int id = addNote (seg.startTime, seg.endTime, pitch);

        auto* note = getNote (id);
        if (note == nullptr)
            continue;

        // Sample the performance into the note's curve so its shape - the
        // scoop in, the drift, the vibrato - is preserved while its centre is
        // pulled to pitch.
        const double len = std::max (1.0e-6, seg.endTime - seg.startTime);
        const int first = track.indexAtOrAfter (seg.startTime);

        note->curve.reserve ((size_t) NoteSpan::curveResolution);

        for (int i = 0; i < NoteSpan::curveResolution; ++i)
        {
            const double u = (double) i / (double) (NoteSpan::curveResolution - 1);
            const double wanted = seg.startTime + u * len;

            float value = seg.medianMidi;
            for (int k = first; k < (int) frames.size(); ++k)
            {
                if (frames[(size_t) k].timeSeconds >= wanted)
                {
                    if (frames[(size_t) k].voiced)
                        value = frames[(size_t) k].detectedMidi;

                    break;
                }
            }

            note->curve.push_back ({ (float) u, value - pitch });
        }
    }
}

// ---------------------------------------------------------------------------

void GraphModel::beginTransaction()
{
    undoStack.push_back (notes);
    if (undoStack.size() > maxUndoLevels)
        undoStack.erase (undoStack.begin());

    redoStack.clear();
}

void GraphModel::undo()
{
    if (undoStack.empty())
        return;

    redoStack.push_back (notes);
    notes = undoStack.back();
    undoStack.pop_back();
}

void GraphModel::redo()
{
    if (redoStack.empty())
        return;

    undoStack.push_back (notes);
    notes = redoStack.back();
    redoStack.pop_back();
}

// ---------------------------------------------------------------------------

juce::ValueTree GraphModel::toValueTree() const
{
    juce::ValueTree tree ("GRAPH");

    for (const auto& n : notes)
    {
        juce::ValueTree v ("NOTE");
        v.setProperty ("start",   n.startTime, nullptr);
        v.setProperty ("end",     n.endTime, nullptr);
        v.setProperty ("pitch",   n.pitchMidi, nullptr);
        v.setProperty ("retune",  n.retuneMs, nullptr);
        v.setProperty ("vib",     n.vibratoScale, nullptr);

        if (! n.curve.empty())
        {
            juce::MemoryBlock mb (n.curve.size() * sizeof (CurvePoint));
            std::memcpy (mb.getData(), n.curve.data(), mb.getSize());
            v.setProperty ("curve", mb.toBase64Encoding(), nullptr);
        }

        tree.appendChild (v, nullptr);
    }

    return tree;
}

void GraphModel::fromValueTree (const juce::ValueTree& tree)
{
    notes.clear();
    nextId = 1;

    if (! tree.hasType ("GRAPH"))
        return;

    for (const auto& v : tree)
    {
        if (! v.hasType ("NOTE"))
            continue;

        GraphNote n;
        n.id           = nextId++;
        n.startTime    = (double) v.getProperty ("start", 0.0);
        n.endTime      = (double) v.getProperty ("end", 0.0);
        n.pitchMidi    = (float)  v.getProperty ("pitch", 60.0f);
        n.retuneMs     = (float)  v.getProperty ("retune", -1.0f);
        n.vibratoScale = (float)  v.getProperty ("vib", -1.0f);

        const juce::String encoded = v.getProperty ("curve", juce::String()).toString();
        if (encoded.isNotEmpty())
        {
            juce::MemoryBlock mb;
            if (mb.fromBase64Encoding (encoded) && (mb.getSize() % sizeof (CurvePoint)) == 0)
            {
                const size_t count = mb.getSize() / sizeof (CurvePoint);
                n.curve.resize (count);
                std::memcpy (n.curve.data(), mb.getData(), mb.getSize());
            }
        }

        if (n.endTime > n.startTime)
            notes.push_back (n);
    }

    sortNotes();
    commit();
}

} // namespace helix
