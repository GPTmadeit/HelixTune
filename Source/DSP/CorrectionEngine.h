#pragma once

#include "PitchDetector.h"
#include "PitchStabilizer.h"
#include "PsolaShifter.h"
#include "ScaleQuantizer.h"
#include "RetuneEngine.h"
#include "VibratoGenerator.h"
#include "KeyDetector.h"
#include "TransientGuard.h"
#include "FormantProcessor.h"
#include "HarmonyEngine.h"
#include "../Model/PitchTrack.h"
#include "../Model/GraphModel.h"

namespace helix
{

/** Ties the whole correction chain together and runs it on a fixed analysis
    hop, independent of whatever block size the host hands us.

    Detection runs once on the channel sum; the resulting pitch ratio is then
    applied to every channel by its own shifter. Detecting per channel would
    let a stereo pair drift apart and smear the image.

    Signal order for the lead voice is pitch first, then formants: PSOLA moves
    the pulse rate while preserving the spectral envelope, and the LPC filter
    then moves the envelope by exactly as much as the user asked for. Doing it
    in this order means the two controls do not interact.
*/
class CorrectionEngine
{
public:
    struct Settings
    {
        InputType inputType = InputType::altoTenor;
        float     tracking  = 0.5f;
        float     pitchSmoothing = 0.55f;
        float     sibilanceGuard = 0.6f;

        int      key = 0;
        int      scaleIndex = 0;
        uint32_t noteStateBits = 0;      // 2 bits per pitch class, see NoteState

        RetuneEngine::Params     retune;
        VibratoGenerator::Params vibrato;
        HarmonyEngine::Params    harmony;

        bool  formantCorrection = true;
        float throatLength = 1.0f;       // 0.5 (short) .. 2.0 (long)

        bool  graphMode  = false;
        bool  midiTarget = false;
        bool  bypass     = false;

        float mix        = 1.0f;         // 0 = dry, 1 = corrected
        float outputGain = 1.0f;         // linear
    };

    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset() noexcept;

    int  getLatencySamples() const noexcept { return latency; }

    /** True once after the input type changed the algorithmic delay. The
        processor uses this to re-report latency to the host. */
    bool consumeLatencyChanged() noexcept;

    void process (juce::AudioBuffer<float>& buffer,
                  const Settings& settings,
                  double blockStartSeconds,
                  double blockStartPpq,
                  const NoteSnapshot* graph,
                  float midiTargetMidi,
                  bool haveMidiTarget,
                  PitchFifo& fifo) noexcept;

    /** Auto-Key result, safe to read from the editor. */
    KeyDetector::Result getKeyEstimate() const noexcept { return keyDetector.getEstimate(); }
    float getChroma (int pitchClass) const noexcept { return keyDetector.getChroma (pitchClass); }

    /** Latest stabilised note, for MIDI output. */
    float getLastPitchRatio() const noexcept { return curPitchRatio; }
    float getLastPeriod() const noexcept { return curPeriod; }
    float getLiveMidiNote() const noexcept { return liveMidi; }
    bool  isLiveVoiced() const noexcept { return liveVoiced; }

private:
    void applySettings (const Settings& s) noexcept;
    void runAnalysisHop (const Settings& s, double hopTime, double hopPpq,
                         const NoteSnapshot* graph, float midiTargetMidi,
                         bool haveMidiTarget, PitchFifo& fifo) noexcept;

    double fs = 44100.0;
    int    hopSize = 256;
    int    hopCounter = 0;
    int    latency = 0;
    int    numCh = 2;
    bool   latencyDirty = false;

    PitchDetector    detector;
    PitchStabilizer  stabilizer;
    ScaleQuantizer   quantizer;
    RetuneEngine     retune;
    VibratoGenerator vibrato;
    KeyDetector      keyDetector;
    TransientGuard   transientGuard;
    FormantProcessor formantProc;
    HarmonyEngine    harmony;
    std::vector<PsolaShifter> shifters;

    // Control values held constant across each analysis hop.
    float curPitchRatio   = 1.0f;
    float curFormantRatio = 1.0f;
    float curPeriod       = 200.0f;
    float curGain         = 1.0f;
    bool  curVoiced       = false;
    float liveMidi        = 0.0f;
    bool  liveVoiced      = false;

    int      lastInputType   = -1;
    uint32_t lastNoteStates  = 0xFFFFFFFF;
    int      lastKey         = -1;
    int      lastScale       = -1;

    std::vector<float> mono;        // channel sum for analysis
    std::vector<float> history;     // newest getMaxFrameSize() samples
    std::vector<float> harmonyL, harmonyR;
    float lastRms = 0.0f;

    // Delayed dry, for the Mix control and for hard bypass, so switching
    // either one does not shift the signal in time.
    std::vector<std::vector<float>> dryRing;
    int64_t dryWrite = 0;
    int64_t dryMask = 0;
};

} // namespace helix
