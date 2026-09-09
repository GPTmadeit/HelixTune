#pragma once

#include "ScaleQuantizer.h"
#include <juce_core/juce_core.h>

namespace helix
{

/** Decides *where* the pitch should go and *how fast* it should get there.

    The pitch shifter is a dumb executor; everything that makes correction sound
    musical rather than robotic is decided here - how quickly the correction
    ramps in, whether a sustained note is allowed to drift, whether a scoop into
    a note survives, and how much of the singer's own vibrato is kept.
*/
class RetuneEngine
{
public:
    struct Params
    {
        float retuneMs        = 20.0f;   // 0 = instant snap (the hard-tune sound)
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
};

} // namespace helix
