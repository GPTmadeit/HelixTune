#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace helix
{

HelixTuneProcessor::HelixTuneProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", params::createLayout())
{
    buildCache();
    graph.commit();
}

void HelixTuneProcessor::buildCache()
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id); };

    cache.bypass           = get (params::bypass);
    cache.mix              = get (params::mix);
    cache.outputGain       = get (params::outputGain);
    cache.inputType        = get (params::inputType);
    cache.tracking         = get (params::tracking);
    cache.key              = get (params::key);
    cache.scale            = get (params::scale);
    cache.retuneSpeed      = get (params::retuneSpeed);
    cache.humanize         = get (params::humanize);
    cache.flexTune         = get (params::flexTune);
    cache.naturalVibrato   = get (params::naturalVibrato);
    cache.targetIgnoresVib = get (params::targetIgnoresVib);
    cache.transpose        = get (params::transpose);
    cache.detune           = get (params::detune);
    cache.classicMode      = get (params::classicMode);
    cache.formantCorrect   = get (params::formantCorrect);
    cache.throatLength     = get (params::throatLength);
    cache.vibShape         = get (params::vibShape);
    cache.vibRate          = get (params::vibRate);
    cache.vibVariation     = get (params::vibVariation);
    cache.vibOnsetDelay    = get (params::vibOnsetDelay);
    cache.vibOnsetRate     = get (params::vibOnsetRate);
    cache.vibPitchAmount   = get (params::vibPitchAmount);
    cache.vibAmpAmount     = get (params::vibAmpAmount);
    cache.vibFormantAmount = get (params::vibFormantAmount);
    cache.graphMode        = get (params::graphMode);
    cache.midiTarget       = get (params::midiTarget);
}

bool HelixTuneProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    const auto& in  = layouts.getMainInputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return in == out;
}

void HelixTuneProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    freeRunningTime = 0.0;

    engine.prepare (sampleRate, samplesPerBlock, getTotalNumOutputChannels());
    setLatencySamples (engine.getLatencySamples());

    fifo.reset();
    heldNotes.fill (false);
    lastHeldNote = -1;
}

NoteState HelixTuneProcessor::getNoteState (int pitchClass) const noexcept
{
    const int pc = ((pitchClass % 12) + 12) % 12;
    const uint32_t bits = (noteStateBits.load (std::memory_order_relaxed) >> (pc * 2)) & 0x3u;
    return (NoteState) juce::jlimit (0u, 2u, bits);
}

void HelixTuneProcessor::setNoteState (int pitchClass, NoteState state) noexcept
{
    const int pc = ((pitchClass % 12) + 12) % 12;
    uint32_t bits = noteStateBits.load (std::memory_order_relaxed);
    bits &= ~(0x3u << (pc * 2));
    bits |= ((uint32_t) state & 0x3u) << (pc * 2);
    noteStateBits.store (bits, std::memory_order_relaxed);
}

void HelixTuneProcessor::clearNoteStates() noexcept
{
    noteStateBits.store (0, std::memory_order_relaxed);
}

CorrectionEngine::Settings HelixTuneProcessor::buildSettings() const noexcept
{
    CorrectionEngine::Settings s;

    s.inputType = (InputType) juce::jlimit (0, numInputTypes - 1, (int) cache.inputType->load());
    s.tracking  = cache.tracking->load() * 0.01f;

    s.key           = (int) cache.key->load();
    s.scaleIndex    = (int) cache.scale->load();
    s.noteStateBits = noteStateBits.load (std::memory_order_relaxed);

    s.retune.retuneMs             = cache.retuneSpeed->load();
    s.retune.humanize             = cache.humanize->load() * 0.01f;
    s.retune.flexTune             = cache.flexTune->load() * 0.01f;
    s.retune.naturalVibrato       = cache.naturalVibrato->load() / 12.0f;
    s.retune.targetIgnoresVibrato = cache.targetIgnoresVib->load() > 0.5f;
    s.retune.transposeSemis       = cache.transpose->load();
    s.retune.detuneCents          = cache.detune->load();
    s.retune.classicMode          = cache.classicMode->load() > 0.5f;

    s.formantCorrection = cache.formantCorrect->load() > 0.5f;
    s.throatLength      = cache.throatLength->load();

    s.vibrato.shape        = (VibratoShape) juce::jlimit (0, 4, (int) cache.vibShape->load());
    s.vibrato.rateHz       = cache.vibRate->load();
    s.vibrato.variation    = cache.vibVariation->load() * 0.01f;
    s.vibrato.onsetDelayMs = cache.vibOnsetDelay->load();
    s.vibrato.onsetRateMs  = cache.vibOnsetRate->load();
    s.vibrato.pitchCents   = cache.vibPitchAmount->load();
    s.vibrato.amplitude    = cache.vibAmpAmount->load() * 0.01f;
    s.vibrato.formantCents = cache.vibFormantAmount->load();

    s.graphMode  = cache.graphMode->load() > 0.5f;
    s.midiTarget = cache.midiTarget->load() > 0.5f;
    s.bypass     = cache.bypass->load() > 0.5f;

    s.mix        = cache.mix->load() * 0.01f;
    s.outputGain = juce::Decibels::decibelsToGain (cache.outputGain->load());

    return s;
}

