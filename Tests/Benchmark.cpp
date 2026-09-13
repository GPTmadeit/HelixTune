/*  Where the audio thread's time goes.

    Average CPU is the wrong number for a plugin. A host gives every buffer a
    hard deadline, and one buffer that misses it is an audible pop no matter how
    idle the other thousand were. So this reports the worst buffers alongside the
    average, per block size, because small buffers are where an analysis hop that
    lands in one callback shows up as a spike.

    Built with the plugin's own optimisation flags, so the numbers transfer.
*/

#include <juce_audio_basics/juce_audio_basics.h>
#include "DSP/CorrectionEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace helix;
using Clock = std::chrono::steady_clock;

namespace
{

/** Vibrato, jitter, shimmer, formant-shaped harmonics and breath noise - the
    same material the DSP test uses, because a steady tone flatters the mark
    search and the tracker alike. */
void fillVoiceLike (float* out, int n, double sr, double f0, float noiseDb, int seed)
{
    juce::Random rng (seed);

    double phase = 0.0, jitter = 0.0;
    const float noiseGain = std::pow (10.0f, noiseDb / 20.0f);

    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / sr;

        jitter += (rng.nextDouble() * 2.0 - 1.0) * 0.0006;
        jitter = juce::jlimit (-0.01, 0.01, jitter);

        const double vib = 0.25 * std::sin (2.0 * juce::MathConstants<double>::pi * 5.0 * t) / 12.0;
        const double f = f0 * std::pow (2.0, vib) * (1.0 + jitter);

        phase += 2.0 * juce::MathConstants<double>::pi * f / sr;

        double v = 0.0;
        for (int h = 1; h <= 24; ++h)
        {
            const double hf = f * h;
            if (hf > sr * 0.45) break;

            const double f1 = std::exp (-std::pow ((hf - 700.0) / 420.0, 2.0));
            const double f2 = std::exp (-std::pow ((hf - 2300.0) / 700.0, 2.0)) * 0.55;
            v += std::sin (phase * h) * (f1 + f2 + 0.05) / (double) h;
        }

        const double shimmer = 1.0 + 0.06 * std::sin (2.0 * juce::MathConstants<double>::pi * 4.3 * t);
        out[i] = (float) (v * 0.22 * shimmer) + (rng.nextFloat() * 2.0f - 1.0f) * noiseGain;
    }
}

double microseconds (Clock::duration d)
{
    return std::chrono::duration<double, std::micro> (d).count();
}

struct Scenario
{
    const char* name;
    InputType   type;
    float       throat;
    int         voices;
    bool        trueStereo;
};

void configure (CorrectionEngine::Settings& s, const Scenario& sc)
{
    s.inputType = sc.type;
    s.key = 0;
    s.scaleIndex = 1;
    s.retune.retuneMs = 20.0f;
    s.throatLength = sc.throat;
    s.mix = 1.0f;
    s.outputGain = 1.0f;

    const int   degrees[] = { 2, 4, -7, 7 };
    const float formant[] = { 1.06f, 0.94f, 1.12f, 0.9f };
    const float pan[]     = { -0.55f, 0.55f, -0.2f, 0.25f };

    s.harmony.level = 0.7f;
    s.harmony.anyEnabled = sc.voices > 0;

    for (int v = 0; v < HarmonyEngine::maxVoices; ++v)
    {
        auto& hv = s.harmony.voices[(size_t) v];
        hv.enabled = v < sc.voices;
        hv.degrees = degrees[v];
        hv.formant = formant[v];
        hv.pan = pan[v];
        hv.level = 0.75f;
        hv.detuneCents = 4.0f;
        hv.delayMs = 0.35f * (6.0f + 5.0f * (float) v);
    }
}

struct BlockStats { double meanPct, p99Pct, maxPct; };

