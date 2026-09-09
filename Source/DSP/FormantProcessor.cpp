#include "FormantProcessor.h"
#include <cmath>
#include <algorithm>

namespace helix
{

void FormantProcessor::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    fs = sampleRate;
    numCh = juce::jmax (1, numChannels);
    fftSize = 512;
    fft = juce::dsp::FFT (9);

    windowed.assign ((size_t) analysisLen, 0.0f);
    hann.assign ((size_t) analysisLen, 0.0f);

    for (int i = 0; i < analysisLen; ++i)
        hann[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi
                                                          * i / (analysisLen - 1)));

    specIn.assign ((size_t) fftSize, Cplx {});
    specOut.assign ((size_t) fftSize, Cplx {});
    magA.assign ((size_t) fftSize / 2 + 1, 1.0f);
    ratioCurve.assign ((size_t) fftSize / 2 + 1, 1.0f);

    taps.assign ((size_t) firLength, 0.0f);
    targetTaps.assign ((size_t) firLength, 0.0f);
    taps[(size_t) (firLength / 2)] = 1.0f;
    targetTaps[(size_t) (firLength / 2)] = 1.0f;

    delayLine.assign ((size_t) numCh, std::vector<float> ((size_t) firLength, 0.0f));
    delayPos.assign ((size_t) numCh, 0);

    juce::ignoreUnused (maxBlockSize);
    reset();
}

void FormantProcessor::reset() noexcept
{
    for (auto& d : delayLine)
        std::fill (d.begin(), d.end(), 0.0f);

    std::fill (delayPos.begin(), delayPos.end(), 0);
    std::fill (magA.begin(), magA.end(), 1.0f);

    std::fill (taps.begin(), taps.end(), 0.0f);
    std::fill (targetTaps.begin(), targetTaps.end(), 0.0f);
    taps[(size_t) (firLength / 2)] = 1.0f;
    targetTaps[(size_t) (firLength / 2)] = 1.0f;

    bypassed = true;
    lastRatio = 1.0f;
}

void FormantProcessor::levinsonDurbin() noexcept
{
    float a[lpcOrder + 1] { };
    a[0] = 1.0f;

    double err = autocorr[0];
    if (err <= 1.0e-12)
    {
        std::fill (std::begin (lpc), std::end (lpc), 0.0f);
        lpc[0] = 1.0f;
        return;
    }

    for (int i = 1; i <= lpcOrder; ++i)
    {
        double acc = autocorr[i];
        for (int j = 1; j < i; ++j)
            acc -= (double) a[j] * autocorr[i - j];

        const double k = acc / err;

        float prev[lpcOrder + 1];
        std::copy (a, a + lpcOrder + 1, prev);

        a[i] = (float) k;
        for (int j = 1; j < i; ++j)
            a[j] = prev[j] - (float) k * prev[i - j];

        err *= (1.0 - k * k);

        // A reflection coefficient outside the unit circle means the recursion
        // has gone unstable on a degenerate frame; keep what we had.
        if (err <= 1.0e-12 || std::abs (k) >= 1.0)
            break;
    }

    lpc[0] = 1.0f;
    for (int i = 1; i <= lpcOrder; ++i)
        lpc[i] = -a[i];        // store A(z) coefficients directly
}

void FormantProcessor::computeEnvelope() noexcept
{
    std::fill (specIn.begin(), specIn.end(), Cplx {});

    for (int i = 0; i <= lpcOrder; ++i)
        specIn[(size_t) i] = Cplx (lpc[(size_t) i], 0.0f);

    fft.perform (specIn.data(), specOut.data(), false);

    for (size_t k = 0; k < magA.size(); ++k)
        magA[k] = juce::jmax (1.0e-5f, std::abs (specOut[k]));
}

void FormantProcessor::analyse (const float* frame, int numSamples) noexcept
{
    const int n = juce::jmin (numSamples, analysisLen);
    const int offset = numSamples - n;

    for (int i = 0; i < n; ++i)
        windowed[(size_t) i] = frame[offset + i] * hann[(size_t) i];

    for (int i = n; i < analysisLen; ++i)
        windowed[(size_t) i] = 0.0f;

    for (int lag = 0; lag <= lpcOrder; ++lag)
    {
        double sum = 0.0;
        for (int i = lag; i < n; ++i)
            sum += (double) windowed[(size_t) i] * windowed[(size_t) (i - lag)];

        autocorr[lag] = sum;
    }

    if (autocorr[0] < 1.0e-9)
        return;

    // Ridge plus lag windowing: widens the poles slightly, which keeps the
    // envelope smooth and the recursion well conditioned on near-periodic
    // frames where the autocorrelation matrix is close to singular.
    autocorr[0] *= 1.0001;
    for (int lag = 1; lag <= lpcOrder; ++lag)
    {
        const double f = 60.0 * lag / fs;
        autocorr[lag] *= std::exp (-0.5 * (2.0 * juce::MathConstants<double>::pi * f)
                                        * (2.0 * juce::MathConstants<double>::pi * f));
    }

    levinsonDurbin();
    computeEnvelope();
    buildFilter (lastRatio);
}

