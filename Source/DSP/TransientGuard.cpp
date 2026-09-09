#include "TransientGuard.h"
#include <cmath>

namespace helix
{

void TransientGuard::prepare (double sampleRate, int hopSizeSamples)
{
    const float hopSeconds = (float) ((double) hopSizeSamples / sampleRate);

    // Asymmetric by design: catch the consonant immediately, then let go
    // slowly so the guard does not chatter on and off across a "sh".
    attack  = 1.0f - std::exp (-hopSeconds / 0.004f);
    release = 1.0f - std::exp (-hopSeconds / 0.070f);

    reset();
}

void TransientGuard::reset() noexcept
{
    smoothed = 0.0f;
}

float TransientGuard::process (const float* frame, int numSamples, float periodicity) noexcept
{
    if (sensitivity <= 1.0e-4f || numSamples < 2)
    {
        smoothed += (0.0f - smoothed) * release;
        return smoothed;
    }

    double total = 0.0, highFreq = 0.0;
    int crossings = 0;
    float prev = frame[0];

    for (int i = 1; i < numSamples; ++i)
    {
        const float x = frame[i];
        const float d = x - prev;      // first difference: a crude high-pass

        total += (double) x * x;
        highFreq += (double) d * d;

        if ((x >= 0.0f) != (prev >= 0.0f))
            ++crossings;

        prev = x;
    }

    if (total < 1.0e-10)
    {
        smoothed += (0.0f - smoothed) * release;
        return smoothed;
    }

    // The difference operator has a gain of 2 at Nyquist, so the ratio tops
    // out near 4 for pure high-frequency content.
    const float hfRatio = juce::jlimit (0.0f, 1.0f, (float) (highFreq / (total * 4.0)));
    const float zcr = juce::jlimit (0.0f, 1.0f, (float) crossings / (float) numSamples * 4.0f);

    // A vowel can be bright, and a quiet passage can be aperiodic; it takes
    // both together to mean "consonant".
    const float spectral = 0.5f * hfRatio + 0.5f * zcr;
    float score = spectral * (1.0f - juce::jlimit (0.0f, 1.0f, periodicity));

    score = juce::jlimit (0.0f, 1.0f, (score - 0.12f) * 3.0f) * sensitivity;

    smoothed += (score - smoothed) * (score > smoothed ? attack : release);
    return smoothed;
}

} // namespace helix
