#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <atomic>
#include <array>

namespace helix
{

/** One analysis hop, as handed from the audio thread to the editor. */
struct PitchFrame
{
    double timeSeconds  = 0.0;   // host timeline position of this hop
    double ppq          = -1.0;  // musical position, or < 0 when the host has none
    float  detectedMidi = 0.0f;
    float  targetMidi   = 0.0f;
    float  outputMidi   = 0.0f;
    float  confidence   = 0.0f;
    float  rms          = 0.0f;
    float  consonant    = 0.0f;  // 1 = unpitched consonant, correction held off
    bool   voiced       = false;
};

/** Single-producer / single-consumer handoff.

    The audio thread must never allocate, lock, or block, so analysis data goes
    through a fixed ring that the editor drains on its timer. Overrun drops the
    oldest frames rather than stalling the audio thread - a gap in a display is
    survivable, a dropout is not.
*/
class PitchFifo
{
public:
    static constexpr int capacity = 1 << 14;   // ~95 s of hops at 172 Hz

    void push (const PitchFrame& f) noexcept
    {
        const int w = writeIndex.load (std::memory_order_relaxed);
        buffer[(size_t) (w & (capacity - 1))] = f;
        writeIndex.store (w + 1, std::memory_order_release);
    }

    /** Pops everything pending into @p dest. Returns how many were dropped. */
    int drain (std::vector<PitchFrame>& dest) noexcept
    {
        const int w = writeIndex.load (std::memory_order_acquire);
        int r = readIndex;
        int dropped = 0;

        if (w - r > capacity)
        {
            dropped = (w - r) - capacity;
            r = w - capacity;
        }

        dest.reserve (dest.size() + (size_t) (w - r));
        for (; r < w; ++r)
            dest.push_back (buffer[(size_t) (r & (capacity - 1))]);

        readIndex = w;
        return dropped;
    }

    void reset() noexcept
    {
        readIndex = writeIndex.load (std::memory_order_acquire);
    }

private:
    std::array<PitchFrame, (size_t) capacity> buffer { };
    std::atomic<int> writeIndex { 0 };
    int readIndex = 0;
};

/** Editor-owned capture of the detected pitch contour over the whole timeline.
    This is what Graph mode draws as the input curve and what "Make Notes"
    is derived from. */
class PitchTrack
{
public:
    void clear();
    void append (const PitchFrame& f);
    void appendAll (const std::vector<PitchFrame>& frames);

    const std::vector<PitchFrame>& getFrames() const noexcept { return frames; }
    bool isEmpty() const noexcept { return frames.empty(); }

    double getStartTime() const noexcept { return frames.empty() ? 0.0 : frames.front().timeSeconds; }
    double getEndTime()   const noexcept { return frames.empty() ? 0.0 : frames.back().timeSeconds; }

    /** Index of the first frame at or after @p time. Frames are kept sorted by
        time, so this is a binary search. */
    int indexAtOrAfter (double time) const noexcept;

    /** Lowest and highest voiced pitch, for auto-ranging the piano roll. */
    bool getVoicedPitchRange (float& lowMidi, float& highMidi) const noexcept;

private:
    std::vector<PitchFrame> frames;
};

/** Contiguous run of voiced frames that "Make Notes" turns into one note. */
struct DetectedSegment
{
    double startTime = 0.0;
    double endTime   = 0.0;
    float  medianMidi = 0.0f;
};

/** Splits a pitch track into note-sized segments.
    @param minDurationSec   runs shorter than this are discarded as consonants
    @param splitSemitones   a jump this large starts a new note */
std::vector<DetectedSegment> segmentTrack (const PitchTrack& track,
                                           double minDurationSec = 0.06,
                                           float splitSemitones = 0.65f);

} // namespace helix