void FormantProcessor::setRatio (float ratio) noexcept
{
    lastRatio = juce::jlimit (0.5f, 2.0f, ratio);
    bypassed = std::abs (lastRatio - 1.0f) < 0.005f;
}

void FormantProcessor::buildFilter (float ratio) noexcept
{
    if (std::abs (ratio - 1.0f) < 0.005f)
    {
        std::fill (targetTaps.begin(), targetTaps.end(), 0.0f);
        targetTaps[(size_t) (firLength / 2)] = 1.0f;
        return;
    }

    const int half = fftSize / 2;

    // R(f) = |A(f)| / |A(f / ratio)|.
    for (int k = 0; k <= half; ++k)
    {
        const float src = (float) k / ratio;
        float ref;

        if (src >= (float) half)
        {
            ref = magA[(size_t) half];
        }
        else
        {
            const int i = (int) src;
            const float f = src - (float) i;
            ref = magA[(size_t) i] + f * (magA[(size_t) (i + 1)] - magA[(size_t) i]);
        }

        // Clamped: a deep null in either envelope would otherwise ask for
        // enormous gain and turn one resonance into a whistle.
        ratioCurve[(size_t) k] = juce::jlimit (0.125f, 8.0f, magA[(size_t) k] / juce::jmax (1.0e-5f, ref));
    }

    // Light smoothing; formant envelopes are broad and the correction should be
    // too, otherwise the short FIR cannot represent it and rings instead.
    for (int pass = 0; pass < 2; ++pass)
    {
        float prev = ratioCurve[0];
        for (int k = 1; k < half; ++k)
        {
            const float cur = ratioCurve[(size_t) k];
            ratioCurve[(size_t) k] = 0.25f * prev + 0.5f * cur + 0.25f * ratioCurve[(size_t) (k + 1)];
            prev = cur;
        }
    }

    // Real, even spectrum -> real, even impulse response.
    std::fill (specIn.begin(), specIn.end(), Cplx {});
    for (int k = 0; k <= half; ++k)
    {
        specIn[(size_t) k] = Cplx (ratioCurve[(size_t) k], 0.0f);
        if (k > 0 && k < half)
            specIn[(size_t) (fftSize - k)] = Cplx (ratioCurve[(size_t) k], 0.0f);
    }

    fft.perform (specIn.data(), specOut.data(), true);

    const int centre = firLength / 2;
    double sum = 0.0;

    for (int i = 0; i < firLength; ++i)
    {
        // Wrap the zero-phase response around zero, then window it.
        const int idx = (i - centre + fftSize) % fftSize;
        const float w = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi
                                                 * (float) i / (float) (firLength - 1));

        targetTaps[(size_t) i] = specOut[(size_t) idx].real() * w;
        sum += targetTaps[(size_t) i];
    }

    // Anchor DC gain to the intended value, which also absorbs whatever
    // scaling convention the inverse transform uses.
    if (std::abs (sum) > 1.0e-9)
    {
        const float scale = ratioCurve[0] / (float) sum;
        for (auto& t : targetTaps)
            t *= scale;
    }
}

void FormantProcessor::process (float* data, int numSamples, int channel) noexcept
{
    if (channel >= numCh)
        return;

    // Ease toward the new filter. Swapping taps outright on every hop clicks.
    // Only once per block, not once per channel, or stereo would smooth twice
    // as fast as mono.
    if (channel == 0)
        for (size_t i = 0; i < taps.size(); ++i)
            taps[i] += (targetTaps[i] - taps[i]) * 0.35f;

    // Deliberately no bypass shortcut: at ratio 1 the taps converge to a unit
    // impulse at the centre, which is a pure delay of firLength/2. Skipping the
    // convolution instead would drop that delay and shift the audio in time the
    // moment the ratio crossed 1.0.
    auto& line = delayLine[(size_t) channel];
    int pos = delayPos[(size_t) channel];

    for (int n = 0; n < numSamples; ++n)
    {
        line[(size_t) pos] = data[n];

        float acc = 0.0f;
        int idx = pos;

        for (int i = 0; i < firLength; ++i)
        {
            acc += taps[(size_t) i] * line[(size_t) idx];
            if (--idx < 0)
                idx = firLength - 1;
        }

        data[n] = acc;

        if (++pos >= firLength)
            pos = 0;
    }

    delayPos[(size_t) channel] = pos;
}

} // namespace helix
