#include "PitchDetector.h"
#include <cmath>
#include <algorithm>

namespace helix
{

FrequencyRange rangeForInputType (InputType t)
{
    switch (t)
    {
        case InputType::soprano:        return {  180.0f, 1600.0f };
        case InputType::altoTenor:      return {   90.0f,  900.0f };
        case InputType::lowMale:        return {   65.0f,  600.0f };
        case InputType::instrument:     return {   55.0f, 2200.0f };
        case InputType::bassInstrument: return {   32.0f,  400.0f };

        case InputType::generic:
        {
            // Derived rather than written out, so retuning any other range can
            // never leave Generic narrower than a type it claims to cover.
            FrequencyRange all { 1.0e6f, 0.0f };

            for (int i = 0; i < numInputTypes; ++i)
            {
                if ((InputType) i == InputType::generic)
                    continue;

                const auto r = rangeForInputType ((InputType) i);
                all.minHz = std::min (all.minHz, r.minHz);
                all.maxHz = std::max (all.maxHz, r.maxHz);
            }

            return all;
        }
    }
    return { 65.0f, 900.0f };
}

void PitchDetector::prepare (double sampleRate)
{
    fs = sampleRate;
    fftPool.clear();

    int maxFft = 0;
    int maxTau = 0;
    maxFrameSize = 0;

    for (int i = 0; i < numInputTypes; ++i)
    {
        const auto r = rangeForInputType ((InputType) i);
        auto& c = configs[(size_t) i];

        // YIN needs the difference sum to span at least two periods of the
        // lowest frequency in range, and tau must reach that period. Hence
        // window == tauMax and frameSize == 2 * window.
        c.tauMax    = (int) std::ceil (sampleRate / (double) r.minHz) + 2;
        c.tauMin    = std::max (2, (int) std::floor (sampleRate / (double) r.maxHz) - 2);
        c.window    = c.tauMax;
        c.frameSize = c.window + c.tauMax;

        // Linear correlation needs fftSize >= len(a) + len(b) - 1.
        int order = 1;
        while ((1 << order) < c.frameSize + c.window)
            ++order;

        c.fftSize = 1 << order;

        // Reuse an engine if another input type already needs this order.
        juce::dsp::FFT* found = nullptr;
        for (auto& e : fftPool)
            if (e->getSize() == c.fftSize)
            {
                found = e.get();
                break;
            }

        if (found == nullptr)
        {
            fftPool.push_back (std::make_unique<juce::dsp::FFT> (order));
            found = fftPool.back().get();
        }

        c.fft = found;

        maxFft       = std::max (maxFft, c.fftSize);
        maxTau       = std::max (maxTau, c.tauMax);
        maxFrameSize = std::max (maxFrameSize, c.frameSize);
    }

    specA  .assign ((size_t) maxFft, Cplx {});
    specB  .assign ((size_t) maxFft, Cplx {});
    specOut.assign ((size_t) maxFft, Cplx {});

    corr .assign ((size_t) maxTau + 1, 0.0f);
    power.assign ((size_t) maxTau + 1, 0.0f);
    diff .assign ((size_t) maxTau + 1, 0.0f);
    cmnd .assign ((size_t) maxTau + 1, 0.0f);

    reset();
}

void PitchDetector::setInputType (InputType type) noexcept
{
    current = juce::jlimit (0, numInputTypes - 1, (int) type);
}

void PitchDetector::reset() noexcept
{
    history[0] = history[1] = 0.0f;
    historyFill = 0;
}

void PitchDetector::computeDifferenceFunction (const float* frame, const Config& c) noexcept
{
    // r(tau) = sum_{j<W} x[j] x[j+tau], obtained as IFFT(A * conj(B)) where A
    // is the whole frame and B is just the first W samples.
    std::fill (specA.begin(), specA.begin() + c.fftSize, Cplx {});
    std::fill (specB.begin(), specB.begin() + c.fftSize, Cplx {});

    for (int i = 0; i < c.frameSize; ++i)
        specA[(size_t) i] = Cplx (frame[i], 0.0f);

    for (int i = 0; i < c.window; ++i)
        specB[(size_t) i] = Cplx (frame[i], 0.0f);

    c.fft->perform (specA.data(), specOut.data(), false);
    std::copy (specOut.begin(), specOut.begin() + c.fftSize, specA.begin());

    c.fft->perform (specB.data(), specOut.data(), false);
    std::copy (specOut.begin(), specOut.begin() + c.fftSize, specB.begin());

    for (int k = 0; k < c.fftSize; ++k)
        specA[(size_t) k] *= std::conj (specB[(size_t) k]);

    c.fft->perform (specA.data(), specOut.data(), true);

    // p(0), then a running p(tau) that slides the W-wide window forward.
    double p0 = 0.0;
    for (int j = 0; j < c.window; ++j)
        p0 += (double) frame[j] * frame[j];

    power[0] = (float) p0;
    double running = p0;
    for (int tau = 1; tau <= c.tauMax; ++tau)
    {
        running -= (double) frame[tau - 1] * frame[tau - 1];
        running += (double) frame[tau - 1 + c.window] * frame[tau - 1 + c.window];
        power[(size_t) tau] = (float) std::max (0.0, running);
    }

    // Self-calibrate the transform scale from the identity r(0) == p(0). This
    // sidesteps any assumption about whether the library's inverse normalises.
    const float raw0 = specOut[0].real();
    const float scale = (std::abs (raw0) > 1.0e-20f) ? (float) p0 / raw0 : 0.0f;

    for (int tau = 0; tau <= c.tauMax; ++tau)
    {
        corr[(size_t) tau] = specOut[(size_t) tau].real() * scale;
        diff[(size_t) tau] = std::max (0.0f, power[0] + power[(size_t) tau] - 2.0f * corr[(size_t) tau]);
    }
}

void PitchDetector::cumulativeMeanNormalise (const Config& c) noexcept
{
    cmnd[0] = 1.0f;
    double running = 0.0;

    for (int tau = 1; tau <= c.tauMax; ++tau)
    {
        running += (double) diff[(size_t) tau];
        cmnd[(size_t) tau] = (running > 1.0e-20)
                           ? (float) (diff[(size_t) tau] * tau / running)
                           : 1.0f;
    }
}

int PitchDetector::absoluteThreshold (const Config& c, float threshold) const noexcept
{
    // Take the *first* dip below threshold rather than the global minimum.
    // That ordering is what biases YIN against reporting an octave down.
    for (int tau = c.tauMin; tau <= c.tauMax; ++tau)
    {
        if (cmnd[(size_t) tau] < threshold)
        {
            while (tau + 1 <= c.tauMax && cmnd[(size_t) (tau + 1)] < cmnd[(size_t) tau])
                ++tau;

            return tau;
        }
    }

    // Nothing periodic enough: hand back the global minimum and let the
    // confidence value tell the caller not to trust it.
    int best = c.tauMin;
    for (int tau = c.tauMin; tau <= c.tauMax; ++tau)
        if (cmnd[(size_t) tau] < cmnd[(size_t) best])
            best = tau;

    return -best;   // negative signals "threshold was never crossed"
}

float PitchDetector::parabolicRefine (const Config& c, int tau) const noexcept
{
    // Sub-sample period accuracy. Without this the quantisation floor at
    // 44.1 kHz is ~13 cents at A4 and ~50 cents at A5 - audibly wrong.
    //
    // The fit runs on the raw difference function rather than the normalised
    // one the minimum was picked from. Normalising multiplies by tau over a
    // running sum, which tilts the curve and drags the parabola's vertex - and
    // at short periods one sample is a large fraction of the period.
    if (tau <= c.tauMin || tau >= c.tauMax)
        return (float) tau;

    const float a = diff[(size_t) (tau - 1)];
    const float b = diff[(size_t) tau];
    const float d = diff[(size_t) (tau + 1)];

    const float denom = a - 2.0f * b + d;
    if (std::abs (denom) < 1.0e-12f)
        return (float) tau;

    return (float) tau + juce::jlimit (-1.0f, 1.0f, 0.5f * (a - d) / denom);
}

void PitchDetector::collectCandidates (const Config& c, float threshold, int tauYin,
                                       Result& out) const noexcept
{
    // Every local minimum of the normalised difference is a period the frame is
    // consistent with, and handing several upward lets a temporal model choose.
    //
    // But the lattice cannot simply be "the lowest-cost minima". The cumulative
    // mean in YIN's step 3 divides by a running average that grows with tau, so
    // a subharmonic routinely scores *better* than the true period - for a
    // 430 Hz tone, cmnd is 0.0014 at T and 0.0002 at 2T. Ranking by cost alone
    // therefore evicts the correct answer and keeps six wrong octaves.
    //
    // So the lattice is anchored on YIN's own threshold-crossing estimate,
    // which is right by construction, and the alternatives are hung around it.
    out.numCandidates = 0;

    const int anchorTau = juce::jlimit (c.tauMin, c.tauMax, std::abs (tauYin));
    const float anchor = parabolicRefine (c, anchorTau);

    if (anchor > 0.0f)
    {
        auto& cand = out.candidates[(size_t) out.numCandidates++];
        cand.periodSamples = anchor;
        cand.frequencyHz   = (float) (fs / (double) anchor);
        cand.cost          = juce::jlimit (0.0f, 1.0f, cmnd[(size_t) anchorTau]);
    }

    struct Local { int tau; float cost; };
    Local best[maxCandidates];
    int found = 0;

    for (int tau = c.tauMin + 1; tau < c.tauMax; ++tau)
    {
        const float v = cmnd[(size_t) tau];

        if (v >= cmnd[(size_t) (tau - 1)] || v > cmnd[(size_t) (tau + 1)])
            continue;

        if (v > 0.75f)          // far too aperiodic to be worth carrying
            continue;

        if (std::abs (tau - anchorTau) <= 2)   // already present as the anchor
            continue;

        if (found < maxCandidates - 1)
        {
            best[found++] = { tau, v };
        }
        else
        {
            // Replace the worst entry if this one beats it.
            int worst = 0;
            for (int i = 1; i < found; ++i)
                if (best[i].cost > best[worst].cost)
                    worst = i;

            if (found > 0 && v < best[worst].cost)
                best[worst] = { tau, v };
        }
    }

    for (int i = 0; i < found && out.numCandidates < maxCandidates; ++i)
    {
        const float tau = parabolicRefine (c, best[i].tau);
        if (tau <= 0.0f)
            continue;

        auto& cand = out.candidates[(size_t) out.numCandidates++];
        cand.periodSamples = tau;
        cand.frequencyHz   = (float) (fs / (double) tau);
        cand.cost          = juce::jlimit (0.0f, 1.0f, best[i].cost);
    }

    if (out.numCandidates == 0 || anchor <= 0.0f)
        return;

    // Octave alternatives stay in the lattice, but start at a disadvantage, so
    // the temporal model only leaves the anchor when history really insists.
    for (int i = 0; i < out.numCandidates; ++i)
    {
        auto& cand = out.candidates[(size_t) i];
        const float ratio = cand.periodSamples / anchor;

        if (ratio > 1.4f)
            cand.cost = juce::jlimit (0.0f, 2.0f, cand.cost + 0.35f * std::log2 (ratio));
        else if (ratio < 1.0f / 1.4f)
            cand.cost = juce::jlimit (0.0f, 2.0f, cand.cost + 0.35f * std::log2 (1.0f / ratio));
    }

    std::sort (out.candidates, out.candidates + out.numCandidates,
               [] (const Candidate& a, const Candidate& b) { return a.periodSamples < b.periodSamples; });

    juce::ignoreUnused (threshold);
}

PitchDetector::Result PitchDetector::process (const float* frame) noexcept
{
    Result out;
    const auto& c = configs[(size_t) current];

    if (c.fft == nullptr)
        return out;

    // Cheap gate first: silence should never cost an FFT.
    double energy = 0.0;
    for (int i = 0; i < c.frameSize; ++i)
        energy += (double) frame[i] * frame[i];

    out.rms = (float) std::sqrt (energy / (double) c.frameSize);

    if (out.rms < 1.0e-5f)
    {
        reset();
        return out;
    }

    computeDifferenceFunction (frame, c);
    cumulativeMeanNormalise (c);

    // tracking 0 -> only very clean periodicity passes; 1 -> tolerate breath.
    const float threshold = juce::jmap (tracking, 0.0f, 1.0f, 0.05f, 0.30f);

    const int raw = absoluteThreshold (c, threshold);

    collectCandidates (c, threshold, std::abs (raw), out);
    const bool crossed = raw > 0;
    const int tauInt = std::abs (raw);

    const float tau = parabolicRefine (c, tauInt);
    if (tau <= 0.0f)
        return out;

    const float freq = (float) (fs / (double) tau);
    const auto range = rangeForInputType ((InputType) current);

    out.confidence = juce::jlimit (0.0f, 1.0f, 1.0f - cmnd[(size_t) tauInt]);
    out.voiced = crossed
              && freq >= range.minHz
              && freq <= range.maxHz
              && out.confidence > 0.35f;

    if (! out.voiced)
    {
        reset();
        return out;
    }

    out.frequencyHz = freq;

    // Single-frame outlier rejection. If the two previous estimates agree with
    // each other but not with this one, this one is almost certainly a
    // consonant or an octave slip - hold the previous value rather than
    // delaying the whole signal through a median filter.
    if (historyFill >= 2)
    {
        const bool prevAgree    = std::abs (std::log2 (history[0] / history[1])) < 0.04f;  // ~2/3 semitone
        const bool newDisagrees = std::abs (std::log2 (freq / history[0]))       > 0.10f;  // ~1.7 semitones

        if (prevAgree && newDisagrees)
            out.frequencyHz = history[0];
    }

    history[1] = history[0];
    history[0] = out.frequencyHz;
    historyFill = std::min (2, historyFill + 1);

    out.midiNote = 69.0f + 12.0f * std::log2 (out.frequencyHz / 440.0f);
    return out;
}

} // namespace helix
