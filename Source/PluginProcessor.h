#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "DSP/CorrectionEngine.h"
#include "Model/Parameters.h"
#include "Model/GraphModel.h"
#include "Model/PitchTrack.h"

namespace helix
{

class HelixTuneProcessor : public juce::AudioProcessor
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
    bool producesMidi() const override { return false; }
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

    /** Editor persists its own view state (zoom, tool, scroll) here. */
    juce::ValueTree editorState { "EDITOR" };

private:
    CorrectionEngine::Settings buildSettings() const noexcept;
    void updateMidiTargets (const juce::MidiBuffer& midi) noexcept;

    CorrectionEngine engine;
    PitchFifo fifo;
    GraphModel graph;

    std::atomic<uint32_t> noteStateBits { 0 };
    std::atomic<double>   playheadSeconds { 0.0 };
    std::atomic<bool>     transportPlaying { false };

    double currentSampleRate = 44100.0;
    double freeRunningTime = 0.0;

    // Most recently pressed note still held, for MIDI target mode.
    std::array<bool, 128> heldNotes { };
    int lastHeldNote = -1;

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
    } cache;

    void buildCache();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HelixTuneProcessor)
};

} // namespace helix
