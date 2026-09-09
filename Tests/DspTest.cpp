/*  Offline checks for the correction chain.

    pluginval proves the plugin does not crash or emit NaNs. It says nothing
    about whether the thing actually tunes anything, which is the only property
    that matters here. These tests measure real numbers off real signals.
*/

#include <juce_audio_basics/juce_audio_basics.h>
#include "DSP/CorrectionEngine.h"
#include "DSP/PitchDetector.h"
#include "DSP/ScaleQuantizer.h"

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

int main()
{
    std::printf ("HELIX Tune - DSP verification\n");
    std::printf ("=============================\n");

    testDetectorAccuracy();
    testScaleQuantizer();
    testShifterDelayDrift();
    testShifterRatioAccuracy();
    testEndToEndCorrection();
    testStability();
    testSilence();

    std::printf ("\n=============================\n");
    std::printf ("%d checks, %d failed\n", checks, failures);

    return failures == 0 ? 0 : 1;
}
