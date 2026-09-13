/*  Offline checks for the correction chain.

    pluginval proves the plugin does not crash or emit NaNs. It says nothing
    about whether the thing actually tunes anything, which is the only property
    that matters here. These tests measure real numbers off real signals.
*/

#include <juce_audio_basics/juce_audio_basics.h>
#include "DSP/CorrectionEngine.h"
#include "DSP/PitchDetector.h"
#include "DSP/ScaleQuantizer.h"
#include "DSP/PitchStabilizer.h"
#include "DSP/KeyDetector.h"
#include "DSP/TransientGuard.h"
#include "DSP/FormantProcessor.h"
#include "DSP/HarmonyEngine.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace helix;

static int failures = 0;
static int checks = 0;

static void check (bool ok, const juce::String& what, const juce::String& detail = {})
{
    ++checks;
    if (ok)
    {
        std::printf ("  [ok]   %s%s\n", what.toRawUTF8(),
                     detail.isEmpty() ? "" : ("  (" + detail + ")").toRawUTF8());
    }
    else
    {
        ++failures;
        std::printf ("  [FAIL] %s%s\n", what.toRawUTF8(),
                     detail.isEmpty() ? "" : ("  (" + detail + ")").toRawUTF8());
    }
}

static float centsBetween (float a, float b)
{
    return 1200.0f * std::log2 (a / b);
}

/** A vocal-ish tone: a few harmonics with a decaying spectrum. A pure sine is
    an unrealistically easy target for a pitch tracker. */
static void fillTone (juce::AudioBuffer<float>& buf, double sr, double freq, float amp = 0.35f)
{
    const int n = buf.getNumSamples();

    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
    {
        auto* d = buf.getWritePointer (ch);

        for (int i = 0; i < n; ++i)
        {
            const double t = (double) i / sr;
            double v = 0.0;

            for (int h = 1; h <= 6; ++h)
                v += std::sin (2.0 * juce::MathConstants<double>::pi * freq * h * t) / (double) h;

            d[i] = (float) (v * amp * 0.5);
        }
    }
}

/** Measures f0 over the tail of a signal, skipping the plugin's latency. */
static float measurePitch (const float* data, int numSamples, double sr, int skipSamples)
{
    PitchDetector det;
    det.prepare (sr);
    det.setInputType (InputType::instrument);
    det.setTracking (0.5f);

    const int frame = det.getFrameSize();
    std::vector<float> sum;
    float acc = 0.0f;
    int count = 0;

    for (int pos = skipSamples; pos + frame <= numSamples; pos += 512)
    {
        const auto r = det.process (data + pos);
        if (r.voiced)
        {
            acc += r.frequencyHz;
            ++count;
        }
    }

    return count > 0 ? acc / (float) count : 0.0f;
}

// ---------------------------------------------------------------------------

static void testDetectorAccuracy()
{
    std::printf ("\nPitch detector accuracy\n");

    const double sr = 44100.0;
    const double freqs[] = { 82.41, 110.0, 220.0, 329.63, 440.0, 880.0 };

    for (double f : freqs)
    {
        juce::AudioBuffer<float> buf (1, 32768);
        fillTone (buf, sr, f);

        const float measured = measurePitch (buf.getReadPointer (0), buf.getNumSamples(), sr, 0);
        const float err = std::abs (centsBetween (measured, (float) f));

        check (err < 2.0f,
               "detects " + juce::String (f, 2) + " Hz",
               juce::String (measured, 3) + " Hz, " + juce::String (err, 2) + " cents error");
    }
}

static void testScaleQuantizer()
{
    std::printf ("\nScale quantiser\n");

    ScaleQuantizer q;
    q.setKey (0);      // C
    q.setScale (1);    // Major

    // F#4 (66) is not in C major; nearest legal tones are F4 (65) and G4 (67).
    const auto t1 = q.findTarget (66.2f);
    check (t1.valid && std::abs (t1.midiNote - 67.0f) < 0.001f,
           "C major pulls 66.2 up to G4", "got " + juce::String (t1.midiNote, 3));

    const auto t2 = q.findTarget (65.8f);
    check (t2.valid && std::abs (t2.midiNote - 65.0f) < 0.001f,
           "C major pulls 65.8 down to F4", "got " + juce::String (t2.midiNote, 3));

    // Removing a note must push the target to the next legal one.
    q.setNoteState (67, NoteState::removed);   // G
    const auto t3 = q.findTarget (66.6f);
    check (t3.valid && std::abs (t3.midiNote - 65.0f) < 0.001f,
           "removing G re-targets to F4", "got " + juce::String (t3.midiNote, 3));

    q.clearNoteStates();

    // Bypassed notes are still found, but flagged so no correction is applied.
    q.setNoteState (64, NoteState::bypassed);  // E
    const auto t4 = q.findTarget (64.1f);
    check (t4.valid && t4.bypass, "bypassed E reports bypass");

    q.clearNoteStates();

    // Just intonation must actually deviate from equal temperament.
    q.setScale (21);   // Just Intonation
    const auto t5 = q.findTarget (64.0f);       // major third above C
    const float dev = (t5.midiNote - 64.0f) * 100.0f;
    check (t5.valid && dev < -10.0f && dev > -17.0f,
           "just-intonation major third is ~14 cents flat",
           juce::String (dev, 2) + " cents");
}



static void testPitchLattice()
{
    std::printf ("\nPitch candidate lattice\n");

    const double sr = 44100.0;
    const double f = 429.95;
    juce::AudioBuffer<float> buf (1, 32768);
    fillTone (buf, sr, f);

    PitchDetector det;
    det.prepare (sr);
    det.setInputType (InputType::instrument);
    det.setTracking (0.5f);

    const auto r = det.process (buf.getReadPointer (0) + 8192);

    check (r.numCandidates > 0, "lattice is populated",
           juce::String (r.numCandidates) + " candidates");

    // The true period must be present and must be the cheapest. YIN's
    // cumulative mean scores 2T *better* than T on a periodic signal, so
    // ranking candidates by raw cost silently discards the right answer.
    bool foundTrue = false;
    float trueCost = 1.0f, subCost = 0.0f;

    for (int i = 0; i < r.numCandidates; ++i)
    {
        const float cents = std::abs (centsBetween (r.candidates[i].frequencyHz, (float) f));
        if (cents < 10.0f) { foundTrue = true; trueCost = r.candidates[i].cost; }

        const float octaveDown = std::abs (centsBetween (r.candidates[i].frequencyHz, (float) f * 0.5f));
        if (octaveDown < 10.0f) subCost = r.candidates[i].cost;
    }

    check (foundTrue, "true period is in the lattice");
    check (subCost > trueCost, "the octave-down subharmonic costs more than the true period",
           "T=" + juce::String (trueCost, 4) + " vs 2T=" + juce::String (subCost, 4));
}

static void testStabilizerHoldsOctave()
{
    std::printf ("\nOctave stability over a full take\n");

    const double sr = 44100.0;
    const int hop = 256;
    const int total = 32768 * 6;

    // Vibrato makes this a realistic tracking problem rather than a static tone.
    juce::AudioBuffer<float> buf (1, total);
    {
        auto* d = buf.getWritePointer (0);
        double phase = 0.0;
        for (int i = 0; i < total; ++i)
        {
            const double t = (double) i / sr;
            const double f0 = 220.0 * std::pow (2.0, 0.3 * std::sin (2.0 * juce::MathConstants<double>::pi * 5.0 * t) / 12.0);
            phase += 2.0 * juce::MathConstants<double>::pi * f0 / sr;
            double v = 0.0;
            for (int h = 1; h <= 6; ++h)
                v += std::sin (phase * h) / (double) h;
            d[i] = (float) (v * 0.18);
        }
    }

    PitchDetector det;
    PitchStabilizer stab;
    det.prepare (sr);
    det.setInputType (InputType::altoTenor);
    det.setTracking (0.5f);
    stab.prepare (sr, hop);
    stab.setSmoothing (0.55f);

    const int frame = det.getFrameSize();
    int frames = 0, octaveErrors = 0;

    for (int pos = 0; pos + frame <= total; pos += hop)
    {
        const auto raw = det.process (buf.getReadPointer (0) + pos);
        const auto st = stab.process (raw, rangeForInputType (InputType::altoTenor));

        if (! st.voiced)
            continue;

        ++frames;
        if (std::abs (centsBetween (st.frequencyHz, 220.0f)) > 400.0f)
            ++octaveErrors;
    }

    check (frames > 100, "tracked the take", juce::String (frames) + " voiced frames");
    check (octaveErrors == 0, "no octave errors across the take",
           juce::String (octaveErrors) + " of " + juce::String (frames));
}

