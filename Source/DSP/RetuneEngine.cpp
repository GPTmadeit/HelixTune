#include "RetuneEngine.h"
#include <cmath>

namespace helix
{

void RetuneEngine::prepare (double sampleRate, int hopSizeSamples)
{
    fs = sampleRate;
    hopSeconds = (float) ((double) hopSizeSamples / sampleRate);
    reset();
}

void RetuneEngine::reset() noexcept
{
    carrier = lastVibrato = smoothedCorr = prevDetected = 0.0f;
    noteAge = 0.0f;
    wasVoiced = false;
    haveCarrier = false;
    onsetFlag = false;
}

float RetuneEngine::alphaFor (float tauSeconds) const noexcept
{
    if (tauSeconds <= 1.0e-5f)
        return 1.0f;

    return 1.0f - std::exp (-hopSeconds / tauSeconds);
}

RetuneEngine::Output RetuneEngine::process (float detectedMidi, bool voiced,
                                            const ScaleQuantizer& scale, const Params& p) noexcept
{
    // Choose the target from the vibrato-free contour when asked, so a wide
    // vibrato does not keep flipping the target between adjacent scale tones.
    const float basis = (p.targetIgnoresVibrato && haveCarrier) ? carrier : detectedMidi;
    const auto t = scale.findTarget (basis);

    return run (detectedMidi, voiced, t.midiNote, t.valid, t.bypass, p);
}

RetuneEngine::Output RetuneEngine::processWithTarget (float detectedMidi, bool voiced,
                                                      float targetMidi, const Params& p) noexcept
{
    return run (detectedMidi, voiced, targetMidi, true, false, p);
}

RetuneEngine::Output RetuneEngine::passThrough (float detectedMidi, bool voiced, const Params& p) noexcept
{
    return run (detectedMidi, voiced, 0.0f, false, false, p);
}

RetuneEngine::Output RetuneEngine::run (float detectedMidi, bool voiced, float targetMidi,
                                        bool haveTarget, bool bypassNote, const Params& p) noexcept
{
    Output out;

    if (! voiced)
    {
        // Let the correction relax back to zero rather than snapping, so the
        // next voiced frame does not start with a jump already applied.
        smoothedCorr *= 0.9f;
        wasVoiced = false;
        haveCarrier = false;
        onsetFlag = false;
        noteAge = 0.0f;

        out.outputMidi = detectedMidi;
        out.pitchRatio = 1.0f;
        return out;
    }

    // --- onset detection -------------------------------------------------
    const bool jumped = wasVoiced && std::abs (detectedMidi - prevDetected) > 0.7f;
    const bool onset  = jumped || ! wasVoiced;
    onsetFlag = onset;

    if (onset || ! haveCarrier)
    {
        carrier = detectedMidi;
        haveCarrier = true;
        noteAge = 0.0f;
    }
    else
    {
        noteAge += hopSeconds;
    }

    // --- vibrato separation ----------------------------------------------
    // A ~120 ms one-pole splits the pitch into a slow contour plus the
    // singer's vibrato riding on top of it.
    carrier += (detectedMidi - carrier) * alphaFor (0.12f);
    lastVibrato = detectedMidi - carrier;

    if (! haveTarget)
    {
        prevDetected = detectedMidi;
        wasVoiced = true;
        out.outputMidi = detectedMidi;
        out.pitchRatio = 1.0f;
        return out;
    }

    const float basis = (p.targetIgnoresVibrato ? carrier : detectedMidi);
    const float deviation = basis - targetMidi;      // semitones, signed

    // --- flex-tune --------------------------------------------------------
    // Correction strength falls off as the input moves away from a scale tone,
    // so deliberate scoops, bends and slides survive while genuinely flat
    // sustained notes still get pulled in.
    float flexWeight = 1.0f;
    if (p.flexTune > 1.0e-4f)
    {
        const float inner = 0.5f * (1.0f - p.flexTune);   // full-pull half-width, semitones
        const float mag   = std::abs (deviation);

        if (mag > inner)
        {
            const float span = juce::jmax (1.0e-4f, 0.5f - inner);
            const float x = juce::jlimit (0.0f, 1.0f, (mag - inner) / span);
            flexWeight = 1.0f - (x * x * (3.0f - 2.0f * x));   // smoothstep
        }
    }

    if (bypassNote)
        flexWeight = 0.0f;

    // --- retune speed -----------------------------------------------------
    // Humanize stretches the time constant only once a note has been held, so
    // onsets stay tight while sustains are allowed to breathe. That asymmetry
    // is the whole trick: uniform slow retune just sounds out of tune.
    float retuneMs = p.retuneMs;
    if (p.humanize > 1.0e-4f)
    {
        const float sustain = juce::jlimit (0.0f, 1.0f, noteAge / 0.20f);
        retuneMs *= 1.0f + p.humanize * 4.0f * sustain;
    }

    float alpha = alphaFor (retuneMs * 0.001f);

    if (p.classicMode)
    {
        // The early hardware updated once per detected period and had no
        // sub-frame smoothing, which reads as a faster, harder grab for the
        // same nominal setting.
        alpha = juce::jlimit (0.0f, 1.0f, alpha * 1.6f);
    }

    const float desired = -deviation * flexWeight;
    smoothedCorr += (desired - smoothedCorr) * alpha;

    // --- assemble ---------------------------------------------------------
    float result = detectedMidi + smoothedCorr;

    // naturalVibrato scales the performer's own modulation. Negative flattens
    // it out, positive exaggerates it. Only meaningful when the correction has
    // not already swallowed the vibrato.
    if (std::abs (p.naturalVibrato) > 1.0e-4f)
        result += lastVibrato * p.naturalVibrato;

    const float offset = p.transposeSemis + p.detuneCents * 0.01f;
    result += offset;

    out.offsetSemis     = offset;
    out.targetMidi      = targetMidi;
    out.correctionSemis = result - detectedMidi;
    out.outputMidi      = result;
    out.pitchRatio      = std::pow (2.0f, out.correctionSemis / 12.0f);
    out.corrected       = flexWeight > 1.0e-3f;

    prevDetected = detectedMidi;
    wasVoiced = true;
    return out;
}

} // namespace helix