BlockStats runScenario (const Scenario& sc, double sr, int blockSize, const std::vector<float>& left,
                        const std::vector<float>& right)
{
    CorrectionEngine engine;
    engine.prepare (sr, blockSize, 2);

    CorrectionEngine::Settings s;
    configure (s, sc);

    PitchFifo fifo;
    std::vector<PitchFrame> drain;

    juce::AudioBuffer<float> block (2, blockSize);
    const int total = (int) left.size();
    const int warmup = (int) sr;           // first second excluded: priming, cache warm-up

    std::vector<double> times;
    times.reserve ((size_t) (total / blockSize) + 1);

    double busy = 0.0;
    int measuredSamples = 0;

    for (int pos = 0; pos + blockSize <= total; pos += blockSize)
    {
        std::copy (left.begin() + pos,  left.begin() + pos + blockSize,  block.getWritePointer (0));
        std::copy (right.begin() + pos, right.begin() + pos + blockSize, block.getWritePointer (1));

        const auto t0 = Clock::now();
        engine.process (block, s, (double) pos / sr, -1.0, nullptr, 0.0f, false, fifo);
        const auto t1 = Clock::now();

        // The editor would drain this; left alone it just wraps, which is fine,
        // but draining keeps the memory access pattern honest.
        if ((pos / blockSize) % 16 == 0)
        {
            drain.clear();
            fifo.drain (drain);
        }

        if (pos < warmup)
            continue;

        const double us = microseconds (t1 - t0);
        times.push_back (us);
        busy += us;
        measuredSamples += blockSize;
    }

    const double budgetUs = 1.0e6 * (double) blockSize / sr;
    std::sort (times.begin(), times.end());

    BlockStats st;
    st.meanPct = 100.0 * (busy * 1.0e-6) / ((double) measuredSamples / sr);
    st.p99Pct  = 100.0 * times[(size_t) ((double) (times.size() - 1) * 0.99)] / budgetUs;
    st.maxPct  = 100.0 * times.back() / budgetUs;
    return st;
}

void reportScenarios (double sr)
{
    std::printf ("\nWhole engine, stereo @ %.0f Hz  (mean = share of one core; p99/max = share of each buffer's deadline)\n", sr);
    std::printf ("  %-34s", "");
    for (int bs : { 64, 256, 512 })
        std::printf ("   ---- %4d samples ----", bs);
    std::printf ("\n  %-34s", "scenario");
    for (int i = 0; i < 3; ++i)
        std::printf ("    mean    p99    max  ");
    std::printf ("\n");

    const int total = (int) (sr * 9.0);
    std::vector<float> mono ((size_t) total), other ((size_t) total);
    fillVoiceLike (mono.data(), total, sr, 180.0, -40.0f, 777);
    fillVoiceLike (other.data(), total, sr, 180.0, -40.0f, 4242);   // same note, different noise

    const Scenario scenarios[] =
    {
        { "correction, Alto/Tenor",          InputType::altoTenor, 1.0f, 0, false },
        { "correction, Generic",             InputType::generic,   1.0f, 0, false },
        { "+ throat 1.2 (formant filter)",   InputType::altoTenor, 1.2f, 0, false },
        { "+ harmony, 1 voice",              InputType::altoTenor, 1.2f, 1, false },
        { "+ harmony, 2 voices",             InputType::altoTenor, 1.2f, 2, false },
        { "+ harmony, 4 voices",             InputType::altoTenor, 1.2f, 4, false },
        { "4 voices, Generic",               InputType::generic,   1.2f, 4, false },
        { "4 voices, true stereo input",     InputType::altoTenor, 1.2f, 4, true  },
    };

    for (const auto& sc : scenarios)
    {
        std::printf ("  %-34s", sc.name);

        for (int bs : { 64, 256, 512 })
        {
            const auto st = runScenario (sc, sr, bs, mono, sc.trueStereo ? other : mono);
            std::printf ("  %5.1f%% %5.0f%% %5.0f%%  ", st.meanPct, st.p99Pct, st.maxPct);
        }

        std::printf ("\n");
        std::fflush (stdout);
    }
}

/** Times a callable enough times to be stable, returns microseconds per call. */
template <typename Fn>
double timePerCall (Fn&& fn, int minIterations = 200, double minSeconds = 0.25)
{
    int iterations = 0;
    const auto start = Clock::now();

    while (iterations < minIterations
           || std::chrono::duration<double> (Clock::now() - start).count() < minSeconds)
    {
        fn();
        ++iterations;
    }

    return microseconds (Clock::now() - start) / (double) iterations;
}