static void testDiatonicHarmony()
{
    std::printf ("\nDiatonic harmony intervals\n");

    ScaleQuantizer q;
    q.setKey (0);     // C
    q.setScale (1);   // Major

    // A third above the tonic is four semitones; a third above the second
    // degree is three. Fixed-interval harmony gets one of these wrong.
    check (std::abs (q.transposeByScaleDegrees (60.0f, 2) - 64.0f) < 0.001f,
           "C4 + 2 degrees is E4 (4 semitones)",
           juce::String (q.transposeByScaleDegrees (60.0f, 2), 2));

    check (std::abs (q.transposeByScaleDegrees (62.0f, 2) - 65.0f) < 0.001f,
           "D4 + 2 degrees is F4 (3 semitones)",
           juce::String (q.transposeByScaleDegrees (62.0f, 2), 2));

    check (std::abs (q.transposeByScaleDegrees (60.0f, 7) - 72.0f) < 0.001f,
           "seven degrees is exactly an octave",
           juce::String (q.transposeByScaleDegrees (60.0f, 7), 2));

    check (std::abs (q.transposeByScaleDegrees (60.0f, -7) - 48.0f) < 0.001f,
           "minus seven degrees is an octave down",
           juce::String (q.transposeByScaleDegrees (60.0f, -7), 2));
}

static void testHarmonyRendering()
{
    std::printf ("\nHarmony voice rendering\n");

    const double sr = 44100.0;
    const int blockSize = 512;
    const int total = (int) (sr * 3.0);
    const double inHz = 220.0;      // A3

    HarmonyEngine harm;
    harm.prepare (sr, blockSize, 55.0f);
    harm.setRange (90.0f);

    ScaleQuantizer q;
    q.setKey (0);
    q.setScale (1);

    HarmonyEngine::Params p;
    p.level = 1.0f;
    p.anyEnabled = true;
    p.voices[0].enabled = true;
    p.voices[0].degrees = 2;        // A3 -> C4 in C major
    p.voices[0].level = 1.0f;
    p.voices[0].pan = -1.0f;        // hard left, so the pan law is testable
    p.voices[0].formant = 1.0f;
    p.voices[0].detuneCents = 0.0f;
    p.voices[0].delayMs = 0.0f;

    juce::AudioBuffer<float> source (1, total);
    fillTone (source, sr, inHz);

    std::vector<float> left ((size_t) total, 0.0f), right ((size_t) total, 0.0f);

    const float leadMidi = 57.0f;   // A3
    for (int pos = 0; pos + blockSize <= total; pos += blockSize)
    {
        harm.updateTargets (leadMidi, leadMidi, true, q, p);
        harm.process (source.getReadPointer (0) + pos, left.data() + pos, right.data() + pos,
                      blockSize, p);
    }

    const int skip = harm.getShifterLatency() + 8192;
    const float measured = measurePitch (left.data(), total, sr, skip);
    const float expected = 261.626f;   // C4

    const float err = std::abs (centsBetween (measured, expected));
    check (err < 20.0f, "a third above A3 in C major renders as C4",
           juce::String (measured, 2) + " Hz, " + juce::String (err, 1) + " cents");

    float lPeak = 0.0f, rPeak = 0.0f;
    for (int i = skip; i < total; ++i)
    {
        lPeak = juce::jmax (lPeak, std::abs (left[(size_t) i]));
        rPeak = juce::jmax (rPeak, std::abs (right[(size_t) i]));
    }

    check (lPeak > 0.02f && rPeak < lPeak * 0.1f, "hard-left pan keeps the voice out of the right",
           "L=" + juce::String (lPeak, 3) + " R=" + juce::String (rPeak, 3));
}



/** Fraction of spectral energy that does NOT sit on the harmonic series of
    @p f0. A clean shifted voice is nearly all harmonic; grain artefacts,
    aliasing and modulation sidebands all land between the harmonics. */
static float inharmonicRatio (const float* data, int numSamples, double sr, double f0)
{
    const int size = 8192;
    juce::dsp::FFT fft (13);
    std::vector<juce::dsp::Complex<float>> in ((size_t) size), out ((size_t) size);

    for (int i = 0; i < size; ++i)
    {
        const float w = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * i / (size - 1));
        in[(size_t) i] = { (i < numSamples ? data[i] : 0.0f) * w, 0.0f };
    }

    fft.perform (in.data(), out.data(), false);

    const double binHz = sr / size;
    double total = 0.0, harmonic = 0.0;

    for (int k = 2; k < size / 2; ++k)
    {
        const double mag = std::abs (out[(size_t) k]);
        const double power = mag * mag;
        const double f = k * binHz;

        if (f > 8000.0)
            break;

        total += power;

        // Within a few bins of any harmonic counts as harmonic energy.
        const double nearest = std::round (f / f0);
        if (nearest >= 1.0 && std::abs (f - nearest * f0) < binHz * 2.5)
            harmonic += power;
    }

    return total > 0.0 ? (float) (1.0 - harmonic / total) : 0.0f;
}



/** A voice-like source: vibrato, cycle-to-cycle jitter, amplitude shimmer, a
    formant-shaped harmonic series and a breath-noise floor. A perfectly
    periodic tone flatters any pitch-synchronous algorithm; this does not. */
static void fillVoiceLike (juce::AudioBuffer<float>& buf, double sr, double f0, float noiseDb)
{
    const int n = buf.getNumSamples();
    juce::Random rng (777);

    double phase = 0.0;
    double jitter = 0.0;
    const float noiseGain = std::pow (10.0f, noiseDb / 20.0f);

    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / sr;

        // 5 Hz vibrato plus a slow random walk in the period (natural jitter).
        jitter += (rng.nextDouble() * 2.0 - 1.0) * 0.0006;
        jitter = juce::jlimit (-0.01, 0.01, jitter);

        const double vib = 0.25 * std::sin (2.0 * juce::MathConstants<double>::pi * 5.0 * t) / 12.0;
        const double f = f0 * std::pow (2.0, vib) * (1.0 + jitter);

        phase += 2.0 * juce::MathConstants<double>::pi * f / sr;

        // Harmonics under a two-formant envelope.
        double v = 0.0;
        for (int h = 1; h <= 24; ++h)
        {
            const double hf = f * h;
            if (hf > sr * 0.45) break;

            const double f1 = std::exp (-std::pow ((hf - 700.0) / 420.0, 2.0));
            const double f2 = std::exp (-std::pow ((hf - 2300.0) / 700.0, 2.0)) * 0.55;
            const double amp = (f1 + f2 + 0.05) / (double) h;

            v += std::sin (phase * h) * amp;
        }

        const double shimmer = 1.0 + 0.06 * std::sin (2.0 * juce::MathConstants<double>::pi * 4.3 * t);
        const float breath = (rng.nextFloat() * 2.0f - 1.0f) * noiseGain;

        buf.getWritePointer (0)[i] = (float) (v * 0.22 * shimmer) + breath;
    }
}

static void testShifterOnRealisticVoice()
{
    std::printf ("\nShifter artefacts on a voice-like source\n");

    const double sr = 44100.0;
    const double inHz = 220.0;
    const int blockSize = 512;
    const int total = (int) (sr * 3.0);

    auto measure = [&] (float ratio, float noiseDb)
    {
        juce::AudioBuffer<float> source (1, total);
        fillVoiceLike (source, sr, inHz, noiseDb);

        // What the input itself scores, so the shifter's own contribution can
        // be separated from the source's natural inharmonicity.
        const float inputScore = inharmonicRatio (source.getReadPointer (0) + 30000, 8192, sr, inHz);

        PsolaShifter shifter;
        shifter.prepare (sr, 55.0f, blockSize);
        shifter.setMinFrequency (90.0f);

        std::vector<float> out ((size_t) total, 0.0f);

        PitchDetector det;
        PitchStabilizer stab;
        det.prepare (sr);
        det.setInputType (InputType::altoTenor);
        stab.prepare (sr, blockSize);
        const int frame = det.getFrameSize();

        float period = (float) (sr / inHz);

        for (int pos = 0; pos + blockSize <= total; pos += blockSize)
        {
            if (pos >= frame)
            {
                const auto raw = det.process (source.getReadPointer (0) + pos - frame);
                const auto st = stab.process (raw, rangeForInputType (InputType::altoTenor));
                if (st.voiced && st.frequencyHz > 1.0f)
                    period = (float) (sr / st.frequencyHz);
            }

            shifter.process (source.getReadPointer (0) + pos, out.data() + pos, blockSize,
                             ratio, 1.0f, period, true);
        }

        const int skip = shifter.getLatencySamples() + 30000;
        const float outputScore = inharmonicRatio (out.data() + skip, 8192, sr, inHz * ratio);

        return std::make_pair (inputScore, outputScore);
    };

    for (float noiseDb : { -60.0f, -34.0f })
    {
        const auto clean = measure (1.0f, noiseDb);
        const auto third = measure (1.189f, noiseDb);
        const auto down  = measure (0.5f, noiseDb);
        const auto up    = measure (2.0f, noiseDb);

        std::printf ("         breath %.0f dB : input %.3f | ratio 1.0 %.3f | 3rd %.3f | 8ve down %.3f | 8ve up %.3f\n",
                     noiseDb, clean.first, clean.second, third.second, down.second, up.second);
    }

    const auto result = measure (1.189f, -34.0f);
    check (result.second < result.first * 3.0f + 0.10f,
           "shifting a breathy voice does not multiply its inharmonic energy",
           "input " + juce::String (result.first, 3) + " -> output " + juce::String (result.second, 3));
}

