#include "KeyDetector.h"
#include <cmath>
#include <algorithm>

namespace helix
{

// Krumhansl-Schmuckler probe-tone profiles.
static const float kMajorProfile[12] =
    { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };

static const float kMinorProfile[12] =
    { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };

void KeyDetector::prepare (double sampleRate, int hopSizeSamples)
{
    const double hopSeconds = (double) hopSizeSamples / sampleRate;

    // ~20 s memory. Long enough to hear a whole section, short enough that a
    // key change part-way through a song is picked up rather than averaged away.
    decayPerHop = (float) std::exp (-hopSeconds / 20.0);

    // Correlating 24 profiles is cheap, but there is no reason to do it every
    // 5 ms when the answer moves on the scale of seconds.
    recomputeInterval = juce::jmax (1, (int) (0.2 / hopSeconds));

    reset();
}

void KeyDetector::reset() noexcept
{
    chroma.fill (0.0f);
    totalWeight = 0.0f;
    hopsUntilRecompute = 0;

    pubValid.store (false, std::memory_order_relaxed);
    pubConfidence.store (0.0f, std::memory_order_relaxed);

    for (auto& c : pubChroma)
        c.store (0.0f, std::memory_order_relaxed);
}

void KeyDetector::push (float midiNote, bool voiced, float weight) noexcept
{
    for (auto& c : chroma)
        c *= decayPerHop;

    totalWeight *= decayPerHop;

    if (voiced && midiNote > 0.0f && std::isfinite (midiNote))
    {
        const int pc = ((int) std::lround (midiNote) % 12 + 12) % 12;
        const float w = juce::jlimit (0.0f, 1.0f, weight);

        chroma[(size_t) pc] += w;
        totalWeight += w;
    }

    if (--hopsUntilRecompute <= 0)
    {
        hopsUntilRecompute = recomputeInterval;
        recompute();
    }
}

void KeyDetector::recompute() noexcept
{
    // Needs a few seconds of actual singing before the histogram means anything.
    if (totalWeight < 20.0f)
    {
        pubValid.store (false, std::memory_order_relaxed);
        return;
    }

    float mean = 0.0f;
    for (float c : chroma)
        mean += c;

    mean /= 12.0f;

    float norm = 0.0f;
    float centred[12];
    for (int i = 0; i < 12; ++i)
    {
        centred[i] = chroma[(size_t) i] - mean;
        norm += centred[i] * centred[i];
    }

    norm = std::sqrt (norm);
    if (norm < 1.0e-6f)
    {
        pubValid.store (false, std::memory_order_relaxed);
        return;
    }

    auto correlate = [&] (const float* profile, int rotation)
    {
        float pMean = 0.0f;
        for (int i = 0; i < 12; ++i)
            pMean += profile[i];

        pMean /= 12.0f;

        float dot = 0.0f, pNorm = 0.0f;
        for (int i = 0; i < 12; ++i)
        {
            const float p = profile[(i - rotation + 12) % 12] - pMean;
            dot += centred[i] * p;
            pNorm += p * p;
        }

        pNorm = std::sqrt (pNorm);
        return (pNorm > 1.0e-6f) ? dot / (norm * pNorm) : 0.0f;
    };

    float bestScore = -2.0f, secondScore = -2.0f;
    int bestRoot = 0;
    bool bestMinor = false;

    for (int root = 0; root < 12; ++root)
    {
        for (int m = 0; m < 2; ++m)
        {
            const float score = correlate (m == 0 ? kMajorProfile : kMinorProfile, root);

            if (score > bestScore)
            {
                secondScore = bestScore;
                bestScore = score;
                bestRoot = root;
                bestMinor = (m == 1);
            }
            else if (score > secondScore)
            {
                secondScore = score;
            }
        }
    }

    // Confidence is the margin over the runner-up, not the absolute
    // correlation: a vocal that fits C major well also fits A minor well, and
    // what matters is how cleanly the winner separates.
    const float margin = juce::jlimit (0.0f, 1.0f, (bestScore - secondScore) * 4.0f);

    pubRoot.store (bestRoot, std::memory_order_relaxed);
    pubMinor.store (bestMinor, std::memory_order_relaxed);
    pubConfidence.store (margin * juce::jlimit (0.0f, 1.0f, bestScore), std::memory_order_relaxed);
    pubValid.store (bestScore > 0.2f, std::memory_order_relaxed);

    for (int i = 0; i < 12; ++i)
        pubChroma[(size_t) i].store (chroma[(size_t) i] / juce::jmax (1.0e-6f, totalWeight),
                                     std::memory_order_relaxed);
}

KeyDetector::Result KeyDetector::getEstimate() const noexcept
{
    Result r;
    r.rootPitchClass = pubRoot.load (std::memory_order_relaxed);
    r.minor = pubMinor.load (std::memory_order_relaxed);
    r.confidence = pubConfidence.load (std::memory_order_relaxed);
    r.valid = pubValid.load (std::memory_order_relaxed);
    return r;
}

float KeyDetector::getChroma (int pitchClass) const noexcept
{
    const int pc = ((pitchClass % 12) + 12) % 12;
    return pubChroma[(size_t) pc].load (std::memory_order_relaxed);
}

} // namespace helix
