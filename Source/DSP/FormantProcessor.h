#pragma once

#include <juce_dsp/juce_dsp.h>
#include <vector>

namespace helix
{

/** Spectral-envelope formant control, independent of pitch.

    The previous approach resampled the PSOLA grain content, which does move
    formants but also stretches the waveform inside the grain - at strong
    settings that reads as a warble rather than a different throat.

    This estimates the vocal tract response properly, by LPC analysis of the
    input, and applies a correction filter equal to the ratio between the
    envelope you want and the envelope you have. The pitch shifter is then left
    to do nothing but move pitch, and the two controls stop interfering.

        E(f)  = 1 / |A(f)|              measured envelope
        T(f)  = E(f / ratio)            envelope with the formants moved
        R(f)  = T(f) / E(f)             correction applied to the output

    Because R is derived as a ratio, the LPC gain term cancels and the filter
    is unity wherever the two envelopes agree.
*/
class FormantProcessor
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset() noexcept;

    /** Extra delay from the linear-phase correction filter. */
    int getLatencySamples() const noexcept { return firLength / 2; }

    /** Estimate the envelope from the newest input. Call once per analysis hop. */
    void analyse (const float* frame, int numSamples) noexcept;

    /** Rebuild the correction filter for a new formant ratio. */
    void setRatio (float ratio) noexcept;

    /** True when the ratio is close enough to 1 that filtering is pointless. */
    bool isBypassed() const noexcept { return bypassed; }

    void process (float* data, int numSamples, int channel) noexcept;

private:
    void levinsonDurbin() noexcept;
    void computeEnvelope() noexcept;
    void buildFilter (float ratio) noexcept;

    static constexpr int lpcOrder   = 32;
    static constexpr int firLength  = 65;    // odd, so the delay is a whole sample
    static constexpr int analysisLen = 1024;

    double fs = 44100.0;
    int numCh = 2;
    bool bypassed = true;
    float lastRatio = 1.0f;

    juce::dsp::FFT fft { 9 };
    int fftSize = 512;

    std::vector<float> windowed;             // Hann-windowed analysis frame
    std::vector<float> hann;
    double autocorr[lpcOrder + 1] { };
    float  lpc[lpcOrder + 1] { };            // A(z) = 1 - sum a_k z^-k

    using Cplx = juce::dsp::Complex<float>;
    std::vector<Cplx> specIn, specOut;
    std::vector<float> magA;                 // |A(f)| on the FFT grid
    std::vector<float> ratioCurve;

    std::vector<float> taps, targetTaps;
    std::vector<std::vector<float>> delayLine;
    std::vector<int> delayPos;
};

} // namespace helix