void HelixTuneProcessor::updateMidiTargets (const juce::MidiBuffer& midi) noexcept
{
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();

        if (m.isNoteOn())
        {
            const int n = m.getNoteNumber();
            heldNotes[(size_t) n] = true;
            lastHeldNote = n;
        }
        else if (m.isNoteOff())
        {
            const int n = m.getNoteNumber();
            heldNotes[(size_t) n] = false;

            if (lastHeldNote == n)
            {
                lastHeldNote = -1;
                for (int i = 127; i >= 0; --i)
                    if (heldNotes[(size_t) i])
                    {
                        lastHeldNote = i;
                        break;
                    }
            }
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            heldNotes.fill (false);
            lastHeldNote = -1;
        }
    }
}

void HelixTuneProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int totalIn  = getTotalNumInputChannels();
    const int totalOut = getTotalNumOutputChannels();

    for (int ch = totalIn; ch < totalOut; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    updateMidiTargets (midi);

    // --- transport --------------------------------------------------------
    double timeSeconds = freeRunningTime;
    double ppq = -1.0;
    bool playing = false;

    if (auto* ph = getPlayHead())
    {
        if (const auto pos = ph->getPosition())
        {
            if (const auto t = pos->getTimeInSeconds())
                timeSeconds = *t;

            if (const auto p = pos->getPpqPosition())
                ppq = *p;

            playing = pos->getIsPlaying();
        }
    }

    playheadSeconds.store (timeSeconds, std::memory_order_relaxed);
    transportPlaying.store (playing, std::memory_order_relaxed);

    const auto settings = buildSettings();

    const bool haveMidiTarget = settings.midiTarget && lastHeldNote >= 0;
    const float midiTargetMidi = haveMidiTarget ? (float) lastHeldNote : 0.0f;

    engine.process (buffer, settings, timeSeconds, ppq,
                    &graph.getSnapshot(), midiTargetMidi, haveMidiTarget, fifo);

    freeRunningTime = timeSeconds + (double) buffer.getNumSamples() / currentSampleRate;

    // Input type changes the grain size and therefore the algorithmic delay.
    // Hosts expect to be told rather than to discover it.
    if (engine.consumeLatencyChanged())
        setLatencySamples (engine.getLatencySamples());
}

juce::AudioProcessorEditor* HelixTuneProcessor::createEditor()
{
    return new HelixTuneEditor (*this);
}

void HelixTuneProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root ("HELIXTUNE");
    root.setProperty ("version", 1, nullptr);
    root.setProperty ("noteStates", (int) noteStateBits.load(), nullptr);

    root.appendChild (apvts.copyState(), nullptr);
    root.appendChild (graph.toValueTree(), nullptr);
    root.appendChild (editorState.createCopy(), nullptr);

    juce::MemoryOutputStream stream (destData, false);
    root.writeToStream (stream);
}

void HelixTuneProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto root = juce::ValueTree::readFromData (data, (size_t) sizeInBytes);

    if (! root.isValid() || ! root.hasType ("HELIXTUNE"))
        return;

    noteStateBits.store ((uint32_t) (int) root.getProperty ("noteStates", 0));

    if (const auto state = root.getChildWithName (apvts.state.getType()); state.isValid())
        apvts.replaceState (state);

    if (const auto g = root.getChildWithName ("GRAPH"); g.isValid())
        graph.fromValueTree (g);

    if (const auto e = root.getChildWithName ("EDITOR"); e.isValid())
        editorState = e.createCopy();
}

} // namespace helix

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new helix::HelixTuneProcessor();
}
