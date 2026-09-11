#pragma once

#include "ScaleQuantizer.h"
#include <juce_core/juce_core.h>

namespace helix
{

/** Note lengths the Note Transition control steps through, in beats. Index 0
    is off; the rest run shortest to longest so the knob turns the same way as
    Retune Speed - clockwise is slower. Kept beside the engine rather than with
    the parameters so the DSP tests can check the arithmetic. */
inline constexpr int kNumTransitionSteps = 7;
inline constexpr float kTransitionBeats[kNumTransitionSteps] =
{
    0.0f,           // off
    1.0f / 6.0f,    // 1/16 triplet
    1.0f / 4.0f,    // 1/16
    1.0f / 3.0f,    // 1/8 triplet
    1.0f / 2.0f,    // 1/8
    2.0f / 3.0f,    // 1/4 triplet
    1.0f            // 1/4
};

/** Seconds for a transition step at a tempo. Hosts that report no tempo - and
    the standalone app - get 120 BPM, the usual session default. */
inline float transitionSecondsFor (int step, double bpm) noexcept
{
    const int i = juce::jlimit (0, kNumTransitionSteps - 1, step);
    const double tempo = bpm > 1.0 ? bpm : 120.0;
    return (float) ((double) kTransitionBeats[i] * 60.0 / tempo);
}

/** Decides *where* the pitch should go and *how fast* it should get there.

    The pitch shifter is a dumb executor; everything that makes correction sound
    musical rather than robotic is decided here - how quickly the correction
    ramps in, how long the move between notes takes, whether a sustained note
    is allowed to drift, whether a scoop into a note survives, and how much of
    the singer's own vibrato is kept.
*/
class RetuneEngine
{
public:
    struct Params
    {
        float retuneMs        = 20.0f;   // 0 = instant snap (the hard-tune sound)
        float transitionSeconds = 0.0f;  // glide between notes; 0 = the target switches at once
        float humanize        = 0.0f;    // 0..1, relaxes retune on sustained notes only
        float flexTune        = 0.0f;    // 0..1, ignores pitch that is far from a target
        float naturalVibrato  = 0.0f;    // -1..1, scales the singer's own vibrato
        bool  targetIgnoresVibrato = false;
        float transposeSemis  = 0.0f;
        float detuneCents     = 0.0f;
        bool  classicMode     = false;   // period-locked response of the early hardware
    };

    struct Output
    {
        float targetMidi      = 0.0f;
        float outputMidi      = 0.0f;
        float correctionSemis = 0.0f;

        /** The part of correctionSemis that is transpose and detune rather than
            pitch correction. Consonant protection must not cancel it: leaving a
            sibilant uncorrected is right, dropping it out of the transposed key
            is not. */
        float offsetSemis     = 0.0f;
        float pitchRatio      = 1.0f;
        bool  corrected       = false;
    };

    void prepare (double sampleRate, int hopSizeSamples);
    void reset() noexcept;

    /** Scale-driven correction. */
    Output process (float detectedMidi, bool voiced, const ScaleQuantizer& scale, const Params& p) noexcept;

    /** Graph mode and MIDI target mode: the target is dictated, not derived. */
    Output processWithTarget (float detectedMidi, bool voiced, float targetMidi, const Params& p) noexcept;

    /** No target available - graph mode with no note under the playhead, or
        MIDI target mode with no key held. Keeps the contour and onset state up
        to date so the next corrected note starts from the right place. */
    Output passThrough (float detectedMidi, bool voiced, const Params& p) noexcept;

    float getSmoothedPitch() const noexcept { return carrier; }
    float getVibratoComponent() const noexcept { return lastVibrato; }

    /** True when the frame just processed began a new note. Drives the
        vibrato onset envelope. */
    bool hadOnset() const noexcept { return onsetFlag; }

private:
    Output run (float detectedMidi, bool voiced, float targetMidi, bool haveTarget,
                bool bypassNote, const Params& p) noexcept;

    float alphaFor (float tauSeconds) const noexcept;

    /** Advances the note transition by one hop and returns the pitch being
        aimed at - the target itself, or a point on the glide towards it. */
    float glideTowards (float target, float seconds) noexcept;
    float currentGlide() const noexcept;

    double fs = 44100.0;
    float  hopSeconds = 0.005f;

    float carrier      = 0.0f;   // vibrato-free pitch contour
    float lastVibrato  = 0.0f;
    float smoothedCorr = 0.0f;
    float prevDetected = 0.0f;
    float noteAge      = 0.0f;   // seconds since the last onset
    bool  wasVoiced    = false;
    bool  haveCarrier  = false;
    bool  onsetFlag    = false;

    // Note transition: once the target note changes, the aim moves from
    // glideFrom to glideTo over glideDuration instead of jumping.
    float glideFrom     = 0.0f;
    float glideTo       = 0.0f;
    float glideElapsed  = 0.0f;
    float glideDuration = 0.0f;
    bool  gliding       = false;
    bool  haveGlide     = false;
};

} // namespace helix
