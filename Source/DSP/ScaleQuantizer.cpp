#include "ScaleQuantizer.h"
#include <cmath>
#include <limits>

namespace helix
{

// ---------------------------------------------------------------------------
// Historical temperaments, as cent deviations from 12-TET with the tonic at 0.
// These are the whole reason a "Just Intonation" setting sounds different from
// "Major": the note set is identical, only the tuning of each degree moves.
// ---------------------------------------------------------------------------
static const float kJustMajor[12]   = { 0.0f,  11.73f,   3.91f,  15.64f, -13.69f,  -1.96f,
                                       -9.78f,  1.96f,  13.69f, -15.64f,  17.60f, -11.73f };

static const float kPythagorean[12] = { 0.0f,  13.69f,   3.91f,  -5.87f,   7.82f,  -1.96f,
                                       11.73f,  1.96f,  -7.82f,   5.87f,  -3.91f,   9.78f };

static const float kMeantone[12]    = { 0.0f, -23.95f,  -6.84f,  10.26f, -13.69f,   3.42f,
                                      -20.53f, -3.42f, -27.37f, -10.26f,   6.84f, -17.11f };

static const float kWerckmeister[12]= { 0.0f,  -9.78f,  -7.82f,  -5.87f,  -9.78f,  -1.96f,
                                      -11.73f, -3.91f,  -7.82f, -11.73f,  -3.91f,  -7.82f };

static const float kKirnberger[12]  = { 0.0f,  -9.78f,  -6.84f,  -5.87f, -13.69f,  -1.96f,
                                       -9.78f, -3.42f,  -7.82f, -10.27f,  -3.91f, -11.73f };

static const float kVallotti[12]    = { 0.0f,  -5.87f,  -3.91f,  -1.96f,  -7.82f,   0.0f,
                                       -7.82f, -1.96f,  -3.91f,  -5.87f,   0.0f,   -9.78f };

static const ScaleDefinition kScales[] =
{
    { "Chromatic",          0xFFF, nullptr },
    { "Major",              0xAB5, nullptr },
    { "Minor",              0x5AD, nullptr },
    { "Harmonic Minor",     0x9AD, nullptr },
    { "Melodic Minor",      0xAAD, nullptr },
    { "Dorian",             0x6AD, nullptr },
    { "Phrygian",           0x5AB, nullptr },
    { "Lydian",             0xAD5, nullptr },
    { "Mixolydian",         0x6B5, nullptr },
    { "Locrian",            0x56B, nullptr },
    { "Major Pentatonic",   0x295, nullptr },
    { "Minor Pentatonic",   0x4A9, nullptr },
    { "Blues",              0x4E9, nullptr },
    { "Whole Tone",         0x555, nullptr },
    { "Diminished W-H",     0xB6D, nullptr },
    { "Diminished H-W",     0x6DA, nullptr },
    { "Harmonic Major",     0x9B5, nullptr },
    { "Hungarian Minor",    0x9CD, nullptr },
    { "Hirajoshi",          0x18D, nullptr },
    { "Double Harmonic",    0x9B3, nullptr },
    { "Phrygian Dominant",  0x5B3, nullptr },

    { "Just Intonation",    0xFFF, kJustMajor },
    { "Pythagorean",        0xFFF, kPythagorean },
    { "Meantone 1/4",       0xFFF, kMeantone },
    { "Werckmeister III",   0xFFF, kWerckmeister },
    { "Kirnberger III",     0xFFF, kKirnberger },
    { "Vallotti",           0xFFF, kVallotti },
};

const ScaleDefinition* getScaleTable() { return kScales; }
int getNumScales() { return (int) (sizeof (kScales) / sizeof (kScales[0])); }

void ScaleQuantizer::setScale (int index) noexcept
{
    scaleIndex = juce::jlimit (0, getNumScales() - 1, index);
}

void ScaleQuantizer::setNoteState (int absolutePitchClass, NoteState s) noexcept
{
    const int pc = ((absolutePitchClass % 12) + 12) % 12;
    noteStates[(size_t) pc] = s;
}

NoteState ScaleQuantizer::getNoteState (int absolutePitchClass) const noexcept
{
    const int pc = ((absolutePitchClass % 12) + 12) % 12;
    return noteStates[(size_t) pc];
}

void ScaleQuantizer::clearNoteStates() noexcept
{
    noteStates.fill (NoteState::normal);
}

bool ScaleQuantizer::hasAnyTarget() const noexcept
{
    const auto& sc = kScales[(size_t) scaleIndex];

    for (int s = 0; s < 12; ++s)
        if ((sc.mask >> s) & 1)
            if (noteStates[(size_t) (((root + s) % 12))] != NoteState::removed)
                return true;

    return false;
}

ScaleQuantizer::Target ScaleQuantizer::findTarget (float inputMidiNote) const noexcept
{
    Target result;

    if (! std::isfinite (inputMidiNote))
        return result;

    const auto& sc = kScales[(size_t) scaleIndex];
    const int centreOctave = (int) std::floor ((inputMidiNote - (float) root) / 12.0f);

    float bestDistance = std::numeric_limits<float>::max();

    // Sweep the octave the input sits in plus its neighbours. Three octaves is
    // enough even when a scale has a six-semitone gap and the input lands in it.
    for (int k = centreOctave - 1; k <= centreOctave + 1; ++k)
    {
        for (int s = 0; s < 12; ++s)
        {
            if (! ((sc.mask >> s) & 1))
                continue;

            const int pc = ((root + s) % 12 + 12) % 12;
            if (noteStates[(size_t) pc] == NoteState::removed)
                continue;

            float candidate = (float) (root + 12 * k + s);
            if (sc.centOffsets != nullptr)
                candidate += sc.centOffsets[s] * 0.01f;

            const float distance = std::abs (candidate - inputMidiNote);
            if (distance < bestDistance)
            {
                bestDistance      = distance;
                result.midiNote   = candidate;
                result.valid      = true;
                result.bypass     = (noteStates[(size_t) pc] == NoteState::bypassed);
            }
        }
    }

    return result;
}

} // namespace helix
