#include "VibratoGenerator.h"
#include <cmath>

namespace helix
{

void VibratoGenerator::prepare (double sampleRate, int hopSizeSamples)
{
    hopSeconds = (float) ((double) hopSizeSamples / sampleRate);
    reset();
}

void VibratoGenerator::reset() noexcept
{
    phase = envelope = sinceOnset = 0.0f;
    rateDrift = depthDrift = 0.0f;
    active = false;
}

void VibratoGenerator::noteOn() noexcept
{
    sinceOnset = 0.0f;
    phase = 0.0f;
    envelope = 0.0f;
    active = true;
}

void VibratoGenerator::noteOff() noexcept
{
    active = false;
}

float VibratoGenerator::shapeValue (VibratoShape s, float ph) const noexcept
{
    switch (s)
    {
        case VibratoShape::sine:     return std::sin (ph * juce::MathConstants<float>::twoPi);
        case VibratoShape::triangle: return 4.0f * std::abs (ph - 0.5f) - 1.0f;
        case VibratoShape::square:   return ph < 0.5f ? 1.0f : -1.0f;
        case VibratoShape::sawUp:    return 2.0f * ph - 1.0f;
        case VibratoShape::sawDown:  return 1.0f - 2.0f * ph;
    }
    return 0.0f;
}

VibratoGenerator::Output VibratoGenerator::process (const Params& p) noexcept
{
    Output out;

    if (! active)
    {
        envelope *= 0.9f;
        if (envelope < 1.0e-4f)
            return out;
    }
    else
    {
        sinceOnset += hopSeconds;

        const float delaySec = p.onsetDelayMs * 0.001f;
        const float riseSec  = juce::jmax (1.0e-3f, p.onsetRateMs * 0.001f);

        const float wanted = (sinceOnset <= delaySec)
                           ? 0.0f
                           : juce::jlimit (0.0f, 1.0f, (sinceOnset - delaySec) / riseSec);

        // Approach the target rather than jumping to it, so a short onset rate
        // still swells instead of clicking in.
        envelope += (wanted - envelope) * juce::jmin (1.0f, hopSeconds / 0.03f);
    }

    // Variation nudges rate and depth on a slow random walk. Fresh noise every
    // hop would sound like fizz; a walk sounds like a human not quite locking
    // to a metronome.
    if (p.variation > 1.0e-4f)
    {
        const float step = hopSeconds / 0.35f;
        rateDrift  += (rng.nextFloat() * 2.0f - 1.0f - rateDrift)  * step;
        depthDrift += (rng.nextFloat() * 2.0f - 1.0f - depthDrift) * step;
    }
    else
    {
        rateDrift = depthDrift = 0.0f;
    }

    const float rate  = juce::jmax (0.05f, p.rateHz * (1.0f + 0.35f * p.variation * rateDrift));
    const float depth = juce::jlimit (0.0f, 1.5f, 1.0f + 0.35f * p.variation * depthDrift);

    phase += rate * hopSeconds;
    while (phase >= 1.0f)
        phase -= 1.0f;

    const float lfo = shapeValue (p.shape, phase) * envelope * depth;

    out.pitchCents   = lfo * p.pitchCents;
    out.formantCents = lfo * p.formantCents;
    out.gain         = 1.0f + lfo * p.amplitude * 0.5f;   // +/- 6 dB at full depth

    return out;
}

} // namespace helix
