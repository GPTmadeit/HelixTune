#include "CorrectionEngine.h"
#include <cmath>
#include <algorithm>
#include <cstring>

namespace helix
{

static constexpr float kLowestSupportedHz = 32.0f;   // bass instrument range

/** Shifts @p count new samples onto the end of a fixed-length history. */
static void slideInto (std::vector<float>& history, const float* src, int count) noexcept
{
    const int size = (int) history.size();

    if (count <= 0 || size <= 0)
        return;

    if (count >= size)
    {
        std::copy (src + (count - size), src + count, history.begin());
        return;
    }

    std::memmove (history.data(), history.data() + count, (size_t) (size - count) * sizeof (float));
    std::copy (src, src + count, history.begin() + (size - count));
}

void CorrectionEngine::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    fs = sampleRate;
    numCh = juce::jmax (1, numChannels);

    // ~5 ms analysis hop, rounded to a power of two. Fast enough to catch a
    // note onset, slow enough that the FFT cost stays reasonable.
    int hop = 1;
    while (hop < (int) (sampleRate * 0.005))
        hop <<= 1;

    hopSize = juce::jlimit (128, 1024, hop);
    hopCounter = 0;

    // Above 64 kHz the detector hears a decimated copy of the input. Its cost
    // follows the sample rate and nothing it measures lives above 20 kHz.
    analysisDecimation = AnalysisDecimator::factorFor (sampleRate);
    decimator.prepare (analysisDecimation);

    detector.prepare (sampleRate / (double) analysisDecimation);
    stabilizer.prepare (sampleRate, hopSize);
    retune.prepare (sampleRate, hopSize);
    vibrato.prepare (sampleRate, hopSize);
    keyDetector.prepare (sampleRate, hopSize);
    transientGuard.prepare (sampleRate, hopSize);
    formantProc.prepare (sampleRate, maxBlockSize, numCh);
    harmony.prepare (sampleRate, maxBlockSize, kLowestSupportedHz);

    shifters.resize ((size_t) numCh);
    for (auto& s : shifters)
        s.prepare (sampleRate, kLowestSupportedHz, maxBlockSize);

    mono.assign ((size_t) juce::jmax (1, maxBlockSize), 0.0f);
    harmonyL.assign ((size_t) juce::jmax (1, maxBlockSize), 0.0f);
    harmonyR.assign ((size_t) juce::jmax (1, maxBlockSize), 0.0f);

    // The session-rate history covers the same stretch of time as the
    // detector's frame, because the formant analysis reads that span.
    history.assign ((size_t) juce::jmax (hopSize, detector.getMaxFrameSize() * analysisDecimation), 0.0f);
    detHistory.assign ((size_t) detector.getMaxFrameSize(), 0.0f);
    decimScratch.assign ((size_t) hopSize + 4, 0.0f);

    latency = shifters[0].getLatencySamples() + formantProc.getLatencySamples();

    int64_t ringSize = 1;
    while (ringSize < (int64_t) (latency + maxBlockSize + 16))
        ringSize <<= 1;

    dryRing.assign ((size_t) numCh, std::vector<float> ((size_t) ringSize, 0.0f));
    dryMask = ringSize - 1;
    dryWrite = 0;

    // The harmony bus skips the lead's formant filter, so it needs that filter's
    // delay added back or the voices would arrive early.
    harmony.setAlignmentDelay (formantProc.getLatencySamples());

    lastInputType = -1;
    lastNoteStates = 0xFFFFFFFF;
    lastKey = lastScale = -1;

    reset();
}

void CorrectionEngine::reset() noexcept
{
    detector.reset();
    stabilizer.reset();
    retune.reset();
    vibrato.reset();
    keyDetector.reset();
    transientGuard.reset();
    formantProc.reset();
    harmony.reset();
    decimator.reset();

    for (auto& s : shifters)
        s.reset();

    std::fill (history.begin(), history.end(), 0.0f);
    std::fill (detHistory.begin(), detHistory.end(), 0.0f);
    for (auto& r : dryRing)
        std::fill (r.begin(), r.end(), 0.0f);

    dryWrite = 0;
    hopCounter = 0;
    curPitchRatio = curFormantRatio = 1.0f;
    curGain = 1.0f;
    curVoiced = false;
    liveMidi = 0.0f;
    liveVoiced = false;
    harmonyRunning = false;
    rightFollowsLeft = true;
    lastRms = 0.0f;
}

