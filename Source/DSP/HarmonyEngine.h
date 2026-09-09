#pragma once

#include "PsolaShifter.h"
#include "ScaleQuantizer.h"
#include <vector>
#include <array>

namespace helix
{

/** Four scale-aware harmony voices built on the same shifter as the lead.

    Intervals are specified in *scale degrees*, so "a third above" tracks the
    key: it is four semitones over the tonic and three over the second degree.
    Each voice also gets its own formant scaling, small detune and timing
    offset, because four perfectly aligned copies of one voice sound like a
    chorus effect rather than like four singers.
*/
class HarmonyEngine
{
public:
    static constexpr int maxVoices = 4;

    struct VoiceParams
    {
        bool  enabled = false;
        int   degrees = 2;          // scale steps relative to the lead
        float level = 0.7f;         // linear
        float pan = 0.0f;           // -1 left .. +1 right
        float formant = 1.0f;       // 0.5 .. 2.0
        float detuneCents = 0.0f;
        float delayMs = 0.0f;       // 0 .. 60
    };

    struct Params
    {
        std::array<VoiceParams, maxVoices> voices { };
        float level = 1.0f;         // overall harmony bus level
        bool  anyEnabled = false;
    };

    void prepare (double sampleRate, int maxBlockSize, float lowestSupportedHz);
    void reset() noexcept;

    int getShifterLatency() const noexcept { return voices[0].shifter.getLatencySamples(); }

    void setRange (float minHz) noexcept;

    /** Extra delay applied to every voice so the harmony bus lines up with a
        lead path that has additional stages (the formant filter) in it. */
    void setAlignmentDelay (int samples) noexcept;

    /** Once per analysis hop: work out where each voice should sing. */
    void updateTargets (float leadMidi, float detectedMidi, bool voiced,
                        const ScaleQuantizer& scale, const Params& p) noexcept;

    /** Renders the enabled voices and sums them into the stereo output. */
    void process (const float* monoInput, float* left, float* right,
                  int numSamples, const Params& p) noexcept;

private:
    struct Voice
    {
        PsolaShifter shifter;
        std::vector<float> scratch;
        std::vector<float> delayLine;
        int delayPos = 0;
        int delaySamples = 0;
        float pitchRatio = 1.0f;
        float formantRatio = 1.0f;
        float gainL = 0.5f, gainR = 0.5f;
        float smoothedGainL = 0.0f, smoothedGainR = 0.0f;
    };

    double fs = 44100.0;
    float lastPeriod = 200.0f;
    bool  lastVoiced = false;
    int   alignmentDelay = 0;
    int   maxDelaySamples = 0;

    std::array<Voice, maxVoices> voices;
};

} // namespace helix
