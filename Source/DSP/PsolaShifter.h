#pragma once

#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <cstdint>

namespace helix
{

/** Time-domain PSOLA pitch shifter.

    Grains of two periods are lifted from the input at pitch-synchronous marks
    and overlap-added at a different spacing. Because the grain *content* is
    never stretched, the spectral envelope - the formants - survives untouched;
    only the pulse rate changes. That is why this sounds like a person singing a
    different note rather than a sped-up tape, and it is why every classic
    hardware pitch corrector works this way.

    Formant motion is then reintroduced deliberately, by resampling the grain
    content itself (see formantRatio), which is how throat modelling and the
    "formant correction off" chipmunk behaviour are produced.
*/
class PsolaShifter
{
public:
    /** @param lowestSupportedHz  size buffers for the lowest pitch the plugin
                                  will ever be asked to track, so that later
                                  range changes never reallocate. */
    void prepare (double sampleRate, float lowestSupportedHz, int maxBlockSize);

    /** Narrows the working range. Recomputes grain size and latency only - no
        allocation - so the input type can change while audio is running. */
    void setMinFrequency (float hz) noexcept;

    /** What to emit when the input has no pitch to shift.

        The lead voice must pass consonants through untouched, so it fades to
        the dry signal. A harmony voice must not: it has nothing to sing on a
        consonant, and passing the input through means every unvoiced moment
        becomes another delayed copy of the lead. Several of those summed is
        heard as gritty, flanged mush that worsens with each voice added. */
    void setPassDryWhenUnvoiced (bool shouldPassDry) noexcept { passDryWhenUnvoiced = shouldPassDry; }

    void reset() noexcept;

    /** Fixed algorithmic delay, in samples. PSOLA needs the input that a grain
        will read *after* the synthesis mark it lands on, so the delay scales
        with the longest period in the configured range. */
    int getLatencySamples() const noexcept { return latency; }

    /** @param pitchRatio     output f0 / input f0. >1 raises pitch.
        @param formantRatio   spectral envelope scaling. 1.0 leaves formants put.
        @param periodSamples  current input period (fs / f0).
        @param voiced         false crossfades to the delayed dry signal. */
    void process (const float* input, float* output, int numSamples,
                  float pitchRatio, float formantRatio,
                  float periodSamples, bool voiced) noexcept;

private:
    void  emitGrain (double analysisMark, double synthMark, float period, float formantRatio) noexcept;
    double refineMark (double predicted, float period) noexcept;
    float readInput (double absPos) const noexcept;
    void  copyFromRing (int64_t start, int count, float* dest) const noexcept;

    inline int  wrapIn  (int64_t p) const noexcept { return (int) (p & inMask); }
    inline int  wrapOut (int64_t p) const noexcept { return (int) (p & outMask); }

    double fs = 44100.0;
    int    latency        = 0;
    int    maxPeriod      = 512;   // current range
    int    capacityPeriod = 512;   // what the buffers were sized for

    std::vector<float> inBuf;
    int64_t inMask = 0;
    int64_t inWritePos = 0;      // total samples ever written

    std::vector<float> outAccum; // overlap-added signal
    std::vector<float> winAccum; // summed window, used to normalise the OLA
    int64_t outMask = 0;
    int64_t outClearedTo = 0;    // accumulators are zeroed lazily up to here

    double analysisPos = 0.0;    // absolute input position of the current mark
    double synthPos    = 0.0;    // absolute output position of the next grain
    bool   primed      = false;

    float lastPeriod = 200.0f;
    float voicedGain = 0.0f;     // smoothed dry/wet crossfade
    bool  passDryWhenUnvoiced = true;

    std::vector<float> window;   // Hann, indexed by normalised grain phase
    int windowSize = 0;

    // Correlation scores for one mark search, sized at prepare() so the
    // epoch refinement never allocates on the audio thread.
    std::vector<double> corrScores;

    // The mark search's samples, unwrapped from the ring so the correlation
    // runs over contiguous memory, plus the running energy of the candidates.
    // Also sized at prepare(). Every member is a value or a vector, so a
    // prepared shifter can be assigned to another of the same configuration
    // without allocating - the engine relies on that to split a linked
    // stereo pair on the audio thread.
    std::vector<float>  refScratch, candScratch;
    std::vector<double> energyPrefix;
};

} // namespace helix
