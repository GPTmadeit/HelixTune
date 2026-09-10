#include "PsolaShifter.h"
#include <cmath>
#include <algorithm>

namespace helix
{

static inline int64_t nextPow2 (int64_t v)
{
    int64_t p = 1;
    while (p < v) p <<= 1;
    return p;
}

void PsolaShifter::setMinFrequency (float hz) noexcept
{
    const int wanted = (int) std::ceil (fs / (double) juce::jmax (20.0f, hz));
    maxPeriod = juce::jlimit (16, capacityPeriod, wanted);

    // A grain centred on a synthesis mark reads input ahead of that mark, and
    // formant scaling widens the read by up to 2x. Three periods covers the
    // worst case; the tail is slack so the emit loop never clips a grain.
    latency = 3 * maxPeriod + 8;
    reset();
}

void PsolaShifter::prepare (double sampleRate, float lowestSupportedHz, int maxBlockSize)
{
    fs = sampleRate;
    capacityPeriod = (int) std::ceil (sampleRate / (double) juce::jmax (20.0f, lowestSupportedHz));
    maxPeriod = capacityPeriod;
    latency = 3 * maxPeriod + 8;

    const int64_t inSize  = nextPow2 (8 * (int64_t) capacityPeriod + 4 * maxBlockSize + 16);
    const int64_t outSize = nextPow2 (8 * (int64_t) capacityPeriod + 4 * maxBlockSize + 16);

    inBuf.assign ((size_t) inSize, 0.0f);
    inMask = inSize - 1;

    outAccum.assign ((size_t) outSize, 0.0f);
    winAccum.assign ((size_t) outSize, 0.0f);
    outMask = outSize - 1;

    corrScores.assign ((size_t) (2 * (capacityPeriod / 4 + 2) + 1), 0.0);

    windowSize = 2048;
    window.resize ((size_t) windowSize + 1);
    for (int i = 0; i <= windowSize; ++i)
    {
        const double t = (double) i / (double) windowSize;
        window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * t));
    }

    reset();
}

void PsolaShifter::reset() noexcept
{
    std::fill (inBuf.begin(), inBuf.end(), 0.0f);
    std::fill (outAccum.begin(), outAccum.end(), 0.0f);
    std::fill (winAccum.begin(), winAccum.end(), 0.0f);

    inWritePos = 0;
    analysisPos = synthPos = 0.0;
    primed = false;
    lastPeriod = (float) maxPeriod * 0.4f;
    voicedGain = 0.0f;
}

float PsolaShifter::readInput (double absPos) const noexcept
{
    // Keep the 4-point kernel inside the valid span of the ring.
    const double lo = (double) (inWritePos - (int64_t) inBuf.size() + 4);
    const double hi = (double) (inWritePos - 3);
    const double p  = juce::jlimit (lo, hi, absPos);

    const int64_t i = (int64_t) std::floor (p);
    const float   f = (float) (p - (double) i);

    const float xm1 = inBuf[(size_t) wrapIn (i - 1)];
    const float x0  = inBuf[(size_t) wrapIn (i)];
    const float x1  = inBuf[(size_t) wrapIn (i + 1)];
    const float x2  = inBuf[(size_t) wrapIn (i + 2)];

    // Catmull-Rom. Linear interpolation here is audible as a dull, gritty
    // top end once grains are being resampled every period.
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);

    return ((c3 * f + c2) * f + c1) * f + x0;
}

double PsolaShifter::refineMark (double predicted, float period) noexcept
{
    // Predicting the next mark as "previous + one period" accumulates phase
    // error; a normalised cross-correlation against the previous grain snaps
    // each mark back onto the same point of the glottal cycle. Marks that
    // drift produce the metallic buzz that gives cheap PSOLA away.
    //
    // The correlation is evaluated on the integer sample grid, so its peak must
    // be interpolated and the result treated as a measured *lag* from the
    // previous mark - never as an absolute position. Snapping the mark to the
    // integer grid instead throws away the fractional part of the period; that
    // loss accumulates into a creeping read offset, and a creeping delay is a
    // pitch shift. At a 200.45-sample period it detunes the output by a
    // constant -3.9 cents at every ratio, including 1.0.
    const int corrLen = juce::jlimit (32, 512, (int) period);
    const int search  = juce::jmax (1, (int) (period * 0.25f));
    const int half    = corrLen / 2;

    const int64_t refBase  = (int64_t) std::llround (analysisPos);
    const int64_t candBase = (int64_t) std::llround (predicted);

    // Everything the search touches must already be in the ring.
    const int64_t highest = candBase + search + half + 2;
    if (highest >= inWritePos)
        return predicted;

    const int count = 2 * search + 1;
    if ((int) corrScores.size() < count)
        return predicted;

    double bestScore = -1.0e30;
    int    bestIndex = 0;

    for (int i = 0; i < count; ++i)
    {
        const int d = i - search;
        double dot = 0.0, energy = 1.0e-12;

        for (int j = -half; j < half; ++j)
        {
            const float a = inBuf[(size_t) wrapIn (refBase + j)];
            const float b = inBuf[(size_t) wrapIn (candBase + d + j)];
            dot    += (double) a * b;
            energy += (double) b * b;
        }

        const double score = dot / std::sqrt (energy);
        corrScores[(size_t) i] = score;

        if (score > bestScore)
        {
            bestScore = score;
            bestIndex = i;
        }
    }

    // Parabolic interpolation of the correlation maximum, for sub-sample lag.
    double sub = 0.0;
    if (bestIndex > 0 && bestIndex < count - 1)
    {
        const double s0 = corrScores[(size_t) (bestIndex - 1)];
        const double s1 = corrScores[(size_t) bestIndex];
        const double s2 = corrScores[(size_t) (bestIndex + 1)];
        const double denom = s0 - 2.0 * s1 + s2;

        if (std::abs (denom) > 1.0e-15)
            sub = juce::jlimit (-1.0, 1.0, 0.5 * (s0 - s2) / denom);
    }

    // Lag measured from the previous mark, then applied to the previous mark's
    // own fractional position. Both fractions survive.
    const double lag = ((double) (candBase + bestIndex - search) + sub) - (double) refBase;

    return analysisPos + lag;
}

