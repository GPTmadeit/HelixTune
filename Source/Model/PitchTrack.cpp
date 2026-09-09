#include "PitchTrack.h"
#include <algorithm>
#include <cmath>

namespace helix
{

void PitchTrack::clear()
{
    frames.clear();
}

int PitchTrack::indexAtOrAfter (double time) const noexcept
{
    const auto it = std::lower_bound (frames.begin(), frames.end(), time,
                                      [] (const PitchFrame& f, double t) { return f.timeSeconds < t; });
    return (int) std::distance (frames.begin(), it);
}

void PitchTrack::append (const PitchFrame& f)
{
    // A backwards jump means the transport looped or the user scrubbed. Drop
    // the stale capture ahead of the new position so the display shows the
    // latest pass rather than two takes drawn on top of each other.
    if (! frames.empty() && f.timeSeconds < frames.back().timeSeconds - 1.0e-6)
    {
        const int cut = indexAtOrAfter (f.timeSeconds);
        frames.erase (frames.begin() + cut, frames.end());
    }

    frames.push_back (f);
}

void PitchTrack::appendAll (const std::vector<PitchFrame>& incoming)
{
    for (const auto& f : incoming)
        append (f);
}

bool PitchTrack::getVoicedPitchRange (float& lowMidi, float& highMidi) const noexcept
{
    bool any = false;
    float lo = 127.0f, hi = 0.0f;

    for (const auto& f : frames)
    {
        if (! f.voiced)
            continue;

        lo = std::min (lo, f.detectedMidi);
        hi = std::max (hi, f.detectedMidi);
        any = true;
    }

    if (! any)
        return false;

    lowMidi = lo;
    highMidi = hi;
    return true;
}

std::vector<DetectedSegment> segmentTrack (const PitchTrack& track,
                                           double minDurationSec,
                                           float splitSemitones)
{
    std::vector<DetectedSegment> out;
    const auto& frames = track.getFrames();

    std::vector<float> pitches;
    int runStart = -1;
    double runSum = 0.0;

    auto closeRun = [&] (int endExclusive)
    {
        if (runStart < 0 || pitches.empty())
        {
            runStart = -1;
            runSum = 0.0;
            pitches.clear();
            return;
        }

        const double start = frames[(size_t) runStart].timeSeconds;
        const double end   = frames[(size_t) (endExclusive - 1)].timeSeconds;

        if (end - start >= minDurationSec)
        {
            // Median, not mean: a single frame of octave error at the note
            // boundary should not drag the whole note off its pitch.
            const size_t mid = pitches.size() / 2;
            std::nth_element (pitches.begin(), pitches.begin() + (long) mid, pitches.end());

            DetectedSegment seg;
            seg.startTime  = start;
            seg.endTime    = end;
            seg.medianMidi = pitches[mid];
            out.push_back (seg);
        }

        runStart = -1;
        runSum = 0.0;
        pitches.clear();
    };

    for (int i = 0; i < (int) frames.size(); ++i)
    {
        const auto& f = frames[(size_t) i];

        if (! f.voiced)
        {
            closeRun (i);
            continue;
        }

        if (runStart < 0)
        {
            runStart = i;
            pitches.clear();
            pitches.push_back (f.detectedMidi);
            runSum = f.detectedMidi;
            continue;
        }

        // A jump away from the note so far starts a new note. The running mean
        // is the reference because it is cheap to maintain incrementally; the
        // note's final pitch is still taken as a median once the run closes.
        const float reference = (float) (runSum / (double) pitches.size());
        if (std::abs (f.detectedMidi - reference) > splitSemitones)
        {
            closeRun (i);
            runStart = i;
            pitches.push_back (f.detectedMidi);
            runSum = f.detectedMidi;
            continue;
        }

        pitches.push_back (f.detectedMidi);
        runSum += f.detectedMidi;
    }

    closeRun ((int) frames.size());
    return out;
}

} // namespace helix