static void testShifterQualityAcrossRatios()
{
    std::printf ("\nShifter quality vs ratio (inharmonic energy, lower is cleaner)\n");

    const double sr = 44100.0;
    const double inHz = 220.0;
    const int blockSize = 512;
    const int total = (int) (sr * 3.0);

    const float ratios[] = { 0.5f, 0.667f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f };
    float worstDown = 0.0f, bestUp = 1.0f;

    for (float ratio : ratios)
    {
        PsolaShifter shifter;
        shifter.prepare (sr, 55.0f, blockSize);
        shifter.setMinFrequency (90.0f);

        juce::AudioBuffer<float> source (1, total);
        fillTone (source, sr, inHz);

        std::vector<float> out ((size_t) total, 0.0f);

        for (int pos = 0; pos + blockSize <= total; pos += blockSize)
            shifter.process (source.getReadPointer (0) + pos, out.data() + pos, blockSize,
                             ratio, 1.0f, (float) (sr / inHz), true);

        const int skip = shifter.getLatencySamples() + 20000;
        const float ratioOut = inharmonicRatio (out.data() + skip, 8192, sr, inHz * ratio);

        std::printf ("         ratio %.3f -> %7.2f Hz : inharmonic %.4f\n",
                     ratio, inHz * ratio, ratioOut);

        if (ratio < 1.0f) worstDown = juce::jmax (worstDown, ratioOut);
        else              bestUp = juce::jmin (bestUp, ratioOut);
    }

    // Downward shifts space grains further apart than upward ones. If the grain
    // length does not grow to match, the overlap-add window sum develops nulls
    // between grains - and the output is divided by that sum, so the artefacts
    // are amplified exactly where the signal is weakest.
    check (worstDown < 0.05f, "downward shifts are as clean as upward ones",
           "worst down " + juce::String (worstDown, 4) + " vs best up " + juce::String (bestUp, 4));
}

static void testHarmonyCleanliness()
{
    std::printf ("\nHarmony voice cleanliness (inharmonic energy, lower is cleaner)\n");

    const double sr = 44100.0;
    const int blockSize = 512;
    const int total = (int) (sr * 3.0);
    const double inHz = 220.0;      // A3

    ScaleQuantizer q;
    q.setKey (0);
    q.setScale (1);

    auto render = [&] (int numVoices, float formant, std::vector<float>& left)
    {
        HarmonyEngine harm;
        harm.prepare (sr, blockSize, 55.0f);
        harm.setRange (90.0f);

        HarmonyEngine::Params p;
        p.level = 1.0f;
        p.anyEnabled = true;

        const int degrees[4] = { 2, 4, -7, 7 };

        for (int v = 0; v < numVoices; ++v)
        {
            auto& vp = p.voices[(size_t) v];
            vp.enabled = true;
            vp.degrees = degrees[v];
            vp.level = 1.0f;
            vp.pan = 0.0f;
            vp.formant = formant;
            vp.detuneCents = 0.0f;
            vp.delayMs = 0.0f;
        }

        juce::AudioBuffer<float> source (1, total);
        fillTone (source, sr, inHz);

        left.assign ((size_t) total, 0.0f);
        std::vector<float> right ((size_t) total, 0.0f);

        for (int pos = 0; pos + blockSize <= total; pos += blockSize)
        {
            harm.updateTargets (57.0f, 57.0f, true, q, p);
            harm.process (source.getReadPointer (0) + pos,
                          left.data() + pos, right.data() + pos, blockSize, p);
        }

        return harm.getShifterLatency() + 20000;
    };

    std::vector<float> buf;

    // One voice, a third up (A3 -> C4 = 261.63 Hz), at three formant settings.
    int skip = render (1, 1.00f, buf);
    const float flat = inharmonicRatio (buf.data() + skip, 8192, sr, 261.626);

    skip = render (1, 1.12f, buf);
    const float up = inharmonicRatio (buf.data() + skip, 8192, sr, 261.626);

    skip = render (1, 0.90f, buf);
    const float down = inharmonicRatio (buf.data() + skip, 8192, sr, 261.626);

    std::printf ("         1 voice: formant 1.00 = %.3f | 1.12 = %.3f | 0.90 = %.3f\n",
                 flat, up, down);

    check (up < flat * 2.5f + 0.05f, "formant shift up does not wreck the voice",
           juce::String (flat, 3) + " -> " + juce::String (up, 3));
    check (down < flat * 2.5f + 0.05f, "formant shift down does not wreck the voice",
           juce::String (flat, 3) + " -> " + juce::String (down, 3));

    check (flat < 0.30f, "a single unshifted-formant voice is clean",
           juce::String (flat, 3));
}


static void testHarmonySilentOnConsonants()
{
    std::printf ("\nHarmony behaviour during unvoiced material\n");

    const double sr = 44100.0;
    const int blockSize = 512;
    const int total = (int) (sr * 4.0);

    ScaleQuantizer q;
    q.setKey (0);
    q.setScale (1);

    // A phrase: sung notes separated by fricatives, which is what a real vocal
    // looks like and what a steady test tone never exercises.
    juce::AudioBuffer<float> source (1, total);
    juce::Random rng (4242);
    std::vector<bool> voicedAt ((size_t) total, false);
    {
        auto* d = source.getWritePointer (0);
        double phase = 0.0;

        for (int i = 0; i < total; ++i)
        {
            const double t = (double) i / sr;
            const bool voiced = std::fmod (t, 0.40) < 0.28;   // 280 ms note, 120 ms consonant
            voicedAt[(size_t) i] = voiced;

            if (voiced)
            {
                phase += 2.0 * juce::MathConstants<double>::pi * 220.0 / sr;
                double v = 0.0;
                for (int h = 1; h <= 6; ++h)
                    v += std::sin (phase * h) / (double) h;

                d[i] = (float) (v * 0.18);
            }
            else
            {
                d[i] = (rng.nextFloat() * 2.0f - 1.0f) * 0.12f;
            }
        }
    }

    HarmonyEngine harm;
    harm.prepare (sr, blockSize, 55.0f);
    harm.setRange (90.0f);

    HarmonyEngine::Params p;
    p.level = 1.0f;
    p.anyEnabled = true;

    for (int v = 0; v < 2; ++v)
    {
        auto& vp = p.voices[(size_t) v];
        vp.enabled = true;
        vp.degrees = (v == 0 ? 2 : 4);
        vp.level = 1.0f;
        vp.pan = 0.0f;
        vp.formant = 1.0f;
        vp.detuneCents = 0.0f;
        vp.delayMs = 0.0f;
    }

    PitchDetector det;
    PitchStabilizer stab;
    TransientGuard guard;
    det.prepare (sr);
    det.setInputType (InputType::altoTenor);
    stab.prepare (sr, blockSize);
    guard.prepare (sr, blockSize);
    guard.setSensitivity (0.6f);

    std::vector<float> left ((size_t) total, 0.0f), right ((size_t) total, 0.0f);
    const int frame = det.getFrameSize();

    for (int pos = 0; pos + blockSize <= total; pos += blockSize)
    {
        bool voiced = false;
        float midi = 57.0f;

        if (pos >= frame)
        {
            const auto raw = det.process (source.getReadPointer (0) + pos - frame);
            const auto st = stab.process (raw, rangeForInputType (InputType::altoTenor));
            voiced = st.voiced;
            if (voiced) midi = st.midiNote;
        }

        // Same wiring the engine uses: the guard ducks the harmony bus out of
        // consonants that the pitch tracker is still holding a note through.
        const float consonant = guard.process (source.getReadPointer (0) + pos, blockSize,
                                               voiced ? 0.9f : 0.05f);
        harm.setGate (1.0f - consonant);

        harm.updateTargets (midi, midi, voiced, q, p);
        harm.process (source.getReadPointer (0) + pos, left.data() + pos, right.data() + pos,
                      blockSize, p);
    }

    // Compare harmony output energy during sung sections against consonants.
    const int lat = harm.getShifterLatency();
    double voicedEnergy = 0.0, unvoicedEnergy = 0.0;
    int voicedCount = 0, unvoicedCount = 0;

    for (int i = lat + 20000; i < total - lat; ++i)
    {
        // Account for the bus delay when deciding which section a sample is in.
        const bool wasVoiced = voicedAt[(size_t) juce::jmax (0, i - lat)];
        const double e = (double) left[(size_t) i] * left[(size_t) i];

        if (wasVoiced) { voicedEnergy += e; ++voicedCount; }
        else           { unvoicedEnergy += e; ++unvoicedCount; }
    }

    const float voicedRms = (float) std::sqrt (voicedEnergy / juce::jmax (1, voicedCount));
    const float unvoicedRms = (float) std::sqrt (unvoicedEnergy / juce::jmax (1, unvoicedCount));
    const float leak = unvoicedRms / juce::jmax (1.0e-6f, voicedRms);

    std::printf ("         harmony RMS: sung %.4f | consonant %.4f | leak %.1f%%\n",
                 voicedRms, unvoicedRms, leak * 100.0f);

    // A regression guard, not a claim of correctness. Some energy here is
    // legitimate - the gain release and the shifter's own tail both decay
    // across the start of a consonant. The number to watch is whether it
    // *grows*: that would mean voices are again passing the dry input through,
    // which is what turns stacked harmony into flanged mush.
    check (leak < 0.45f, "harmony energy during consonants stays within its release tail",
           juce::String (leak * 100.0f, 1) + "% of sung level");
}

