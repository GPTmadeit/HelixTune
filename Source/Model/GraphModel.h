#pragma once

#include "PitchTrack.h"
#include "../DSP/ScaleQuantizer.h"
#include <juce_data_structures/juce_data_structures.h>
#include <vector>
#include <atomic>
#include <array>

namespace helix
{

struct CurvePoint { float x = 0.0f; float y = 0.0f; };   // x normalised in note, y semitones

/** An editable note in the graph editor. Times are in seconds on the host
    timeline so the edits stay put when the tempo map is not linear. */
struct GraphNote
{
    int    id           = 0;
    double startTime    = 0.0;
    double endTime      = 0.0;
    float  pitchMidi    = 60.0f;
    float  retuneMs     = -1.0f;   // < 0 inherits the global Retune Speed
    float  vibratoScale = -1.0f;   // < 0 inherits the global vibrato amount
    bool   selected     = false;
    std::vector<CurvePoint> curve; // empty = flat note at pitchMidi

    double length() const noexcept { return endTime - startTime; }
    bool contains (double t) const noexcept { return t >= startTime && t < endTime; }
};

// ---------------------------------------------------------------------------
// Audio-thread snapshot
// ---------------------------------------------------------------------------

/** Flattened note, with any curve resampled to a fixed grid so the audio
    thread never touches a heap allocation or a std::vector. */
struct NoteSpan
{
    static constexpr int curveResolution = 32;

    double start = 0.0, end = 0.0;
    float  pitch = 60.0f;
    float  retuneMs = -1.0f;
    float  vibratoScale = -1.0f;
    bool   hasCurve = false;
    std::array<float, (size_t) curveResolution> curve { };

    float pitchAt (double t) const noexcept;
};

/** Double-buffered publication of the note list.

    The editor builds into the inactive buffer and flips one atomic index; the
    audio thread only ever reads the active buffer. No locks, no allocation,
    and a torn read is impossible because the buffer being written is never the
    one being read.
*/
class NoteSnapshot
{
public:
    static constexpr int maxNotes = 2048;

    struct Buffer
    {
        std::array<NoteSpan, (size_t) maxNotes> spans { };
        int count = 0;
    };

    /** Editor side. */
    void publish (const std::vector<GraphNote>& notes);

    /** Audio side. Returns nullptr when there is nothing to apply. */
    const NoteSpan* find (double timeSeconds) const noexcept;

    bool isEmpty() const noexcept { return buffers[(size_t) active.load (std::memory_order_acquire)].count == 0; }

private:
    std::array<Buffer, 2> buffers { };
    std::atomic<int> active { 0 };
};

// ---------------------------------------------------------------------------

class GraphModel
{
public:
    using NoteList = std::vector<GraphNote>;

    const NoteList& getNotes() const noexcept { return notes; }
    NoteList& getNotesForEdit() noexcept { return notes; }

    int  addNote (double start, double end, float pitch);
    void removeNote (int id);
    void removeSelected();
    void clear();

    GraphNote*       getNote (int id) noexcept;
    const GraphNote* getNote (int id) const noexcept;

    /** Topmost note under a point, or nullptr. */
    GraphNote* hitTest (double time, float pitch, float pitchTolerance = 0.5f) noexcept;

    void selectAll (bool shouldBeSelected);
    void setSelected (int id, bool shouldBeSelected, bool exclusive);
    int  getNumSelected() const noexcept;

    void moveSelected (double deltaSeconds, float deltaSemitones);
    void resizeSelected (double deltaStart, double deltaEnd);
    void nudgeSelectedCents (float cents);
    void snapSelectedToScale (const ScaleQuantizer& scale);
    void setSelectedRetune (float ms);
    void setSelectedVibrato (float scale);
    void flattenSelectedCurves();

    /** Scissors: splits the note containing @p time into two. */
    void splitNoteAt (int id, double time);

    /** Graph mode's "Make Notes" - turns the captured contour into flat notes
        at the nearest scale pitch. */
    void makeNotesFromTrack (const PitchTrack& track, const ScaleQuantizer& scale,
                             double minDurationSec = 0.06f, float splitSemitones = 0.65f);

    /** "Make Curve" - same segmentation, but each note keeps the shape of the
        original performance so only the centre pitch is corrected. */
    void makeCurvesFromTrack (const PitchTrack& track, const ScaleQuantizer& scale,
                              double minDurationSec = 0.06f, float splitSemitones = 0.65f);

    // undo -----------------------------------------------------------------
    void beginTransaction();     // snapshot before an edit
    void undo();
    void redo();
    bool canUndo() const noexcept { return ! undoStack.empty(); }
    bool canRedo() const noexcept { return ! redoStack.empty(); }

    // persistence ----------------------------------------------------------
    juce::ValueTree toValueTree() const;
    void fromValueTree (const juce::ValueTree& tree);

    /** Republish to the audio thread. Call after any mutation. */
    void commit() { snapshot.publish (notes); }
    NoteSnapshot& getSnapshot() noexcept { return snapshot; }

private:
    void sortNotes();

    NoteList notes;
    int nextId = 1;
    NoteSnapshot snapshot;

    std::vector<NoteList> undoStack, redoStack;
    static constexpr size_t maxUndoLevels = 64;
};

} // namespace helix
