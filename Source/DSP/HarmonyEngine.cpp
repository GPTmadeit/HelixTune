#include "HarmonyEngine.h"
#include <cmath>
#include <algorithm>

namespace helix
{

void HarmonyEngine::prepare (double sampleRate, int maxBlockSize, float lowestSupportedHz)
{
    fs = sampleRate;

    // 60 ms of timing offset per voice, plus room for the lead-path alignment.
    maxDelaySamples = (int) (sampleRate * 0.070) + 256;

    for (auto& v : voices)
    {
        v.shifter.prepare (sampleRate, lowestSupportedHz, maxBlockSize);

        // A harmony voice falls silent on consonants instead of echoing the
        // lead. Four delayed copies of the dry input is what made stacked
        // voices sound gritty.
        v.shifter.setPassDryWhenUnvoiced (false);
        v.scratch.assign ((size_t) juce::jmax (1, maxBlockSize), 0.0f);
        v.delayLine.assign ((size_t) maxDelaySamples, 0.0f);
        v.delayPos = 0;
        v.delaySamples = 0;
    }

    reset();
}

void HarmonyEngine::reset() noexcept
{
    for (auto& v : voices)
    {
        v.shifter.reset();
        std::fill (v.delayLine.begin(), v.delayLine.end(), 0.0f);
        v.delayPos = 0;
        v.pitchRatio = 1.0f;
        v.formantRatio = 1.0f;
        v.smoothedGainL = v.smoothedGainR = 0.0f;
        v.running = false;
    }

    lastPeriod = 200.0f;
    lastVoiced = false;
}

void HarmonyEngine::setRange (float minHz) noexcept
{
    for (auto& v : voices)
        v.shifter.setMinFrequency (minHz);
}

void HarmonyEngine::setAlignmentDelay (int samples) noexcept
{
    alignmentDelay = juce::jlimit (0, maxDelaySamples - 1, samples);
}

void HarmonyEngine::updateTargets (float leadMidi, float detectedMidi, bool voiced,
                                   const ScaleQuantizer& scale, const Params& p) noexcept
{
    lastVoiced = voiced;

    if (voiced && detectedMidi > 0.0f)
        lastPeriod = (float) (fs / (440.0 * std::pow (2.0, (detectedMidi - 69.0) / 12.0)));

    for (int i = 0; i < maxVoices; ++i)
    {
        auto& v = voices[(size_t) i];
        const auto& vp = p.voices[(size_t) i];

        if (! vp.enabled || ! voiced)
        {
            v.gainL = v.gainR = 0.0f;
            continue;
        }

        const float target = scale.transposeByScaleDegrees (leadMidi, vp.degrees)
                           + vp.detuneCents * 0.01f;

        v.pitchRatio = std::pow (2.0f, (target - detectedMidi) / 12.0f);
        v.formantRatio = juce::jlimit (0.5f, 2.0f, vp.formant);

        // Constant-power pan, so a voice swept across the image does not dip
        // in level through the centre.
        const float angle = (juce::jlimit (-1.0f, 1.0f, vp.pan) + 1.0f) * 0.25f
                          * juce::MathConstants<float>::pi;

        const float g = vp.level * p.level * gate;
        v.gainL = g * std::cos (angle);
        v.gainR = g * std::sin (angle);

        v.delaySamples = juce::jlimit (0, maxDelaySamples - 1,
                                       alignmentDelay + (int) (vp.delayMs * 0.001f * fs));
    }
}

void HarmonyEngine::process (const float* monoInput, float* left, float* right,
                             int numSamples, const Params& p) noexcept
{
    if (! p.anyEnabled || numSamples <= 0)
        return;

    // Asymmetric: ease in, but get out of the way quickly. A slow release
    // leaves the voice audible well into a consonant.
    const float attack  = 1.0f - std::exp (-1.0f / (float) (fs * 0.025));
    const float release = 1.0f - std::exp (-1.0f / (float) (fs * 0.005));

    for (int i = 0; i < maxVoices; ++i)
    {
        auto& v = voices[(size_t) i];
        const auto& vp = p.voices[(size_t) i];

        const bool releasing = v.running && (v.smoothedGainL > 1.0e-4f || v.smoothedGainR > 1.0e-4f);

        // A voice that is off, and has finished fading out, costs nothing.
        // Idle voices used to keep their shifters running so their mark
        // tracking stayed current, which made one voice cost exactly as much
        // as four. Starting from a clean reset instead costs one fade-in, and
        // the gain smoothing already provides that.
        if (! vp.enabled && ! releasing)
        {
            v.running = false;
            v.smoothedGainL = v.smoothedGainR = 0.0f;
            continue;
        }

        if (! v.running)
        {
            v.shifter.reset();
            std::fill (v.delayLine.begin(), v.delayLine.end(), 0.0f);
            v.delayPos = 0;
            v.smoothedGainL = v.smoothedGainR = 0.0f;
            v.running = true;
        }

        // Unvoiced, the targets are zero and there is nothing left to fade, but
        // the shifter and the delay line still have to keep pace with the input.
        const bool audible = v.gainL > 0.0f || v.gainR > 0.0f || releasing;

        if ((int) v.scratch.size() < numSamples)
            v.scratch.resize ((size_t) numSamples);

        v.shifter.process (monoInput, v.scratch.data(), numSamples,
                           v.pitchRatio, v.formantRatio, lastPeriod, lastVoiced);

        if (! audible)
        {
            for (int n = 0; n < numSamples; ++n)
            {
                v.delayLine[(size_t) v.delayPos] = v.scratch[(size_t) n];
                if (++v.delayPos >= maxDelaySamples)
                    v.delayPos = 0;
            }

            v.smoothedGainL = v.smoothedGainR = 0.0f;
            continue;
        }

        for (int n = 0; n < numSamples; ++n)
        {
            v.delayLine[(size_t) v.delayPos] = v.scratch[(size_t) n];

            int readPos = v.delayPos - v.delaySamples;
            if (readPos < 0)
                readPos += maxDelaySamples;

            const float s = v.delayLine[(size_t) readPos];

            v.smoothedGainL += (v.gainL - v.smoothedGainL)
                             * (v.gainL > v.smoothedGainL ? attack : release);
            v.smoothedGainR += (v.gainR - v.smoothedGainR)
                             * (v.gainR > v.smoothedGainR ? attack : release);

            left[n]  += s * v.smoothedGainL;
            right[n] += s * v.smoothedGainR;

            if (++v.delayPos >= maxDelaySamples)
                v.delayPos = 0;
        }
    }
}

} // namespace helix
