#pragma once

#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <memory>
#include <array>

namespace helix
{

/** Voice/instrument range presets. These set the search bounds for the
    detector, which is the single biggest lever on octave-error rate: a soprano
    tracker that never looks below 200 Hz cannot halve-octave onto a rumble.

    Generic gives that protection up in exchange for needing no setup: it
    searches every range at once, and the Viterbi tracker's time context has to
    do the octave work alone. It also inherits the lowest range's grain size,
    and therefore the highest latency of any type.
*/
enum class InputType
{
    soprano = 0,     // ~C4 - C6
    altoTenor,       // ~G2 - G5
    lowMale,         // ~E2 - E4
    instrument,      // wide
    bassInstrument,  // ~E1 - E3
    generic          // all of the above, for when the source is unknown
};

// Appended rather than inserted: hosts persist the choice by index, so every
// existing entry must keep its number or old sessions would change voice type.
inline constexpr int numInputTypes = 6;

struct FrequencyRange { float minHz, maxHz; };
FrequencyRange rangeForInputType (InputType t);

/** Monophonic f0 estimator: YIN (de Cheveigne & Kawahara 2002) with an
    FFT-accelerated difference function and parabolic peak interpolation.

    YIN over plain autocorrelation matters here because the cumulative-mean
    normalisation in step 3 is what suppresses the octave-down errors that make
    a naive ACF tracker unusable on real vocals.

    Every configuration the plugin can switch to is built during prepare(), so
    changing input type mid-stream never allocates on the audio thread.
*/
class PitchDetector
{
public:
    /** One local minimum of the normalised difference function - a plausible
        period. Octave errors show up here as a second candidate at 2x or 1/2x
        the true period with a nearly identical cost, which is exactly the
        ambiguity the stabiliser resolves using time context. */
    struct Candidate
    {
        float periodSamples = 0.0f;
        float frequencyHz   = 0.0f;
        float cost          = 1.0f;   // cmnd value; lower is more periodic
    };

    static constexpr int maxCandidates = 6;

    struct Result
    {
        float frequencyHz = 0.0f;
        float midiNote    = 0.0f;   // fractional, 69 = A440
        float confidence  = 0.0f;   // 1 - aperiodicity, in [0,1]
        bool  voiced      = false;

        float rms         = 0.0f;
        int   numCandidates = 0;
        Candidate candidates[maxCandidates] { };
    };

    void prepare (double sampleRate);
    void setInputType (InputType type) noexcept;

    /** Detector permissiveness. Low values only accept strongly periodic
        frames (clean studio vocal); high values track breathier / noisier
        material at the cost of more spurious estimates. */
    void setTracking (float amount01) noexcept { tracking = juce::jlimit (0.0f, 1.0f, amount01); }

    /** Samples the current configuration needs. Varies with input type. */
    int getFrameSize() const noexcept { return configs[(size_t) current].frameSize; }

    /** Largest frame any configuration will ask for - size caller buffers to this. */
    int getMaxFrameSize() const noexcept { return maxFrameSize; }

    /** @param frame  pointer to the newest getFrameSize() samples, oldest first. */
    Result process (const float* frame) noexcept;

    void reset() noexcept;

private:
    struct Config
    {
        int window    = 1024;
        int tauMin    = 2;
        int tauMax    = 1024;
        int frameSize = 2048;
        int fftSize   = 4096;
        juce::dsp::FFT* fft = nullptr;
    };

    void computeDifferenceFunction (const float* frame, const Config& c) noexcept;
    void cumulativeMeanNormalise (const Config& c) noexcept;
    int  absoluteThreshold (const Config& c, float threshold) const noexcept;
    float parabolicRefine (const Config& c, int tauEstimate) const noexcept;
    void collectCandidates (const Config& c, float threshold, int tauYin, Result& out) const noexcept;

    double fs = 44100.0;
    int current = (int) InputType::altoTenor;
    int maxFrameSize = 2048;
    float tracking = 0.5f;

    std::array<Config, (size_t) numInputTypes> configs { };
    std::vector<std::unique_ptr<juce::dsp::FFT>> fftPool;

    using Cplx = juce::dsp::Complex<float>;
    std::vector<Cplx> specA, specB, specOut;
    std::vector<float> corr, power, diff, cmnd;

    // Single-frame outlier rejection across hops. A frame that lands an octave
    // off is common on consonants; two in a row is not.
    float history[2] { 0.0f, 0.0f };
    int   historyFill = 0;
};

} // namespace helix
