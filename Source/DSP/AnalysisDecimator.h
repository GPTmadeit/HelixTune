#pragma once

#include <juce_core/juce_core.h>
#include <algorithm>
#include <array>
#include <cmath>

namespace helix
{

/** Integer-factor downsampler for the pitch detector's input only.

    Detection has no use for anything above 20 kHz, but its cost follows the
    sample rate: the difference function has to span two periods of the lowest
    note, so doubling the rate doubles both the frame and the transform. At
    96 kHz that pushed one analysis hop past the deadline of a small buffer.
    Detecting at a half or a quarter of the rate keeps it at the 44.1/48 kHz
    cost everything else is tuned for. The audio itself never passes through
    here.

    Each stage is a 31-tap Blackman-windowed half-band, which is flat well past
    the top of any voice and costs nothing worth measuring.
*/
class AnalysisDecimator
{
public:
    static int factorFor (double sampleRate) noexcept
    {
        return sampleRate > 128000.0 ? 4 : (sampleRate > 64000.0 ? 2 : 1);
    }

    void prepare (int factor) noexcept
    {
        numStages = factor >= 4 ? 2 : (factor >= 2 ? 1 : 0);

        const int centre = numTaps / 2;
        const double pi = juce::MathConstants<double>::pi;
        double sum = 0.0;

        for (int i = 0; i < numTaps; ++i)
        {
            const int k = i - centre;

            // Half-band ideal response: 1/2 at the centre, zero at every other
            // even offset, sinc on the odd ones.
            const double ideal = (k == 0) ? 0.5
                               : (k % 2 == 0) ? 0.0
                                              : std::sin (0.5 * pi * k) / (pi * k);

            const double window = 0.42 + 0.5 * std::cos (pi * k / centre)
                                + 0.08 * std::cos (2.0 * pi * k / centre);

            taps[(size_t) i] = (float) (ideal * window);
            sum += ideal * window;
        }

        // Unity at DC, so the detector's level thresholds mean the same thing
        // at every sample rate.
        for (auto& t : taps)
            t = (float) (t / sum);

        reset();
    }

    void reset() noexcept
    {
        for (auto& s : stages)
        {
            s.line.fill (0.0f);
            s.pos = 0;
            s.phase = 0;
        }
    }

    /** Writes the decimated signal to @p dest and returns how many samples that
        was. @p dest may be the same buffer as @p input. */
    int process (const float* input, int numSamples, float* dest) noexcept
    {
        if (numStages == 0)
        {
            std::copy (input, input + numSamples, dest);
            return numSamples;
        }

        int count = numSamples;
        const float* src = input;

        for (int s = 0; s < numStages; ++s)
        {
            count = stages[(size_t) s].run (src, count, dest, taps);
            src = dest;
        }

        return count;
    }

private:
    static constexpr int numTaps = 31;

    struct Stage
    {
        std::array<float, numTaps> line { };
        int pos = 0;
        int phase = 0;

        // Safe in place: output k is written only after input 2k + 1 is read.
        int run (const float* in, int n, float* out, const std::array<float, numTaps>& h) noexcept
        {
            int produced = 0;

            for (int i = 0; i < n; ++i)
            {
                line[(size_t) pos] = in[i];
                if (++pos == numTaps)
                    pos = 0;

                phase ^= 1;
                if (phase != 0)
                    continue;

                float acc = 0.0f;
                int idx = pos;   // oldest sample

                for (int t = 0; t < numTaps; ++t)
                {
                    acc += h[(size_t) t] * line[(size_t) idx];
                    if (++idx == numTaps)
                        idx = 0;
                }

                out[produced++] = acc;
            }

            return produced;
        }
    };

    std::array<float, numTaps> taps { };
    std::array<Stage, 2> stages { };
    int numStages = 0;
};

} // namespace helix