static void testHarmonyGainStaging()
{
    std::printf ("\nHarmony gain staging\n");

    const double sr = 44100.0;
    const int blockSize = 512;
    const int total = (int) (sr * 2.5);

    auto run = [&] (int numVoices)
    {
        CorrectionEngine engine;
        engine.prepare (sr, blockSize, 2);

        PitchFifo fifo;
        CorrectionEngine::Settings s;
        s.inputType = InputType::altoTenor;
        s.retune.retuneMs = 20.0f;
        s.key = 0;
        s.scaleIndex = 1;              // C major
        s.mix = 1.0f;
        s.outputGain = 1.0f;

        const int   degrees[4] = {  2,  4, -7,  7 };
        const float pans[4]    = { -0.55f, 0.55f, -0.2f, 0.25f };

        s.harmony.level = 0.7f;
        s.harmony.anyEnabled = numVoices > 0;

        for (int v = 0; v < numVoices; ++v)
        {
            auto& vp = s.harmony.voices[(size_t) v];
            vp.enabled = true;
            vp.degrees = degrees[v];
            vp.level = 0.75f;
            vp.pan = pans[v];
            vp.formant = 1.0f;
            vp.detuneCents = 0.0f;
        }

        juce::AudioBuffer<float> source (2, total);
        fillTone (source, sr, 220.0);

        juce::AudioBuffer<float> block (2, blockSize);
        const int skip = engine.getLatencySamples() + (int) (sr * 0.7);

        float peak = 0.0f;

        for (int pos = 0; pos + blockSize <= total; pos += blockSize)
        {
            for (int ch = 0; ch < 2; ++ch)
                block.copyFrom (ch, 0, source, ch, pos, blockSize);

            engine.process (block, s, (double) pos / sr, -1.0, nullptr, 0.0f, false, fifo);

            if (pos > skip)
                peak = juce::jmax (peak, block.getMagnitude (0, blockSize));
        }

        return peak;
    };

    const float lead = run (0);
    const float v1 = run (1);
    const float v2 = run (2);
    const float v3 = run (3);
    const float v4 = run (4);

    std::printf ("         lead %.3f | +1 %.3f | +2 %.3f | +3 %.3f | +4 %.3f\n",
                 lead, v1, v2, v3, v4);

    // Anything at or past full scale clips in the host and is heard as grit.
    check (v4 < 0.99f, "four voices stay below full scale",
           juce::String (v4, 3));
    check (v3 < 0.99f, "three voices stay below full scale",
           juce::String (v3, 3));

    // Adding voices should thicken the sound, not just make it louder.
    check (v4 < v1 * 1.9f, "level does not grow in proportion to voice count",
           "1 voice " + juce::String (v1, 3) + " -> 4 voices " + juce::String (v4, 3));
}

static void testKeyDetection()
{
    std::printf ("\nAuto-Key detection\n");

    const double sr = 44100.0;
    const int hop = 256;

    KeyDetector kd;
    kd.prepare (sr, hop);

    // A melody that sits clearly in C major: tonic and dominant heavy.
    const int melody[] = { 60, 64, 67, 64, 60, 62, 64, 65, 67, 65, 64, 62, 60, 67, 72, 67 };
    const int n = (int) (sizeof (melody) / sizeof (melody[0]));

    for (int rep = 0; rep < 40; ++rep)
        for (int i = 0; i < n; ++i)
            for (int frame = 0; frame < 40; ++frame)     // ~200 ms per note
                kd.push ((float) melody[i], true, 1.0f);

    const auto r = kd.getEstimate();
    check (r.valid, "produced an estimate");
    check (r.valid && r.rootPitchClass == 0 && ! r.minor, "identified C major",
           juce::String (r.rootPitchClass) + (r.minor ? " minor" : " major")
               + ", confidence " + juce::String (r.confidence, 2));

    // The relative minor uses the same notes; only the weighting differs.
    KeyDetector kd2;
    kd2.prepare (sr, hop);
    const int aMinor[] = { 57, 60, 64, 57, 59, 60, 62, 57, 55, 57, 64, 57 };
    const int n2 = (int) (sizeof (aMinor) / sizeof (aMinor[0]));

    for (int rep = 0; rep < 40; ++rep)
        for (int i = 0; i < n2; ++i)
            for (int frame = 0; frame < 40; ++frame)
                kd2.push ((float) aMinor[i], true, 1.0f);

    const auto r2 = kd2.getEstimate();
    check (r2.valid && r2.rootPitchClass == 9 && r2.minor, "identified A minor",
           juce::String (r2.rootPitchClass) + (r2.minor ? " minor" : " major"));
}

static void testTransientGuard()
{
    std::printf ("\nConsonant detection\n");

    const double sr = 44100.0;
    const int hop = 256;

    TransientGuard g;
    g.prepare (sr, hop);
    g.setSensitivity (1.0f);

    // Sustained vowel: periodic, low frequency content -> must not trigger.
    juce::AudioBuffer<float> tone (1, hop * 40);
    fillTone (tone, sr, 220.0);

    float toneAmount = 0.0f;
    for (int i = 0; i < 40; ++i)
        toneAmount = g.process (tone.getReadPointer (0) + i * hop, hop, 0.98f);

    check (toneAmount < 0.1f, "a sustained vowel is not flagged",
           juce::String (toneAmount, 3));

    // Fricative: broadband, aperiodic -> must trigger.
    g.reset();
    juce::Random rng (99);
    std::vector<float> noise ((size_t) (hop * 40));
    for (auto& v : noise)
        v = rng.nextFloat() * 2.0f - 1.0f;

    float noiseAmount = 0.0f;
    for (int i = 0; i < 40; ++i)
        noiseAmount = g.process (noise.data() + i * hop, hop, 0.05f);

    check (noiseAmount > 0.5f, "a fricative is flagged", juce::String (noiseAmount, 3));
}

static float spectralCentroid (const float* data, int numSamples, double sr)
{
    juce::dsp::FFT fft (11);
    const int size = 2048;
    std::vector<juce::dsp::Complex<float>> in ((size_t) size), out ((size_t) size);

    for (int i = 0; i < size; ++i)
    {
        const float w = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * i / (size - 1));
        in[(size_t) i] = { (i < numSamples ? data[i] : 0.0f) * w, 0.0f };
    }

    fft.perform (in.data(), out.data(), false);

    double num = 0.0, den = 0.0;
    for (int k = 1; k < size / 2; ++k)
    {
        const double mag = std::abs (out[(size_t) k]);
        const double f = (double) k * sr / size;
        num += f * mag;
        den += mag;
    }

    return den > 0.0 ? (float) (num / den) : 0.0f;
}

static void testFormantProcessor()
{
    std::printf ("\nLPC formant control\n");

    const double sr = 44100.0;
    const int blockSize = 512;
    const int total = (int) (sr * 1.5);

    auto run = [&] (float ratio, std::vector<float>& out)
    {
        FormantProcessor fp;
        fp.prepare (sr, blockSize, 1);

        juce::AudioBuffer<float> src (1, total);
        fillTone (src, sr, 150.0);      // low f0, so formant motion is visible

        out.assign ((size_t) total, 0.0f);
        std::copy (src.getReadPointer (0), src.getReadPointer (0) + total, out.begin());

        for (int pos = 0; pos + blockSize <= total; pos += blockSize)
        {
            fp.setRatio (ratio);
            fp.analyse (src.getReadPointer (0) + juce::jmax (0, pos - 1024),
                        juce::jmin (1024, pos + blockSize));
            fp.process (out.data() + pos, blockSize, 0);
        }
    };

    std::vector<float> flat, shiftedUp, shiftedDown;
    run (1.0f, flat);
    run (1.5f, shiftedUp);
    run (0.7f, shiftedDown);

    const int at = total / 2;
    const float cFlat = spectralCentroid (flat.data() + at, 2048, sr);
    const float cUp   = spectralCentroid (shiftedUp.data() + at, 2048, sr);
    const float cDown = spectralCentroid (shiftedDown.data() + at, 2048, sr);

    check (cUp > cFlat * 1.05f, "raising the formant ratio raises the spectral centroid",
           juce::String (cFlat, 0) + " -> " + juce::String (cUp, 0) + " Hz");
    check (cDown < cFlat * 0.95f, "lowering it lowers the centroid",
           juce::String (cFlat, 0) + " -> " + juce::String (cDown, 0) + " Hz");

    bool clean = true;
    for (float v : shiftedUp)
        if (! std::isfinite (v) || std::abs (v) > 8.0f)
            clean = false;

    check (clean, "formant filter stays bounded and finite");
}

