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

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

juce::StringArray getKeyNames();
juce::StringArray getScaleNames();
juce::StringArray getInputTypeNames();
juce::StringArray getVibratoShapeNames();

} // namespace helix::params