void PsolaShifter::emitGrain (double analysisMark, double synthMark,
                              float period, float formantRatio) noexcept
{
    const int L = juce::jmax (2, (int) std::lround (period));

    const int64_t base = (int64_t) std::llround (synthMark);
    const float   frac = (float) (synthMark - (double) base);

    const float invSpan = 1.0f / (float) (2 * L);
    const float wScale  = (float) windowSize;

    for (int k = -L; k <= L; ++k)
    {
        const float kp = (float) k - frac;          // offset from the true mark

        float u = (kp + (float) L) * invSpan;
        if (u <= 0.0f || u >= 1.0f)
            continue;

        const float wIdx = u * wScale;
        const int   wi   = (int) wIdx;
        const float wf   = wIdx - (float) wi;
        const float w    = window[(size_t) wi] + wf * (window[(size_t) (wi + 1)] - window[(size_t) wi]);

        const float s = readInput (analysisMark + (double) (kp * formantRatio));

        const int o = wrapOut (base + k);
        outAccum[(size_t) o] += w * s;
        winAccum[(size_t) o] += w;
    }
}

void PsolaShifter::process (const float* input, float* output, int numSamples,
                            float pitchRatio, float formantRatio,
                            float periodSamples, bool voiced) noexcept
{
    pitchRatio   = juce::jlimit (0.25f, 4.0f, pitchRatio);
    formantRatio = juce::jlimit (0.5f,  2.0f, formantRatio);

    if (voiced && periodSamples > 1.0f)
        lastPeriod = juce::jlimit (16.0f, (float) maxPeriod, periodSamples);

    const float period = lastPeriod;

    for (int i = 0; i < numSamples; ++i)
        inBuf[(size_t) wrapIn (inWritePos + i)] = input[i];

    inWritePos += numSamples;

    const int64_t outStart = inWritePos - numSamples - latency;

    if (outStart < 0)
    {
        juce::FloatVectorOperations::clear (output, numSamples);
        return;
    }

    if (! primed)
    {
        analysisPos = synthPos = (double) outStart;
        primed = true;
    }

    // A stall (host transport jump, long unvoiced stretch) can leave the marks
    // stranded. Snap them back rather than grinding through thousands of
    // catch-up grains.
    if (std::abs (synthPos - (double) outStart) > (double) (8 * maxPeriod))
        analysisPos = synthPos = (double) outStart;

    if (std::abs (analysisPos - synthPos) > (double) (4 * maxPeriod))
        analysisPos = synthPos;

    const int    L         = juce::jmax (2, (int) std::lround (period));
    const double emitUntil = (double) (outStart + numSamples + L);
    const double synthStep = (double) period / pitchRatio;

    int guard = 0;
    while (synthPos < emitUntil && guard++ < 4096)
    {
        // Track the analysis mark that sits closest to this synthesis mark.
        // For pitchRatio > 1 the synthesis marks are denser, so a mark gets
        // reused; for < 1 marks are skipped. That asymmetry *is* the shift.
        while (std::abs (analysisPos + (double) period - synthPos) < std::abs (analysisPos - synthPos))
        {
            const double next = refineMark (analysisPos + (double) period, period);
            if (next <= analysisPos)
                break;

            analysisPos = next;
        }

        emitGrain (analysisPos, synthPos, period, formantRatio);
        synthPos += synthStep;
    }

    // Normalising by the summed window makes the overlap-add gain-flat for any
    // grain spacing, so the output level does not swing with the shift amount.
    const float target = voiced ? 1.0f : 0.0f;
    const float coeff  = 1.0f - std::exp (-1.0f / (float) (fs * 0.010));

    for (int i = 0; i < numSamples; ++i)
    {
        const int64_t p = outStart + i;
        const int     o = wrapOut (p);

        const float w   = winAccum[(size_t) o];
        const float wet = (w > 1.0e-4f) ? (outAccum[(size_t) o] / w) : 0.0f;
        const float dry = inBuf[(size_t) wrapIn (p)];

        outAccum[(size_t) o] = 0.0f;
        winAccum[(size_t) o] = 0.0f;

        voicedGain += (target - voicedGain) * coeff;

        output[i] = passDryWhenUnvoiced ? (wet * voicedGain + dry * (1.0f - voicedGain))
                                        : (wet * voicedGain);
    }
}

} // namespace helix