static void runChain (double sr, double inputHz, CorrectionEngine::Settings s,
                      float& outHz, float& peak)
{
    const int blockSize = 512;
    const int total = (int) (sr * 3.0);

    CorrectionEngine engine;
    engine.prepare (sr, blockSize, 1);

    PitchFifo fifo;
    juce::AudioBuffer<float> source (1, total);
    fillTone (source, sr, inputHz);

    juce::AudioBuffer<float> out (1, total);
    out.clear();

    juce::AudioBuffer<float> block (1, blockSize);

    for (int pos = 0; pos + blockSize <= total; pos += blockSize)
    {
        block.copyFrom (0, 0, source, 0, pos, blockSize);
        engine.process (block, s, (double) pos / sr, -1.0, nullptr, 0.0f, false, fifo);
        out.copyFrom (0, pos, block, 0, 0, blockSize);
    }

    // Report what the engine actually decided, so a failure says why.
    std::vector<PitchFrame> frames;
    fifo.drain (frames);

    int voiced = 0;
    double consonant = 0.0, shift = 0.0, conf = 0.0, detMidi = 0.0;
    for (const auto& f : frames)
    {
        consonant += f.consonant;
        conf += f.confidence;
        if (f.voiced) { ++voiced; shift += (f.outputMidi - f.detectedMidi); detMidi += f.detectedMidi; }
    }

    const size_t nf = juce::jmax ((size_t) 1, frames.size());
    std::printf ("         [frames=%d voiced=%.0f%% conf=%.2f consonant=%.2f meanShift=%.3f st detMidi=%.2f ratio=%.3f period=%.1f]\n",
                 (int) frames.size(), 100.0 * voiced / (double) nf,
                 conf / (double) nf, consonant / (double) nf,
                 voiced ? shift / voiced : 0.0,
                 voiced ? detMidi / voiced : 0.0,
                 engine.getLastPitchRatio(), engine.getLastPeriod());

    // Skip the algorithmic latency plus time for the retune ramp to settle.
    const int skip = engine.getLatencySamples() + (int) (sr * 0.5);
    outHz = measurePitch (out.getReadPointer (0), total, sr, skip);
    peak = out.getMagnitude (skip, total - skip);
}

static void testEndToEndCorrection()
{
    std::printf ("\nEnd-to-end correction\n");

    const double sr = 44100.0;

    // A4 sung 40 cents flat.
    const double flatA = 440.0 * std::pow (2.0, -40.0 / 1200.0);

    CorrectionEngine::Settings s;
    s.inputType = InputType::instrument;
    s.tracking = 0.5f;
    s.key = 0;
    s.scaleIndex = 0;              // chromatic
    s.retune.retuneMs = 0.0f;      // hard snap
    s.mix = 1.0f;
    s.outputGain = 1.0f;

    float outHz = 0.0f, peak = 0.0f;
    runChain (sr, flatA, s, outHz, peak);

    float err = std::abs (centsBetween (outHz, 440.0f));
    check (err < 2.0f, "429.9 Hz (40 cents flat) is corrected to A440",
           juce::String (outHz, 2) + " Hz, " + juce::String (err, 2) + " cents from A440");
    check (peak > 0.05f, "output is not silent", "peak " + juce::String (peak, 4));

    // Transpose must move the result by exactly an octave.
    s.retune.transposeSemis = 12.0f;
    runChain (sr, flatA, s, outHz, peak);
    err = std::abs (centsBetween (outHz, 880.0f));
    check (err < 2.0f, "transpose +12 lands an octave up",
           juce::String (outHz, 2) + " Hz, " + juce::String (err, 2) + " cents from 880");

    s.retune.transposeSemis = 0.0f;

    // Bypass must pass the original pitch through untouched.
    s.bypass = true;
    runChain (sr, flatA, s, outHz, peak);
    err = std::abs (centsBetween (outHz, (float) flatA));
    check (err < 3.0f, "bypass leaves pitch alone",
           juce::String (outHz, 2) + " Hz vs input " + juce::String (flatA, 2));
    s.bypass = false;

    // With correction off-scale: 429.9 Hz in C major should still go to A440,
    // since A is in C major.
    s.scaleIndex = 1;
    runChain (sr, flatA, s, outHz, peak);
    err = std::abs (centsBetween (outHz, 440.0f));
    check (err < 2.0f, "C major also corrects to A440",
           juce::String (outHz, 2) + " Hz");
}

/** Finds the lag, in samples, that best aligns @p b to @p a around @p centre. */
static double bestLag (const float* a, const float* b, int centre, int window, int search)
{
    double bestScore = -1.0e30;
    int bestD = 0;

    for (int d = -search; d <= search; ++d)
    {
        double dot = 0.0, energy = 1.0e-12;

        for (int i = -window / 2; i < window / 2; ++i)
        {
            const double x = a[centre + i];
            const double y = b[centre + i + d];
            dot += x * y;
            energy += y * y;
        }

        const double score = dot / std::sqrt (energy);
        if (score > bestScore) { bestScore = score; bestD = d; }
    }

    return (double) bestD;
}

/** At ratio 1.0 the shifter should emit a constant-delay copy of its input.
    If the alignment lag between input and output *changes* over time, the
    delay is drifting - and a drifting delay is exactly a pitch error. This
    measures that drift directly instead of inferring it from a pitch estimate. */
static void testShifterDelayDrift()
{
    std::printf ("\nPSOLA delay stability at ratio 1.0\n");

    const double sr = 44100.0;
    const double inHz = 220.0;
    const int blockSize = 512;
    const int total = (int) (sr * 4.0);

    PsolaShifter shifter;
    shifter.prepare (sr, 55.0f, blockSize);

    juce::AudioBuffer<float> source (1, total);
    fillTone (source, sr, inHz);
    std::vector<float> out ((size_t) total, 0.0f);

    for (int pos = 0; pos + blockSize <= total; pos += blockSize)
        shifter.process (source.getReadPointer (0) + pos, out.data() + pos, blockSize,
                         1.0f, 1.0f, (float) (sr / inHz), true);

    const int lat = shifter.getLatencySamples();
    const int early = lat + 20000;
    const int late  = lat + 120000;
    const int period = (int) (sr / inHz);

    // Search +/- one period around the nominal latency at two points in time.
    const double lagEarly = bestLag (out.data(), source.getReadPointer (0), early, 4096, period);
    const double lagLate  = bestLag (out.data(), source.getReadPointer (0), late,  4096, period);

    const double drift = lagLate - lagEarly;
    const double cents = 1200.0 * std::log2 (1.0 + drift / (double) (late - early));

    std::printf ("         lag@early=%.1f  lag@late=%.1f  drift=%.1f samples over %d\n",
                 lagEarly, lagLate, drift, late - early);

    check (std::abs (cents) < 1.0, "delay does not drift at ratio 1.0",
           juce::String (cents, 3) + " cents equivalent");
}

/** Drives the shifter directly at a known ratio, with no detector or retune
    engine in the loop, so any pitch error is unambiguously the shifter's. */
static void testShifterRatioAccuracy()
{
    std::printf ("\nPSOLA ratio accuracy (shifter in isolation)\n");

    const double sr = 44100.0;
    const double inHz = 220.0;
    const int blockSize = 512;
    const int total = (int) (sr * 3.0);

    const float ratios[] = { 1.0f, 1.05f, 1.5f, 2.0f, 0.75f };

    for (float ratio : ratios)
    {
        PsolaShifter shifter;
        shifter.prepare (sr, 55.0f, blockSize);
        shifter.setMinFrequency (55.0f);

        juce::AudioBuffer<float> source (1, total);
        fillTone (source, sr, inHz);

        std::vector<float> out ((size_t) total, 0.0f);

        for (int pos = 0; pos + blockSize <= total; pos += blockSize)
            shifter.process (source.getReadPointer (0) + pos, out.data() + pos, blockSize,
                             ratio, 1.0f, (float) (sr / inHz), true);

        const int skip = shifter.getLatencySamples() + 4096;
        const float measured = measurePitch (out.data(), total, sr, skip);
        const float expected = (float) inHz * ratio;
        const float err = centsBetween (measured, expected);

        check (std::abs (err) < 1.0f,
               "ratio " + juce::String (ratio, 2) + " -> " + juce::String (expected, 2) + " Hz",
               juce::String (measured, 3) + " Hz, " + juce::String (err, 2) + " cents");
    }
}

