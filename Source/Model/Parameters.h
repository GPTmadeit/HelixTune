#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace helix::params
{

// Parameter IDs. These are persisted in host sessions, so they are frozen:
// renaming one silently breaks every saved project that used it.
inline constexpr const char* bypass          = "bypass";
inline constexpr const char* mix             = "mix";
inline constexpr const char* outputGain      = "outputGain";

inline constexpr const char* inputType       = "inputType";
inline constexpr const char* tracking        = "tracking";

inline constexpr const char* key             = "key";
inline constexpr const char* scale           = "scale";

inline constexpr const char* retuneSpeed     = "retuneSpeed";
inline constexpr const char* humanize        = "humanize";
inline constexpr const char* flexTune        = "flexTune";
inline constexpr const char* naturalVibrato  = "naturalVibrato";
inline constexpr const char* targetIgnoresVib= "targetIgnoresVib";
inline constexpr const char* transpose       = "transpose";
inline constexpr const char* detune          = "detune";
inline constexpr const char* classicMode     = "classicMode";

inline constexpr const char* formantCorrect  = "formantCorrect";
inline constexpr const char* throatLength    = "throatLength";

inline constexpr const char* vibShape        = "vibShape";
inline constexpr const char* vibRate         = "vibRate";
inline constexpr const char* vibVariation    = "vibVariation";
inline constexpr const char* vibOnsetDelay   = "vibOnsetDelay";
inline constexpr const char* vibOnsetRate    = "vibOnsetRate";
inline constexpr const char* vibPitchAmount  = "vibPitchAmount";
inline constexpr const char* vibAmpAmount    = "vibAmpAmount";
inline constexpr const char* vibFormantAmount= "vibFormantAmount";

inline constexpr const char* graphMode       = "graphMode";
inline constexpr const char* midiTarget      = "midiTarget";

// --- advanced tracking / naturalness ---------------------------------------
inline constexpr const char* pitchSmooth     = "pitchSmooth";
inline constexpr const char* sibilance       = "sibilance";
inline constexpr const char* autoKey         = "autoKey";
inline constexpr const char* midiOut         = "midiOut";

// --- harmony ---------------------------------------------------------------
inline constexpr int numHarmonyVoices = 4;

inline constexpr const char* harmLevel  = "harmLevel";
inline constexpr const char* harmSpread = "harmSpread";

/** Per-voice IDs are generated rather than listed, so adding a fifth voice is
    a constant change instead of twenty-odd new declarations. The generated
    strings are still stable and still persisted, so the naming must not move. */
inline juce::String harmonyID (const char* base, int voiceIndex)
{
    return juce::String (base) + juce::String (voiceIndex + 1);
}

inline constexpr const char* harmEnable  = "harmEnable";
inline constexpr const char* harmDegrees = "harmDegrees";
inline constexpr const char* harmVoiceLevel = "harmVoiceLevel";
inline constexpr const char* harmPan     = "harmPan";
inline constexpr const char* harmFormant = "harmFormant";
inline constexpr const char* harmDetune  = "harmDetune";

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

juce::StringArray getKeyNames();
juce::StringArray getScaleNames();
juce::StringArray getInputTypeNames();
juce::StringArray getVibratoShapeNames();

} // namespace helix::params
