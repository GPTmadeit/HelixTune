#pragma once

#include "PitchDetector.h"

namespace helix
{

/** Temporal decoding of the pitch-candidate lattice.

    A single frame cannot tell 220 Hz from 110 Hz: both explain the waveform,
    and their difference-function costs are often within a percent of each
    other. What separates them is history - a singer does not jump an octave
    for one 5 ms hop and jump back.

    This runs an online Viterbi forward pass over the candidates, where staying
    near the previous pitch is cheap and leaping is expensive but never
    forbidden. Real octave leaps still get through because the accumulated cost
    of persisting at the wrong octave overtakes the one-off jump penalty within
    a few frames.
*/
class PitchStabilizer
{
public:
    struct Result
    {
        float frequencyHz = 0.0f;
        float midiNote    = 0.0f;
        float confidence  = 0.0f;
        bool  voiced      = false;
    };

    void prepare (double sampleRate, int hopSizeSamples);
    void reset() noexcept;

    /** 0 = follow the raw detector frame by frame, 1 = strongly favour
        continuity. Maps onto the transition penalty. */
    void setSmoothing (float amount01) noexcept { smoothing = juce::jlimit (0.0f, 1.0f, amount01); }

    /** Voicing sensitivity, shared with the detector's tracking control. */
    void setTracking (float amount01) noexcept { tracking = juce::jlimit (0.0f, 1.0f, amount01); }

    Result process (const PitchDetector::Result& det, FrequencyRange range) noexcept;

private:
    static constexpr int maxStates = PitchDetector::maxCandidates;

    double fs = 44100.0;
    float  hopSeconds = 0.005f;
    float  smoothing = 0.5f;
    float  tracking = 0.5f;

    // Previous frame's surviving states.
    float prevFreq[maxStates] { };
    float prevCost[maxStates] { };
    int   prevCount = 0;

    float unvoicedCost = 0.0f;   // accumulated cost of the "no pitch" state
    bool  wasVoiced = false;
    float lastFreq = 0.0f;
    int   voicedRun = 0;
    int   unvoicedRun = 0;
};

} // namespace helix