static void testStability()
{
    std::printf ("\nNumerical stability\n");

    const double sr = 48000.0;
    const int blockSize = 256;
    const int total = blockSize * 400;

    CorrectionEngine engine;
    engine.prepare (sr, blockSize, 2);

    PitchFifo fifo;
    CorrectionEngine::Settings s;
    s.inputType = InputType::altoTenor;
    s.retune.retuneMs = 15.0f;
    s.mix = 1.0f;
    s.outputGain = 1.0f;

    juce::Random rng (12345);
    juce::AudioBuffer<float> block (2, blockSize);
    bool clean = true;
    float maxAbs = 0.0f;

    // White noise is the worst case for a pitch tracker: no stable period for
    // the epoch search to lock onto.
    for (int i = 0; i < total / blockSize; ++i)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = block.getWritePointer (ch);
            for (int n = 0; n < blockSize; ++n)
                d[n] = rng.nextFloat() * 2.0f - 1.0f;
        }

        engine.process (block, s, (double) (i * blockSize) / sr, -1.0, nullptr, 0.0f, false, fifo);

        for (int ch = 0; ch < 2; ++ch)
        {
            const auto* d = block.getReadPointer (ch);
            for (int n = 0; n < blockSize; ++n)
            {
                if (! std::isfinite (d[n]))
                    clean = false;

                maxAbs = juce::jmax (maxAbs, std::abs (d[n]));
            }
        }
    }

    check (clean, "no NaN or Inf on unpitched noise input");
    check (maxAbs < 4.0f, "output stays bounded on noise",
           "peak " + juce::String (maxAbs, 3));
}

static void testSilence()
{
    std::printf ("\nSilence handling\n");

    const double sr = 44100.0;
    const int blockSize = 512;

    CorrectionEngine engine;
    engine.prepare (sr, blockSize, 1);

    PitchFifo fifo;
    CorrectionEngine::Settings s;
    s.mix = 1.0f;
    s.outputGain = 1.0f;

    juce::AudioBuffer<float> block (1, blockSize);
    float maxAbs = 0.0f;

    for (int i = 0; i < 200; ++i)
    {
        block.clear();
        engine.process (block, s, (double) (i * blockSize) / sr, -1.0, nullptr, 0.0f, false, fifo);
        maxAbs = juce::jmax (maxAbs, block.getMagnitude (0, blockSize));
    }

    check (maxAbs < 1.0e-6f, "silence in, silence out", "peak " + juce::String (maxAbs, 9));
}


/** Linking renders a mono source once. The claim is that nothing audible - or
    even measurable - changes, including across the moment the input stops
    being mono, so this compares against an engine that never links. */
static void testStereoLinkingIsExact()
{
    std::printf ("\nStereo linking\n");

    const double sr = 44100.0;
    const int blockSize = 256;
    const int total = (int) (sr * 3.0);
    const int split = (int) (sr * 1.5);

    juce::AudioBuffer<float> source (1, total);
    fillVoiceLike (source, sr, 196.0, -40.0f);

    std::vector<float> inL ((size_t) total), inR ((size_t) total);
    for (int i = 0; i < total; ++i)
    {
        inL[(size_t) i] = source.getSample (0, i);
        inR[(size_t) i] = i < split ? source.getSample (0, i) : 0.9f * source.getSample (0, i - 37);
    }

    CorrectionEngine::Settings s;
    s.inputType = InputType::altoTenor;
    s.key = 0;
    s.scaleIndex = 1;
    s.retune.retuneMs = 10.0f;
    s.throatLength = 1.15f;            // formant filter live, so its state is copied too
    s.mix = 1.0f;
    s.outputGain = 1.0f;
    s.harmony.anyEnabled = true;
    s.harmony.level = 0.7f;
    s.harmony.voices[0].enabled = true;
    s.harmony.voices[0].degrees = 2;
    s.harmony.voices[0].level = 0.7f;
    s.harmony.voices[0].pan = -0.4f;

    auto render = [&] (bool link, std::vector<float>& outL, std::vector<float>& outR)
    {
        auto engine = std::make_unique<CorrectionEngine>();
        engine->prepare (sr, blockSize, 2);
        engine->setStereoLinking (link);

        auto fifo = std::make_unique<PitchFifo>();
        juce::AudioBuffer<float> block (2, blockSize);

        outL.assign ((size_t) total, 0.0f);
        outR.assign ((size_t) total, 0.0f);

        for (int pos = 0; pos + blockSize <= total; pos += blockSize)
        {
            std::copy (inL.begin() + pos, inL.begin() + pos + blockSize, block.getWritePointer (0));
            std::copy (inR.begin() + pos, inR.begin() + pos + blockSize, block.getWritePointer (1));

            engine->process (block, s, (double) pos / sr, -1.0, nullptr, 0.0f, false, *fifo);

            std::copy (block.getReadPointer (0), block.getReadPointer (0) + blockSize, outL.begin() + pos);
            std::copy (block.getReadPointer (1), block.getReadPointer (1) + blockSize, outR.begin() + pos);
        }
    };

    std::vector<float> linkedL, linkedR, separateL, separateR;
    render (true,  linkedL, linkedR);
    render (false, separateL, separateR);

    double maxDiff = 0.0;
    for (int i = 0; i < total; ++i)
        maxDiff = juce::jmax (maxDiff,
                              (double) std::abs (linkedL[(size_t) i] - separateL[(size_t) i]),
                              (double) std::abs (linkedR[(size_t) i] - separateR[(size_t) i]));

    check (maxDiff == 0.0, "a mono source rendered once is bit-identical to rendering both channels",
           "max difference " + juce::String (maxDiff, 12) + ", across the switch to stereo at 1.5 s");

    double side = 0.0;
    for (int i = split + (int) sr / 10; i < total; ++i)
    {
        const double d = linkedL[(size_t) i] - linkedR[(size_t) i];
        side += d * d;
    }

    check (side > 1.0e-3, "the channels separate once the input does",
           "side energy " + juce::String (side, 4));
}

/** Detection runs on a decimated copy above 64 kHz. It must still land the
    correction where it did at 44.1 kHz. */
static void testHighSampleRates()
{
    std::printf ("\nHigh sample rates (detection on a decimated copy)\n");

    const double flatA = 440.0 * std::pow (2.0, -40.0 / 1200.0);

    for (double sr : { 88200.0, 96000.0, 192000.0 })
    {
        for (auto type : { InputType::instrument, InputType::generic })
        {
            CorrectionEngine::Settings s;
            s.inputType = type;
            s.tracking = 0.5f;
            s.key = 0;
            s.scaleIndex = 0;
            s.retune.retuneMs = 0.0f;
            s.mix = 1.0f;
            s.outputGain = 1.0f;

            float outHz = 0.0f, peak = 0.0f;
            runChain (sr, flatA, s, outHz, peak);

            const float err = std::abs (centsBetween (outHz, 440.0f));
            check (err < 2.0f,
                   juce::String (sr / 1000.0, 1) + " kHz, " + (type == InputType::generic ? "Generic" : "Instrument")
                       + ": 429.9 Hz corrected to A440",
                   juce::String (outHz, 2) + " Hz, " + juce::String (err, 2) + " cents");
        }
    }
}

/** Idle voices no longer run. That has to mean silent and free while off, a
    clean start when switched on, and nothing left behind when switched off. */
static void testHarmonyVoiceSwitching()
{
    std::printf ("\nHarmony voices switching on and off\n");

    const double sr = 44100.0;
    const int blockSize = 512;
    const int total = (int) (sr * 3.0);

    juce::AudioBuffer<float> source (1, total);
    fillVoiceLike (source, sr, 196.0, -40.0f);

    CorrectionEngine::Settings s;
    s.inputType = InputType::altoTenor;
    s.key = 0;
    s.scaleIndex = 1;
    s.retune.retuneMs = 10.0f;
    s.mix = 1.0f;
    s.outputGain = 1.0f;
    s.harmony.anyEnabled = true;
    s.harmony.level = 0.7f;

    s.harmony.voices[0].enabled = true;
    s.harmony.voices[0].degrees = 2;
    s.harmony.voices[0].level = 0.7f;
    s.harmony.voices[0].pan = -0.5f;

    s.harmony.voices[1].enabled = false;
    s.harmony.voices[1].degrees = 4;
    s.harmony.voices[1].level = 0.7f;
    s.harmony.voices[1].pan = 0.5f;

    auto reference = std::make_unique<CorrectionEngine>();
    auto toggled   = std::make_unique<CorrectionEngine>();
    reference->prepare (sr, blockSize, 2);
    toggled->prepare (sr, blockSize, 2);

    auto fifoA = std::make_unique<PitchFifo>();
    auto fifoB = std::make_unique<PitchFifo>();

    juce::AudioBuffer<float> blockA (2, blockSize), blockB (2, blockSize);
    std::vector<float> diff ((size_t) total, 0.0f);

    for (int pos = 0; pos + blockSize <= total; pos += blockSize)
    {
        const double t = (double) pos / sr;

        for (int ch = 0; ch < 2; ++ch)
        {
            blockA.copyFrom (ch, 0, source, 0, pos, blockSize);
            blockB.copyFrom (ch, 0, source, 0, pos, blockSize);
        }

        auto sB = s;
        sB.harmony.voices[1].enabled = (t >= 1.0 && t < 2.0);

        reference->process (blockA, s,  t, -1.0, nullptr, 0.0f, false, *fifoA);
        toggled->process   (blockB, sB, t, -1.0, nullptr, 0.0f, false, *fifoB);

        for (int i = 0; i < blockSize; ++i)
            diff[(size_t) (pos + i)] = std::abs (blockA.getSample (0, i) - blockB.getSample (0, i))
                                     + std::abs (blockA.getSample (1, i) - blockB.getSample (1, i));
    }

    auto maxOver = [&] (double from, double to)
    {
        float m = 0.0f;
        for (int i = (int) (from * sr); i < juce::jmin (total, (int) (to * sr)); ++i)
            m = juce::jmax (m, diff[(size_t) i]);
        return m;
    };

    auto rmsOver = [&] (double from, double to)
    {
        double acc = 0.0;
        int count = 0;
        for (int i = (int) (from * sr); i < juce::jmin (total, (int) (to * sr)); ++i, ++count)
            acc += (double) diff[(size_t) i] * diff[(size_t) i];
        return (float) std::sqrt (acc / juce::jmax (1, count));
    };

    const float before = maxOver (0.0, 1.0);
    const float during = rmsOver (1.2, 2.0);
    const float after  = maxOver (2.2, 3.0);

    check (before == 0.0f, "a voice that is off leaves the output bit-identical",
           "max difference " + juce::String (before, 9));
    check (during > 1.0e-3f, "switching it on renders it",
           "difference rms " + juce::String (during, 4));
    check (after == 0.0f, "switching it off leaves nothing behind within 200 ms",
           "max difference " + juce::String (after, 9));
}


