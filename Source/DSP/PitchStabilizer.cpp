#include "PitchStabilizer.h"
#include <cmath>
#include <algorithm>

namespace helix
{

void PitchStabilizer::prepare (double sampleRate, int hopSizeSamples)
{
    fs = sampleRate;
    hopSeconds = (float) ((double) hopSizeSamples / sampleRate);
    reset();
}

void PitchStabilizer::reset() noexcept
{
    prevCount = 0;
    unvoicedCost = 0.0f;
    wasVoiced = false;
    lastFreq = 0.0f;
    voicedRun = unvoicedRun = 0;

    std::fill (std::begin (prevFreq), std::end (prevFreq), 0.0f);
    std::fill (std::begin (prevCost), std::end (prevCost), 0.0f);
}

PitchStabilizer::Result PitchStabilizer::process (const PitchDetector::Result& det,
                                                  FrequencyRange range) noexcept
{
    Result out;

    // Transition penalty per octave of movement. Higher smoothing makes the
    // tracker more stubborn about leaving the pitch it is already on.
    const float jumpPenalty = juce::jmap (smoothing, 0.0f, 1.0f, 0.25f, 1.4f);

    // Cost of declaring the frame unpitched. Permissive tracking lowers it, so
    // breathy material still gets tracked instead of dropping out.
    const float silence = juce::jlimit (0.0f, 1.0f, det.rms * 60.0f);
    const float unvoicedEmission = juce::jmap (tracking, 0.0f, 1.0f, 0.42f, 0.62f) * silence
                                 + (1.0f - silence) * 0.02f;

    float curFreq[maxStates] { };
    float curCost[maxStates] { };
    float curEmission[maxStates] { };
    int   curCount = 0;

    for (int i = 0; i < det.numCandidates && curCount < maxStates; ++i)
    {
        const auto& c = det.candidates[(size_t) i];

        if (c.frequencyHz < range.minHz || c.frequencyHz > range.maxHz)
            continue;

        float best = c.cost + unvoicedCost + 0.30f;   // arriving from unvoiced: a note onset

        for (int j = 0; j < prevCount; ++j)
        {
            const float octaves = std::abs (std::log2 (c.frequencyHz / prevFreq[j]));

            // Sub-linear in the distance: a semitone of vibrato is nearly free,
            // an octave is costly, but a genuine large leap is not impossible.
            const float move = jumpPenalty * std::sqrt (octaves * 4.0f);
            const float total = prevCost[j] + move + c.cost;

            best = std::min (best, total);
        }

        curFreq[curCount] = c.frequencyHz;
        curCost[curCount] = best;
        curEmission[curCount] = c.cost;
        ++curCount;
    }

    // The unvoiced state can be reached from anywhere at a fixed exit cost.
    float bestVoicedPrev = 1.0e30f;
    for (int j = 0; j < prevCount; ++j)
        bestVoicedPrev = std::min (bestVoicedPrev, prevCost[j]);

    float newUnvoiced = unvoicedCost + unvoicedEmission;
    if (prevCount > 0)
        newUnvoiced = std::min (newUnvoiced, bestVoicedPrev + unvoicedEmission + 0.30f);

    // Pick the winner for this frame.
    int bestState = -1;
    float bestScore = newUnvoiced;

    for (int i = 0; i < curCount; ++i)
    {
        if (curCost[i] < bestScore)
        {
            bestScore = curCost[i];
            bestState = i;
        }
    }

    // Renormalise so accumulated costs cannot drift toward infinity over a
    // long take. Only differences matter.
    const float floorCost = bestScore;
    for (int i = 0; i < curCount; ++i)
        curCost[i] -= floorCost;

    newUnvoiced -= floorCost;

    const float chosenEmission = (bestState >= 0) ? curEmission[bestState] : 1.0f;

    std::copy (curFreq, curFreq + curCount, prevFreq);
    std::copy (curCost, curCost + curCount, prevCost);
    prevCount = curCount;
    unvoicedCost = newUnvoiced;

    if (bestState < 0)
    {
        unvoicedRun = std::min (unvoicedRun + 1, 1000);
        voicedRun = 0;

        // Brief dropouts inside a sustained note are almost always the tracker
        // stumbling on a consonant, not the singer stopping. Hold the pitch
        // across them rather than releasing the correction.
        if (wasVoiced && unvoicedRun < 6 && lastFreq > 0.0f)
        {
            out.frequencyHz = lastFreq;
            out.midiNote = 69.0f + 12.0f * std::log2 (lastFreq / 440.0f);
            out.confidence = 0.25f;
            out.voiced = true;
            return out;
        }

        wasVoiced = false;
        return out;
    }

    unvoicedRun = 0;
    voicedRun = std::min (voicedRun + 1, 1000);

    out.frequencyHz = curFreq[bestState];
    out.midiNote = 69.0f + 12.0f * std::log2 (out.frequencyHz / 440.0f);
    // Confidence describes the candidate that won, not whichever one happened
    // to be listed first - they are in discovery order, not rank order.
    out.confidence = juce::jlimit (0.0f, 1.0f, 1.0f - chosenEmission);
    out.voiced = true;

    wasVoiced = true;
    lastFreq = out.frequencyHz;
    return out;
}

} // namespace helix
