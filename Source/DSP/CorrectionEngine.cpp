#include "CorrectionEngine.h"
#include <cmath>
#include <algorithm>
#include <cstring>

namespace helix
{

static constexpr float kLowestSupportedHz = 32.0f;   // bass instrument range

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

    detector.prepare (sampleRate);
    retune.prepare (sampleRate, hopSize);
    vibrato.prepare (sampleRate, hopSize);

    shifters.resize ((size_t) numCh);
    for (auto& s : shifters)
        s.prepare (sampleRate, kLowestSupportedHz, maxBlockSize);

    mono.assign ((size_t) juce::jmax (1, maxBlockSize), 0.0f);
    history.assign ((size_t) detector.getMaxFrameSize(), 0.0f);

    int64_t ringSize = 1;
    while (ringSize < (int64_t) (shifters[0].getLatencySamples() + maxBlockSize + 16))
        ringSize <<= 1;

    dryRing.assign ((size_t) numCh, std::vector<float> ((size_t) ringSize, 0.0f));
    dryMask = ringSize - 1;
    dryWrite = 0;

    latency = shifters[0].getLatencySamples();
    lastInputType = -1;
    lastNoteStates = 0xFFFFFFFF;
    lastKey = lastScale = -1;

    reset();
}

void CorrectionEngine::reset() noexcept
{
    detector.reset();
    retune.reset();
    vibrato.reset();

    for (auto& s : shifters)
        s.reset();

    std::fill (history.begin(), history.end(), 0.0f);
    for (auto& r : dryRing)
        std::fill (r.begin(), r.end(), 0.0f);

    dryWrite = 0;
    hopCounter = 0;
    curPitchRatio = curFormantRatio = 1.0f;
    curGain = 1.0f;
    curVoiced = false;
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

        const int newLatency = shifters[0].getLatencySamples();
        if (newLatency != latency)
        {
            latency = newLatency;
            latencyDirty = true;
        }
    }

    detector.setTracking (s.tracking);

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
    const int offset = (int) history.size() - frameSize;
    const auto det = detector.process (history.data() + offset);

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
    const float totalSemis = ro.correctionSemis + vo.pitchCents * 0.01f;
    curPitchRatio = std::pow (2.0f, totalSemis / 12.0f);

    // PSOLA leaves formants where they are by default. Formant correction OFF
    // deliberately drags them along with the pitch, which is the varispeed /
    // chipmunk character. Throat length then scales the vocal tract on top.
    float formant = 1.0f;
    if (! s.formantCorrection)
        formant *= curPitchRatio;

    formant /= juce::jmax (0.1f, s.throatLength);
    formant *= std::pow (2.0f, vo.formantCents / 1200.0f);
    curFormantRatio = formant;

    curGain = vo.gain;
    curVoiced = det.voiced;

    if (det.voiced && det.frequencyHz > 1.0f)
        curPeriod = (float) (fs / (double) det.frequencyHz);

    // --- hand the frame to the editor -------------------------------------
    PitchFrame frame;
    frame.timeSeconds  = hopTime;
    frame.ppq          = hopPpq;
    frame.detectedMidi = det.midiNote;
    frame.targetMidi   = haveTarget ? targetMidi : ro.targetMidi;
    frame.outputMidi   = ro.outputMidi;
    frame.confidence   = det.confidence;
    frame.rms          = lastRms;
    frame.voiced       = det.voiced;
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
        mono.resize ((size_t) n);

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

    const int historySize = (int) history.size();
    int pos = 0;

    while (pos < n)
    {
        const int chunk = juce::jmin (hopSize - hopCounter, n - pos);

        // Slide the analysis history forward by this chunk.
        if (chunk >= historySize)
        {
            std::copy (mono.begin() + (pos + chunk - historySize), mono.begin() + (pos + chunk),
                       history.begin());
        }
        else
        {
            std::memmove (history.data(), history.data() + chunk,
                          (size_t) (historySize - chunk) * sizeof (float));
            std::copy (mono.begin() + pos, mono.begin() + (pos + chunk),
                       history.begin() + (historySize - chunk));
        }

        for (int ch = 0; ch < channels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            auto& ring = dryRing[(size_t) ch];

            for (int i = 0; i < chunk; ++i)
                ring[(size_t) ((dryWrite + i) & dryMask)] = data[pos + i];

            shifters[(size_t) ch].process (data + pos, data + pos, chunk,
                                           curPitchRatio, curFormantRatio,
                                           curPeriod, curVoiced);
        }

        // Blend against the dry signal delayed by exactly our own latency, so
        // Mix is a true crossfade rather than a comb filter.
        for (int ch = 0; ch < channels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            const auto& ring = dryRing[(size_t) ch];

            for (int i = 0; i < chunk; ++i)
            {
                const int64_t readPos = dryWrite + i - latency;
                const float dry = (readPos >= 0) ? ring[(size_t) (readPos & dryMask)] : 0.0f;
                const float wet = data[pos + i] * curGain;

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
