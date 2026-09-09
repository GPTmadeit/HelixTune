#pragma once

#include <juce_core/juce_core.h>

namespace helix
{

enum class VibratoShape { sine = 0, triangle, square, sawUp, sawDown };

/** Synthetic vibrato with a note-triggered onset envelope.

    Real singers do not start a note already vibrating - the modulation swells
    in after the pitch has settled. The delay/onset-rate pair reproduces that,
    and is the difference between vibrato that sounds performed and vibrato
    that sounds bolted on.
*/
class VibratoGenerator
{
public:
    struct Params
    {
        VibratoShape shape = VibratoShape::sine;
        float rateHz         = 5.5f;
        float variation      = 0.0f;   // 0..1, cycle-to-cycle rate/depth jitter
        float onsetDelayMs   = 300.0f;
        float onsetRateMs    = 500.0f;
        float pitchCents     = 0.0f;   // 0..100
        float amplitude      = 0.0f;   // 0..1, tremolo depth
        float formantCents   = 0.0f;   // 0..100, moves the envelope with the pitch
    };

    struct Output
    {
        float pitchCents  = 0.0f;
        float gain        = 1.0f;
        float formantCents = 0.0f;
    };

    void prepare (double sampleRate, int hopSizeSamples);
    void reset() noexcept;
    void noteOn() noexcept;      // restarts delay + swell
    void noteOff() noexcept;

    Output process (const Params& p) noexcept;

private:
    float shapeValue (VibratoShape s, float phase) const noexcept;

    float hopSeconds = 0.005f;
    float phase      = 0.0f;     // 0..1
    float envelope   = 0.0f;
    float sinceOnset = 0.0f;
    bool  active     = false;

    // Slow random walks so "variation" wanders rather than buzzing.
    juce::Random rng { 0x5eed1234 };
    float rateDrift  = 0.0f;
    float depthDrift = 0.0f;
};

} // namespace helix