bool CorrectionEngine::consumeLatencyChanged() noexcept
{
    const bool was = latencyDirty;
    latencyDirty = false;
    return was;
}

void CorrectionEngine::applySettings (const Settings& s) noexcept
{
    const int typeIndex = (int) s.inputType;

    if (typeIndex != lastInputType)
    {
        lastInputType = typeIndex;
        detector.setInputType (s.inputType);

        const auto range = rangeForInputType (s.inputType);
        for (auto& sh : shifters)
            sh.setMinFrequency (range.minHz);

        harmony.setRange (range.minHz);

        const int newLatency = shifters[0].getLatencySamples() + formantProc.getLatencySamples();
        if (newLatency != latency)
        {
            latency = newLatency;
            latencyDirty = true;
        }
    }

    detector.setTracking (s.tracking);
    stabilizer.setTracking (s.tracking);
    stabilizer.setSmoothing (s.pitchSmoothing);
    transientGuard.setSensitivity (s.sibilanceGuard);

    if (s.key != lastKey)
    {
        lastKey = s.key;
        quantizer.setKey (s.key);
    }

    if (s.scaleIndex != lastScale)
    {
        lastScale = s.scaleIndex;
        quantizer.setScale (s.scaleIndex);
    }

    if (s.noteStateBits != lastNoteStates)
    {
        lastNoteStates = s.noteStateBits;
        for (int pc = 0; pc < 12; ++pc)
        {
            const uint32_t bits = (s.noteStateBits >> (pc * 2)) & 0x3u;
            quantizer.setNoteState (pc, (NoteState) juce::jlimit (0u, 2u, bits));
        }
    }
}

