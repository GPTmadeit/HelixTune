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
    testDiatonicHarmony();
    testHarmonyRendering();
    testKeyDetection();
    testTransientGuard();
    testFormantProcessor();
    testStability();
    testSilence();
    reportPerformance();

    std::printf ("\n=============================\n");
    std::printf ("%d checks, %d failed\n", checks, failures);

    return failures == 0 ? 0 : 1;
}