void reportComponents()
{
    const double sr = 44100.0;
    const int hop = 256;
    const double hopsPerSecond = sr / hop;

    std::printf ("\nComponents @ 44.1 kHz  (%% = share of one core in continuous use)\n");

    const int total = (int) (sr * 4.0);
    std::vector<float> voice ((size_t) total);
    fillVoiceLike (voice.data(), total, sr, 180.0, -40.0f, 777);

    // --- detector, per input type -----------------------------------------
    const char* typeNames[] = { "Soprano", "Alto/Tenor", "Low Male", "Instrument", "Bass Inst.", "Generic" };

    for (int t = 0; t < numInputTypes; ++t)
    {
        PitchDetector det;
        det.prepare (sr);
        det.setInputType ((InputType) t);

        const int frame = det.getFrameSize();
        int offset = (int) sr;

        const double us = timePerCall ([&]
        {
            det.process (voice.data() + offset);
            offset += hop;
            if (offset + frame >= total) offset = (int) sr;
        });

        std::printf ("  pitch detector, %-11s %7.1f us/hop   %5.2f%%   (frame %d)\n",
                     t < 6 ? typeNames[t] : "?", us, 100.0 * us * hopsPerSecond * 1.0e-6, frame);
    }

    // --- formant ------------------------------------------------------------
    for (float ratio : { 1.0f, 0.83f })
    {
        FormantProcessor fp;
        fp.prepare (sr, 512, 2);
        fp.setRatio (ratio);

        int offset = (int) sr;
        const double analyseUs = timePerCall ([&]
        {
            fp.analyse (voice.data() + offset, 2048);
            offset += hop;
            if (offset + 2048 >= total) offset = (int) sr;
        });

        std::vector<float> buf (512);
        int pos = (int) sr;
        const double processUs = timePerCall ([&]
        {
            std::copy (voice.begin() + pos, voice.begin() + pos + 512, buf.begin());
            fp.process (buf.data(), 512, 0);
            pos += 512;
            if (pos + 512 >= total) pos = (int) sr;
        });

        std::printf ("  formant ratio %.2f: analyse %6.1f us/hop (%5.2f%%), filter %6.1f ns/sample per channel (%5.2f%%)\n",
                     ratio, analyseUs, 100.0 * analyseUs * hopsPerSecond * 1.0e-6,
                     processUs * 1000.0 / 512.0, 100.0 * processUs * (sr / 512.0) * 1.0e-6);
    }

    // --- shifter --------------------------------------------------------------
    for (double f0 : { 330.0, 180.0, 110.0 })
    {
        std::vector<float> src ((size_t) total);
        fillVoiceLike (src.data(), total, sr, f0, -40.0f, 99);

        for (float formantRatio : { 1.0f, 1.06f })
        {
            PsolaShifter sh;
            sh.prepare (sr, 32.0f, 512);
            sh.setMinFrequency (90.0f);

            std::vector<float> out (512);
            int pos = 0;
            const float period = (float) (sr / f0);

            // Prime past the latency so the timing covers real grain work.
            for (; pos + 512 < (int) sr; pos += 512)
                sh.process (src.data() + pos, out.data(), 512, 1.189f, formantRatio, period, true);

            const double us = timePerCall ([&]
            {
                sh.process (src.data() + pos, out.data(), 512, 1.189f, formantRatio, period, true);
                pos += 512;
                if (pos + 512 >= total) pos = (int) sr;
            });

            std::printf ("  PSOLA shifter, %3.0f Hz voice, formant %.2f: %6.1f ns/sample   %5.2f%%\n",
                         f0, formantRatio, us * 1000.0 / 512.0, 100.0 * us * (sr / 512.0) * 1.0e-6);
        }
    }
}

} // namespace

int main (int argc, char** argv)
{
    const bool quick = argc > 1 && juce::String (argv[1]) == "--quick";

    std::printf ("HELIX Tune - performance\n");
    std::printf ("========================\n");

    reportComponents();
    reportScenarios (44100.0);

    if (! quick)
    {
        reportScenarios (48000.0);
        reportScenarios (96000.0);
    }

    return 0;
}