void CorrectionEngine::runAnalysisHop (const Settings& s, double hopTime, double hopPpq,
                                       const NoteSnapshot* graph, float midiTargetMidi,
                                       bool haveMidiTarget, PitchFifo& fifo) noexcept
{
    const int frameSize = detector.getFrameSize();
    const int historySize = (int) history.size();

    // At high sample rates the detector reads its decimated copy; everything
    // else in the hop works at the session rate.
    const float* detectorFrame = analysisDecimation > 1
                               ? detHistory.data() + ((int) detHistory.size() - frameSize)
                               : history.data() + (historySize - frameSize);

    auto raw = detector.process (detectorFrame);

    // Candidate periods come back in the detector's own samples. Nothing
    // downstream should have to know it ran at a different rate.
    for (int i = 0; i < raw.numCandidates; ++i)
        raw.candidates[(size_t) i].periodSamples *= (float) analysisDecimation;

    const auto det = stabilizer.process (raw, rangeForInputType (s.inputType));

    // Consonants are judged on the freshest hop only; a long analysis window
    // would smear a 20 ms plosive into the vowel around it.
    const float consonant = transientGuard.process (history.data() + historySize - hopSize,
                                                    hopSize, det.confidence);

    keyDetector.push (det.midiNote, det.voiced,
                      det.confidence * juce::jlimit (0.0f, 1.0f, lastRms * 40.0f));

    liveMidi = det.midiNote;
    liveVoiced = det.voiced;

    // --- pick a target ----------------------------------------------------
    float targetMidi = 0.0f;
    bool  haveTarget = false;
    float noteRetune = -1.0f;
    float noteVibScale = -1.0f;

    if (s.graphMode)
    {
        if (graph != nullptr)
        {
            if (const auto* span = graph->find (hopTime))
            {
                targetMidi   = span->pitchAt (hopTime);
                noteRetune   = span->retuneMs;
                noteVibScale = span->vibratoScale;
                haveTarget   = true;
            }
        }
        // No note under the playhead means no correction. That is the point of
        // graph mode: silence in the editor is a decision, not a default.
    }
    else if (s.midiTarget)
    {
        if (haveMidiTarget)
        {
            targetMidi = midiTargetMidi;
            haveTarget = true;
        }
    }

    auto rp = s.retune;
    if (noteRetune >= 0.0f)
        rp.retuneMs = noteRetune;

    RetuneEngine::Output ro;

    if (haveTarget)
        ro = retune.processWithTarget (det.midiNote, det.voiced, targetMidi, rp);
    else if (s.graphMode || s.midiTarget)
        ro = retune.passThrough (det.midiNote, det.voiced, rp);
    else
        ro = retune.process (det.midiNote, det.voiced, quantizer, rp);

    // A consonant has no pitch to correct, so fade the correction out across
    // it and let the original through untouched.
    const float correctionScale = 1.0f - consonant;

    // --- vibrato ----------------------------------------------------------
    auto vp = s.vibrato;
    if (noteVibScale >= 0.0f)
    {
        vp.pitchCents   *= noteVibScale;
        vp.amplitude    *= noteVibScale;
        vp.formantCents *= noteVibScale;
    }

    if (det.voiced)
    {
        if (retune.hadOnset())
            vibrato.noteOn();
    }
    else
    {
        vibrato.noteOff();
    }

    const auto vo = vibrato.process (vp);

    // --- assemble the shifter controls ------------------------------------
    // Only the corrective part is faded out across a consonant; transpose and
    // detune are deliberate and stay applied throughout. Correction Amount
    // scales the pull toward the note and nothing else - the vibrato generator
    // is an effect rather than a correction, so it keeps its full depth.
    const float corrective = (ro.correctionSemis - ro.offsetSemis) * s.correctionAmount
                           + vo.pitchCents * 0.01f;
    const float totalSemis = corrective * correctionScale + ro.offsetSemis;
    curPitchRatio = std::pow (2.0f, totalSemis / 12.0f);

    // PSOLA is left as a pure pitch shifter - it preserves formants by
    // construction - and the LPC filter then moves the envelope by exactly the
    // amount asked for. Turning formant correction off means "let the formants
    // follow the pitch", which is the varispeed / chipmunk character.
    float formant = s.formantCorrection ? 1.0f : curPitchRatio;
    formant /= juce::jmax (0.1f, s.throatLength);
    formant *= std::pow (2.0f, vo.formantCents / 1200.0f);

    curFormantRatio = juce::jlimit (0.5f, 2.0f, formant);
    formantProc.setRatio (curFormantRatio);

    const int formantSpan = juce::jmin (historySize, frameSize * analysisDecimation);
    formantProc.analyse (history.data() + (historySize - formantSpan), formantSpan);

    curGain = 1.0f + (vo.gain - 1.0f) * correctionScale;
    curVoiced = det.voiced;

    if (det.voiced && det.frequencyHz > 1.0f)
        curPeriod = (float) (fs / (double) det.frequencyHz);

    // The lead rides through consonants; the harmony bus ducks out of them.
    harmony.setGate (1.0f - consonant);
    harmony.updateTargets (ro.outputMidi, det.midiNote, det.voiced, quantizer, s.harmony);

    // --- hand the frame to the editor -------------------------------------
    PitchFrame frame;
    frame.timeSeconds  = hopTime;
    frame.ppq          = hopPpq;
    frame.detectedMidi = det.midiNote;
    frame.targetMidi   = haveTarget ? targetMidi : ro.targetMidi;
    frame.outputMidi   = det.midiNote + totalSemis;
    frame.confidence   = det.confidence;
    frame.rms          = lastRms;
    frame.voiced       = det.voiced;
    frame.consonant    = consonant;
    fifo.push (frame);
}

