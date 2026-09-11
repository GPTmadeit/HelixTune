#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "DSP/CorrectionEngine.h"
#include "Model/Parameters.h"
#include "Model/GraphModel.h"
#include "Model/PitchTrack.h"

namespace helix
{

class HelixTuneProcessor : public juce::AudioProcessor,
                           private juce::AsyncUpdater
{
public:
    HelixTuneProcessor();
    ~HelixTuneProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "HELIX Tune"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // --- editor-facing --------------------------------------------------
    juce::AudioProcessorValueTreeState apvts;

    PitchFifo&  getPitchFifo()  noexcept { return fifo; }
    GraphModel& getGraphModel() noexcept { return graph; }

    /** Per-pitch-class Remove / Bypass state, packed two bits per class.
        Not an automatable parameter - it is a 12-way editor state that no host
        would present usefully as 12 separate automation lanes. */
    NoteState getNoteState (int pitchClass) const noexcept;
    void setNoteState (int pitchClass, NoteState state) noexcept;
    void clearNoteStates() noexcept;

    double getPlayheadSeconds() const noexcept { return playheadSeconds.load (std::memory_order_relaxed); }
    bool   isTransportPlaying() const noexcept { return transportPlaying.load (std::memory_order_relaxed); }
    double getCurrentSampleRate() const noexcept { return currentSampleRate; }

    /** The tempo Note Transition is locked to. When the host reports none,
        this is the 120 BPM fallback and hasHostTempo() is false. */
    double getHostBpm() const noexcept   { return hostBpm.load (std::memory_order_relaxed); }
    bool   hasHostTempo() const noexcept { return hostTempoKnown.load (std::memory_order_relaxed); }

    /** Auto-Key readouts for the editor. */
    KeyDetector::Result getKeyEstimate() const noexcept { return engine.getKeyEstimate(); }
    float getChroma (int pitchClass) const noexcept { return engine.getChroma (pitchClass); }

    /** Editor persists its own view state (zoom, tool, scroll) here. */
    juce::ValueTree editorState { "EDITOR" };

private:
    CorrectionEngine::Settings buildSettings (double bpm) const noexcept;
    void updateMidiTargets (const juce::MidiBuffer& midi) noexcept;
    void emitPitchMidi (juce::MidiBuffer& midi, int numSamples) noexcept;
    void handleAsyncUpdate() override;

    CorrectionEngine engine;
    PitchFifo fifo;
    GraphModel graph;

    std::atomic<uint32_t> noteStateBits { 0 };
    std::atomic<double>   playheadSeconds { 0.0 };
    std::atomic<bool>     transportPlaying { false };
    std::atomic<double>   hostBpm { 120.0 };
    std::atomic<bool>     hostTempoKnown { false };

    double currentSampleRate = 44100.0;
    double freeRunningTime = 0.0;

    // Most recently pressed note still held, for MIDI target mode.
    std::array<bool, 128> heldNotes { };
    int lastHeldNote = -1;

    // Auto-Key hands its suggestion to the message thread; changing a
    // parameter from the audio thread is not safe.
    std::atomic<int> pendingKeyRoot { -1 };
    std::atomic<int> pendingKeyScale { -1 };
    int appliedKeyRoot = -1, appliedKeyScale = -1;

    int emittedMidiNote = -1;

    // Cached parameter pointers. Looking these up by string in processBlock
    // would be a hash lookup per parameter per block.
    struct Cache
    {
        std::atomic<float>* bypass = nullptr;
        std::atomic<float>* mix = nullptr;
        std::atomic<float>* outputGain = nullptr;
        std::atomic<float>* inputType = nullptr;
        std::atomic<float>* tracking = nullptr;
        std::atomic<float>* key = nullptr;
        std::atomic<float>* scale = nullptr;
        std::atomic<float>* retuneSpeed = nullptr;
        std::atomic<float>* humanize = nullptr;
        std::atomic<float>* flexTune = nullptr;
        std::atomic<float>* naturalVibrato = nullptr;
        std::atomic<float>* targetIgnoresVib = nullptr;
        std::atomic<float>* transpose = nullptr;
        std::atomic<float>* detune = nullptr;
        std::atomic<float>* classicMode = nullptr;
        std::atomic<float>* formantCorrect = nullptr;
        std::atomic<float>* throatLength = nullptr;
        std::atomic<float>* vibShape = nullptr;
        std::atomic<float>* vibRate = nullptr;
        std::atomic<float>* vibVariation = nullptr;
        std::atomic<float>* vibOnsetDelay = nullptr;
        std::atomic<float>* vibOnsetRate = nullptr;
        std::atomic<float>* vibPitchAmount = nullptr;
        std::atomic<float>* vibAmpAmount = nullptr;
        std::atomic<float>* vibFormantAmount = nullptr;
        std::atomic<float>* graphMode = nullptr;
        std::atomic<float>* midiTarget = nullptr;
        std::atomic<float>* pitchSmooth = nullptr;
        std::atomic<float>* sibilance = nullptr;
        std::atomic<float>* autoKey = nullptr;
        std::atomic<float>* midiOut = nullptr;
        std::atomic<float>* noteTransition = nullptr;
        std::atomic<float>* correctionAmount = nullptr;
        std::atomic<float>* harmOn = nullptr;
        std::atomic<float>* harmLevel = nullptr;
        std::atomic<float>* harmSpread = nullptr;

        struct HarmonyVoice
        {
            std::atomic<float>* enable = nullptr;
            std::atomic<float>* degrees = nullptr;
            std::atomic<float>* level = nullptr;
            std::atomic<float>* pan = nullptr;
            std::atomic<float>* formant = nullptr;
            std::atomic<float>* detune = nullptr;
        };

        std::array<HarmonyVoice, (size_t) params::numHarmonyVoices> harmony { };
    } cache;

    void buildCache();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HelixTuneProcessor)
};

} // namespace helix
