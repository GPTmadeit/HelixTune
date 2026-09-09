#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <array>

namespace helix
{

/** Automatic key and mode detection from the sung pitch contour.

    Builds a pitch-class histogram weighted by how long and how confidently
    each note was held, then correlates it against the Krumhansl-Schmuckler
    key profiles - perceptual weightings derived from listener experiments,
    where the tonic and dominant dominate and the leading tone is rare.

    Working from detected f0 rather than a spectral chroma is both cheaper and
    more accurate here: the input is monophonic by assumption, so there are no
    harmonics from other instruments to confuse the histogram.
*/
class KeyDetector
{
public:
    struct Result
    {
        int   rootPitchClass = 0;    // 0 = C
        bool  minor = false;
        float confidence = 0.0f;     // margin over the runner-up, 0..1
        bool  valid = false;
    };

    void prepare (double sampleRate, int hopSizeSamples);
    void reset() noexcept;

    /** Called once per analysis hop from the audio thread. */
    void push (float midiNote, bool voiced, float weight) noexcept;

    /** Latest published estimate. Safe to call from the editor. */
    Result getEstimate() const noexcept;

    /** Normalised pitch-class histogram, for display. */
    float getChroma (int pitchClass) const noexcept;

private:
    void recompute() noexcept;

    float decayPerHop = 0.9999f;
    int   hopsUntilRecompute = 0;
    int   recomputeInterval = 40;

    std::array<float, 12> chroma { };
    float totalWeight = 0.0f;

    std::atomic<int>   pubRoot { 0 };
    std::atomic<bool>  pubMinor { false };
    std::atomic<float> pubConfidence { 0.0f };
    std::atomic<bool>  pubValid { false };
    std::array<std::atomic<float>, 12> pubChroma { };
};

} // namespace helix