void CorrectionEngine::process (juce::AudioBuffer<float>& buffer,
                                const Settings& s,
                                double blockStartSeconds,
                                double blockStartPpq,
                                const NoteSnapshot* graph,
                                float midiTargetMidi,
                                bool haveMidiTarget,
                                PitchFifo& fifo) noexcept
{
    applySettings (s);

    const int n = buffer.getNumSamples();
    const int channels = juce::jmin (buffer.getNumChannels(), (int) shifters.size());

    if (n <= 0 || channels <= 0)
        return;

    // Hosts are supposed to honour the block size they declared, and some do
    // not. Growing here allocates on the audio thread, which is bad, but it is
    // strictly better than the buffer overrun that the alternative would be.
    if ((int) mono.size() < n)
    {
        mono.resize ((size_t) n);
        harmonyL.resize ((size_t) n);
        harmonyR.resize ((size_t) n);
    }

    // Channel sum drives detection so both channels get the same correction.
    const float norm = 1.0f / (float) channels;
    for (int i = 0; i < n; ++i)
    {
        float sum = 0.0f;
        for (int ch = 0; ch < channels; ++ch)
            sum += buffer.getReadPointer (ch)[i];

        mono[(size_t) i] = sum * norm;
    }

    double energy = 0.0;
    for (int i = 0; i < n; ++i)
        energy += (double) mono[(size_t) i] * mono[(size_t) i];

    lastRms = (float) std::sqrt (energy / (double) n);

    std::fill (harmonyL.begin(), harmonyL.begin() + n, 0.0f);
    std::fill (harmonyR.begin(), harmonyR.begin() + n, 0.0f);

    const bool stereoPair = channels == 2;
    int pos = 0;

    while (pos < n)
    {
        const int chunk = juce::jmin (hopSize - hopCounter, n - pos);

        slideInto (history, mono.data() + pos, chunk);

        if (analysisDecimation > 1)
        {
            const int produced = decimator.process (mono.data() + pos, chunk, decimScratch.data());
            slideInto (detHistory, decimScratch.data(), produced);
        }

        for (int ch = 0; ch < channels; ++ch)
        {
            const auto* data = buffer.getReadPointer (ch);
            auto& ring = dryRing[(size_t) ch];

            for (int i = 0; i < chunk; ++i)
                ring[(size_t) ((dryWrite + i) & dryMask)] = data[pos + i];
        }

        // A mono vocal on a stereo track reaches us as two identical channels,
        // and rendering both would do the most expensive work twice for the
        // same result. While every sample since the last reset has matched,
        // the right channel's state is provably the same as the left's, so the
        // left is rendered once and copied. The first chunk that differs hands
        // the right channel a copy of that state and it carries on alone - the
        // output is bit-identical to having rendered both all along.
        const bool linked = stereoPair && stereoLinkingEnabled && rightFollowsLeft
                         && std::memcmp (buffer.getReadPointer (0) + pos,
                                         buffer.getReadPointer (1) + pos,
                                         (size_t) chunk * sizeof (float)) == 0;

        if (linked)
        {
            auto* left = buffer.getWritePointer (0);

            shifters[0].process (left + pos, left + pos, chunk,
                                 curPitchRatio, 1.0f, curPeriod, curVoiced);
            formantProc.process (left + pos, chunk, 0);

            std::memcpy (buffer.getWritePointer (1) + pos, left + pos, (size_t) chunk * sizeof (float));
        }
        else
        {
            if (stereoPair && rightFollowsLeft)
            {
                // Same configuration on both sides, so this copies state into
                // storage that already exists rather than allocating.
                shifters[1] = shifters[0];
                formantProc.copyChannelState (0, 1);
                rightFollowsLeft = false;
            }

            for (int ch = 0; ch < channels; ++ch)
            {
                auto* data = buffer.getWritePointer (ch);

                shifters[(size_t) ch].process (data + pos, data + pos, chunk,
                                               curPitchRatio, 1.0f, curPeriod, curVoiced);

                formantProc.process (data + pos, chunk, ch);
            }
        }

        if (s.harmony.anyEnabled)
        {
            // Voices stop consuming input while the bus is off, so after any
            // spell switched off their mark tracking is stranded in the past.
            // A clean start costs one fade-in; resuming stale state costs a
            // burst of misplaced grains.
            if (! harmonyRunning)
                harmony.reset();

            harmony.process (mono.data() + pos,
                             harmonyL.data() + pos, harmonyR.data() + pos,
                             chunk, s.harmony);
        }

        harmonyRunning = s.harmony.anyEnabled;

        // Blend against the dry signal delayed by exactly our own latency, so
        // Mix is a true crossfade rather than a comb filter.
        for (int ch = 0; ch < channels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            const auto& ring = dryRing[(size_t) ch];
            const float* harm = (ch == 0 || channels == 1) ? harmonyL.data() : harmonyR.data();

            for (int i = 0; i < chunk; ++i)
            {
                const int64_t readPos = dryWrite + i - latency;
                const float dry = (readPos >= 0) ? ring[(size_t) (readPos & dryMask)] : 0.0f;
                const float wet = data[pos + i] * curGain + harm[pos + i];

                data[pos + i] = s.bypass ? dry
                                         : (dry + (wet - dry) * s.mix) * s.outputGain;
            }
        }

        dryWrite += chunk;
        pos += chunk;
        hopCounter += chunk;

        if (hopCounter >= hopSize)
        {
            hopCounter = 0;

            const double hopTime = blockStartSeconds + (double) pos / fs;
            const double hopPpq  = (blockStartPpq >= 0.0) ? blockStartPpq : -1.0;

            runAnalysisHop (s, hopTime, hopPpq, graph, midiTargetMidi, haveMidiTarget, fifo);
        }
    }
}

} // namespace helix