/** Not a pass/fail check - a number the user needs in order to decide whether
    this fits on a track. Reported as a real-time factor: 100x means one second
    of audio costs 10 ms of CPU. */
static void reportPerformance()
{
    std::printf ("\nThroughput (stereo, 44.1 kHz, 512-sample blocks)\n");

    const double sr = 44100.0;
    const int blockSize = 512;
    const int total = (int) (sr * 10.0);

    auto measure = [&] (const char* label, bool harmonyOn)
    {
        CorrectionEngine engine;
        engine.prepare (sr, blockSize, 2);

        PitchFifo fifo;
        CorrectionEngine::Settings s;
        s.inputType = InputType::altoTenor;
        s.retune.retuneMs = 20.0f;
        s.mix = 1.0f;
        s.outputGain = 1.0f;
        s.throatLength = harmonyOn ? 1.2f : 1.0f;   // exercise the LPC filter too

        if (harmonyOn)
        {
            s.harmony.anyEnabled = true;
            s.harmony.level = 0.7f;
            for (int v = 0; v < HarmonyEngine::maxVoices; ++v)
            {
                s.harmony.voices[(size_t) v].enabled = true;
                s.harmony.voices[(size_t) v].degrees = 2 + v;
                s.harmony.voices[(size_t) v].level = 0.7f;
            }
        }

        juce::AudioBuffer<float> source (2, total);
        fillTone (source, sr, 220.0);

        juce::AudioBuffer<float> block (2, blockSize);
        const auto start = juce::Time::getHighResolutionTicks();

        for (int pos = 0; pos + blockSize <= total; pos += blockSize)
        {
            for (int ch = 0; ch < 2; ++ch)
                block.copyFrom (ch, 0, source, ch, pos, blockSize);

            engine.process (block, s, (double) pos / sr, -1.0, nullptr, 0.0f, false, fifo);
        }

        const double seconds = juce::Time::highResolutionTicksToSeconds (
                                   juce::Time::getHighResolutionTicks() - start);
        const double audioSeconds = (double) total / sr;

        std::printf ("         %-28s %6.1fx real time  (%.1f%% of one core)\n",
                     label, audioSeconds / seconds, 100.0 * seconds / audioSeconds);
    };

    measure ("correction only", false);
    measure ("correction + 4 harmony", true);
}

/** Generic has to cover every voice without being told which one it is. */
static void testGenericInputType()
{
    std::printf ("\nGeneric input type (all ranges)\n");

    const auto all = rangeForInputType (InputType::generic);
    bool covers = true;

    for (int i = 0; i < numInputTypes; ++i)
    {
        const auto r = rangeForInputType ((InputType) i);
        covers = covers && all.minHz <= r.minHz && all.maxHz >= r.maxHz;
    }

    check (covers, "Generic spans every other input type's range",
           juce::String (all.minHz, 0) + " - " + juce::String (all.maxHz, 0) + " Hz");

    // From the floor of a bass voice to the top of a soprano's, one setting.
    const double sr = 44100.0;
    const double freqs[] = { 41.20, 65.41, 98.00, 196.00, 392.00, 783.99, 1318.51, 1975.53 };

    float worst = 0.0f;
    juce::String worstAt;

    for (double f : freqs)
    {
        juce::AudioBuffer<float> buf (1, 44100);
        fillTone (buf, sr, f);

        PitchDetector det;
        det.prepare (sr);
        det.setInputType (InputType::generic);
        det.setTracking (0.5f);

        const int frame = det.getFrameSize();
        double acc = 0.0;
        int count = 0;

        for (int pos = 0; pos + frame <= buf.getNumSamples(); pos += 512)
        {
            const auto r = det.process (buf.getReadPointer (0) + pos);
            if (r.voiced)
            {
                acc += r.frequencyHz;
                ++count;
            }
        }

        const float measured = count > 0 ? (float) (acc / count) : 0.0f;
        const float err = measured > 0.0f ? std::abs (centsBetween (measured, (float) f)) : 1200.0f;

        std::printf ("         %8.2f Hz -> %8.2f Hz  (%.2f cents, %d voiced frames)\n",
                     f, measured, err, count);

        if (err > worst)
        {
            worst = err;
            worstAt = juce::String (f, 2) + " Hz";
        }
    }

    check (worst < 2.0f, "Generic detects 41 Hz to 1.98 kHz without being told the range",
           "worst " + juce::String (worst, 2) + " cents at " + worstAt);

    // Detection alone is not the point - the whole chain has to correct a low
    // voice and a high one on the same setting.
    CorrectionEngine::Settings s;
    s.inputType = InputType::generic;
    s.tracking = 0.5f;
    s.key = 0;
    s.scaleIndex = 0;              // chromatic
    s.retune.retuneMs = 0.0f;
    s.mix = 1.0f;
    s.outputGain = 1.0f;

    struct Case { const char* name; double targetHz; };
    const Case cases[] = { { "G2", 98.00 }, { "A3", 220.00 }, { "C6", 1046.50 } };

    for (const auto& c : cases)
    {
        const double flat = c.targetHz * std::pow (2.0, -40.0 / 1200.0);
        float outHz = 0.0f, peak = 0.0f;
        runChain (sr, flat, s, outHz, peak);

        const float err = std::abs (centsBetween (outHz, (float) c.targetHz));
        check (err < 2.0f, juce::String ("Generic corrects ") + c.name + " sung 40 cents flat",
               juce::String (outHz, 2) + " Hz, " + juce::String (err, 2) + " cents from target");
    }

    // The cost, reported rather than asserted: Generic inherits the grain size
    // of the lowest range it covers.
    auto latencyFor = [&] (InputType t)
    {
        CorrectionEngine engine;
        engine.prepare (sr, 512, 1);

        PitchFifo fifo;
        CorrectionEngine::Settings ls;
        ls.inputType = t;

        juce::AudioBuffer<float> block (1, 512);
        block.clear();
        engine.process (block, ls, 0.0, -1.0, nullptr, 0.0f, false, fifo);

        return 1000.0 * engine.getLatencySamples() / sr;
    };

    std::printf ("         latency: Alto/Tenor %.1f ms, Generic %.1f ms\n",
                 latencyFor (InputType::altoTenor), latencyFor (InputType::generic));
}

/** The master switch has to mean *off*: with the bus disabled the output must
    be bit-identical to a chain that never had a harmony voice, and switching it
    back on must come in cleanly rather than with a burst of stale grains. */
