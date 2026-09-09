#pragma once

#include <juce_core/juce_core.h>

namespace helix
{

/** Detects unpitched consonants so they can be left alone.

    Sibilants and plosives have no meaningful pitch, but a tracker will always
    report *something* for them. Correcting that something is what produces the
    classic artefacts: a lisping, warbling "s", and a click where a "t" gets
    dragged onto a scale note.

    The test is cheap and entirely time domain - fricatives sit high in the
    spectrum, cross zero constantly, and are aperiodic - so it costs a couple of
    passes over the frame rather than another transform.
*/
class TransientGuard
{
public:
    void prepare (double sampleRate, int hopSizeSamples);
    void reset() noexcept;

    /** 0 disables the guard entirely; 1 is aggressive. */
    void setSensitivity (float amount01) noexcept { sensitivity = juce::jlimit (0.0f, 1.0f, amount01); }

    /** @param periodicity  detector confidence, 0..1
        @return how much this frame should be left uncorrected, 0..1 */
    float process (const float* frame, int numSamples, float periodicity) noexcept;

    float getAmount() const noexcept { return smoothed; }

private:
    float sensitivity = 0.5f;
    float smoothed = 0.0f;
    float attack = 0.5f;
    float release = 0.02f;
};

} // namespace helix
