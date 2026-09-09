#pragma once

#include <juce_core/juce_core.h>
#include <cstdint>
#include <array>

namespace helix
{

/** Per-note editing, matching the three states a scale keyboard offers.
    - normal   : a legal correction target.
    - removed  : never a target; pitches near it snap to the next scale tone.
                 This is how you stop a passing tone being yanked onto a note
                 that is technically in key but wrong for the line.
    - bypassed : still recognised, but pitch near it is passed through
                 untouched. Useful for leaving one expressive note alone.
*/
enum class NoteState : uint8_t { normal = 0, removed, bypassed };

struct ScaleDefinition
{
    const char*  name;
    uint16_t     mask;         // bit s set => semitone s above the root is a degree
    const float* centOffsets;  // nullptr => 12-TET; else 12 offsets from equal temperament
};

/** All selectable scales, equal-tempered subsets first, then the historical
    temperaments where the deviation from 12-TET is the whole point. */
const ScaleDefinition* getScaleTable();
int getNumScales();

class ScaleQuantizer
{
public:
    struct Target
    {
        float midiNote = 0.0f;   // nearest legal pitch, temperament applied
        bool  valid    = false;
        bool  bypass   = false;  // nearest note is user-bypassed: do not correct
    };

    void setKey (int rootPitchClass) noexcept   { root = ((rootPitchClass % 12) + 12) % 12; }
    void setScale (int index) noexcept;
    void setNoteState (int absolutePitchClass, NoteState s) noexcept;
    NoteState getNoteState (int absolutePitchClass) const noexcept;
    void clearNoteStates() noexcept;

    int getKey() const noexcept   { return root; }
    int getScale() const noexcept { return scaleIndex; }

    /** Nearest legal target to a fractional MIDI note. */
    Target findTarget (float inputMidiNote) const noexcept;

    /** Moves a note by @p degrees steps *along the scale*, not by a fixed
        interval. A third above the tonic in a major scale is four semitones;
        a third above the second degree is three. Harmony that ignores this
        sounds wrong on every other note. */
    float transposeByScaleDegrees (float midiNote, int degrees) const noexcept;

    /** Ordered scale tones in one octave, as semitone offsets from the root.
        Returns how many were written (removed notes are excluded). */
    int getScaleTones (int* destination, int capacity) const noexcept;

    /** True if the scale currently admits at least one target. */
    bool hasAnyTarget() const noexcept;

private:
    int root = 0;                    // 0 = C
    int scaleIndex = 0;              // index into the scale table
    std::array<NoteState, 12> noteStates { };
};

} // namespace helix