static void testHarmonyMasterSwitch()
{
    std::printf ("\nHarmony master switch\n");

    const double sr = 44100.0;
    const int blockSize = 512;
    const int total = (int) (sr * 3.0);
    const int switchAt = ((int) (sr * 1.5) / blockSize) * blockSize;

    juce::AudioBuffer<float> source (1, total);
    fillTone (source, sr, 220.0);

    CorrectionEngine::Settings base;
    base.inputType = InputType::altoTenor;
    base.retune.retuneMs = 20.0f;
    base.mix = 1.0f;
    base.outputGain = 1.0f;
    base.harmony.level = 0.8f;

    for (int v = 0; v < HarmonyEngine::maxVoices; ++v)
    {
        auto& vp = base.harmony.voices[(size_t) v];
        vp.enabled = true;
        vp.degrees = 2 + 2 * v;
        vp.level = 0.7f;
    }

    // anyEnabled is exactly what the processor derives from the master switch.
    auto render = [&] (bool voicesSetUp, bool switchOnHalfway)
    {
        CorrectionEngine engine;
        engine.prepare (sr, blockSize, 1);
        PitchFifo fifo;

        auto s = base;
        if (! voicesSetUp)
            for (auto& vp : s.harmony.voices)
                vp.enabled = false;

        std::vector<float> out ((size_t) total, 0.0f);
        juce::AudioBuffer<float> block (1, blockSize);

        for (int pos = 0; pos + blockSize <= total; pos += blockSize)
        {
            s.harmony.anyEnabled = voicesSetUp && switchOnHalfway && pos >= switchAt;

            block.copyFrom (0, 0, source, 0, pos, blockSize);
            engine.process (block, s, (double) pos / sr, -1.0, nullptr, 0.0f, false, fifo);
            std::copy (block.getReadPointer (0), block.getReadPointer (0) + blockSize,
                       out.begin() + pos);
        }

        return out;
    };

    const auto leadOnly = render (false, false);
    const auto switched = render (true, true);

    double maxDiffBefore = 0.0;
    for (int i = 0; i < switchAt; ++i)
        maxDiffBefore = std::max (maxDiffBefore, (double) std::abs (switched[(size_t) i] - leadOnly[(size_t) i]));

    check (maxDiffBefore == 0.0, "bus off with four voices set up: output bit-identical to no harmony",
           "max difference " + juce::String (maxDiffBefore, 9));

    const int settleFrom = switchAt + (int) (sr * 0.3);
    const int settleTo = total - blockSize;
    double energy = 0.0;
    float settledPeak = 0.0f, transitionPeak = 0.0f;

    for (int i = settleFrom; i < settleTo; ++i)
    {
        const float d = switched[(size_t) i] - leadOnly[(size_t) i];
        energy += (double) d * d;
        settledPeak = juce::jmax (settledPeak, std::abs (d));
    }

    for (int i = switchAt; i < settleFrom; ++i)
        transitionPeak = juce::jmax (transitionPeak, std::abs (switched[(size_t) i] - leadOnly[(size_t) i]));

    const double harmonyRms = std::sqrt (energy / (double) (settleTo - settleFrom));

    check (harmonyRms > 0.02, "bus on: the voices are rendered",
           "harmony rms " + juce::String (harmonyRms, 4));

    check (transitionPeak <= settledPeak * 1.25f, "switching the bus on does not click",
           "transition peak " + juce::String (transitionPeak, 3)
               + " vs settled " + juce::String (settledPeak, 3));
}

/** Note Transition: the glide between notes has to take the musical length
    asked for at the host's tempo - that is the whole promise of the control. */
static void testNoteTransition()
{
    std::printf ("\nNote transition (tempo-locked)\n");

    check (std::abs (transitionSecondsFor (6, 120.0) - 0.5f) < 1.0e-6f
               && std::abs (transitionSecondsFor (2, 120.0) - 0.125f) < 1.0e-6f
               && std::abs (transitionSecondsFor (1, 90.0) - 1.0f / 9.0f) < 1.0e-6f,
           "note values convert at the host tempo",
           "1/4 @ 120 = 500 ms, 1/16 @ 120 = 125 ms, 1/16T @ 90 = 111 ms");

    check (transitionSecondsFor (0, 120.0) == 0.0f, "Off adds no glide");

    // An in-tune singer steps from A4 to C5, hard tune. Measure when the output
    // lands on the new note, and how closely the move follows its S-curve.
    const double sr = 44100.0;
    const int hop = 256;
    const float hopSeconds = (float) hop / (float) sr;
    const int jumpAt = 100;

    struct Result { float arriveSeconds; float curveError; };

    auto measure = [&] (int step, double bpm)
    {
        RetuneEngine re;
        re.prepare (sr, hop);

        ScaleQuantizer q;
        q.setKey (0);
        q.setScale (0);              // chromatic

        RetuneEngine::Params p;
        p.retuneMs = 0.0f;
        p.transitionSeconds = transitionSecondsFor (step, bpm);

        Result r { -1.0f, 0.0f };

        for (int i = 0; i < 600; ++i)
        {
            const float sung = i < jumpAt ? 69.0f : 72.0f;
            const auto out = re.process (sung, true, q, p);

            if (i < jumpAt)
                continue;

            // The engine advances the glide by a hop on the frame the note
            // changes, so frame i sits (i - jumpAt + 1) hops into the move.
            const float elapsed = (float) (i - jumpAt + 1) * hopSeconds;

            if (p.transitionSeconds > 0.0f && elapsed < p.transitionSeconds)
            {
                const float x = elapsed / p.transitionSeconds;
                const float expected = 69.0f + 3.0f * (x * x * (3.0f - 2.0f * x));
                r.curveError = juce::jmax (r.curveError, std::abs (out.outputMidi - expected));
            }

            if (r.arriveSeconds < 0.0f && std::abs (out.outputMidi - 72.0f) < 1.0e-3f)
                r.arriveSeconds = elapsed;
        }

        return r;
    };

    const auto off       = measure (0, 120.0);
    const auto sixteenth = measure (2, 120.0);
    const auto slow      = measure (2, 60.0);
    const auto quarter   = measure (6, 120.0);

    std::printf ("         lands: off %.1f ms | 1/16 @120 %.1f ms | 1/16 @60 %.1f ms | 1/4 @120 %.1f ms"
                 "  (analysis hop %.1f ms)\n",
                 off.arriveSeconds * 1000.0f, sixteenth.arriveSeconds * 1000.0f,
                 slow.arriveSeconds * 1000.0f, quarter.arriveSeconds * 1000.0f, hopSeconds * 1000.0f);

    check (off.arriveSeconds >= 0.0f && off.arriveSeconds <= hopSeconds + 1.0e-4f,
           "Off: the output steps to the new note at once",
           juce::String (off.arriveSeconds * 1000.0f, 1) + " ms");

    // Landing is judged to a tenth of a cent, and the eased tail of the curve
    // gets that close a fraction of a hop before the end - so "on time" means
    // within one analysis hop of the note length, either side.
    auto onTime = [&] (const Result& r, float seconds)
    {
        return std::abs (r.arriveSeconds - seconds) <= hopSeconds;
    };

    check (onTime (sixteenth, 0.125f), "1/16 at 120 BPM lands in 125 ms",
           juce::String (sixteenth.arriveSeconds * 1000.0f, 1) + " ms");
    check (onTime (quarter, 0.5f), "1/4 at 120 BPM lands in 500 ms",
           juce::String (quarter.arriveSeconds * 1000.0f, 1) + " ms");
    check (onTime (slow, 0.25f), "at 60 BPM the same 1/16 takes 250 ms",
           juce::String (slow.arriveSeconds * 1000.0f, 1) + " ms");
    check (sixteenth.curveError < 0.02f, "the move follows its S-curve at every hop",
           "max error " + juce::String (sixteenth.curveError * 100.0f, 2) + " cents");
}

/** Correction Amount scales how far the pitch is pulled, and nothing else. */
static void testCorrectionAmount()
{
    std::printf ("\nCorrection amount\n");

    const double sr = 44100.0;
    const double flatA = 440.0 * std::pow (2.0, -40.0 / 1200.0);   // 40 cents flat

    CorrectionEngine::Settings s;
    s.inputType = InputType::instrument;
    s.tracking = 0.5f;
    s.scaleIndex = 0;
    s.retune.retuneMs = 0.0f;
    s.mix = 1.0f;
    s.outputGain = 1.0f;

    for (float amount : { 1.0f, 0.5f, 0.0f })
    {
        s.correctionAmount = amount;

        float outHz = 0.0f, peak = 0.0f;
        runChain (sr, flatA, s, outHz, peak);

        const float cents = centsBetween (outHz, 440.0f);
        const float expected = -40.0f * (1.0f - amount);

        check (std::abs (cents - expected) < 2.0f,
               "amount " + juce::String ((int) (amount * 100.0f)) + "% leaves the note "
                   + juce::String (expected, 0) + " cents from A440",
               juce::String (cents, 2) + " cents");
    }

    // Transpose is a decision, not a correction: it must survive amount 0.
    s.correctionAmount = 0.0f;
    s.retune.transposeSemis = 12.0f;

    float outHz = 0.0f, peak = 0.0f;
    runChain (sr, flatA, s, outHz, peak);

    const float err = std::abs (centsBetween (outHz, (float) (flatA * 2.0)));
    check (err < 2.0f, "amount 0% still applies transpose",
           juce::String (outHz, 2) + " Hz vs " + juce::String (flatA * 2.0, 2) + " Hz");
}

int main()
{
    std::printf ("HELIX Tune - DSP verification\n");
    std::printf ("=============================\n");

    testDetectorAccuracy();
    testPitchLattice();
    testStabilizerHoldsOctave();
    testScaleQuantizer();
    testShifterDelayDrift();
    testShifterRatioAccuracy();
    testEndToEndCorrection();
    testGenericInputType();
    testNoteTransition();
    testCorrectionAmount();
    testHarmonyMasterSwitch();
    testDiatonicHarmony();
    testHarmonyRendering();
    testShifterOnRealisticVoice();
    testShifterQualityAcrossRatios();
    testHarmonyCleanliness();
    testHarmonySilentOnConsonants();
    testHarmonyGainStaging();
    testKeyDetection();
    testTransientGuard();
    testFormantProcessor();
    testStereoLinkingIsExact();
    testHighSampleRates();
    testHarmonyVoiceSwitching();
    testStability();
    testSilence();
    reportPerformance();

    std::printf ("\n=============================\n");
    std::printf ("%d checks, %d failed\n", checks, failures);

    return failures == 0 ? 0 : 1;
}
