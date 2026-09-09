#include "Parameters.h"
#include "../DSP/ScaleQuantizer.h"

namespace helix::params
{

using APF   = juce::AudioParameterFloat;
using APB   = juce::AudioParameterBool;
using APC   = juce::AudioParameterChoice;
using APInt = juce::AudioParameterInt;

juce::StringArray getKeyNames()
{
    return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
}

juce::StringArray getScaleNames()
{
    juce::StringArray names;
    const auto* table = getScaleTable();

    for (int i = 0; i < getNumScales(); ++i)
        names.add (table[i].name);

    return names;
}

juce::StringArray getInputTypeNames()
{
    return { "Soprano", "Alto / Tenor", "Low Male", "Instrument", "Bass Instrument" };
}

juce::StringArray getVibratoShapeNames()
{
    return { "Sine", "Triangle", "Square", "Saw Up", "Saw Down" };
}

static juce::String msSuffix (float v, int)      { return juce::String (v, 1) + " ms"; }
static juce::String centsSuffix (float v, int)   { return juce::String (v, 1) + " ct"; }
static juce::String percentSuffix (float v, int) { return juce::String (v, 0) + " %"; }
static juce::String hzSuffix (float v, int)      { return juce::String (v, 2) + " Hz"; }

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Version hint 1 on every parameter: bumping it later is how a host is
    // told that a parameter's meaning changed between plugin versions.
    auto pid = [] (const char* id) { return juce::ParameterID { id, 1 }; };

    // --- global -----------------------------------------------------------
    layout.add (std::make_unique<APB>   (pid (bypass), "Bypass", false));
    layout.add (std::make_unique<APF>   (pid (mix), "Mix",
                                         juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
                                         juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentSuffix)));
    layout.add (std::make_unique<APF>   (pid (outputGain), "Output",
                                         juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
                                         juce::AudioParameterFloatAttributes().withLabel ("dB")));

    // --- detection --------------------------------------------------------
    layout.add (std::make_unique<APC>   (pid (inputType), "Input Type", getInputTypeNames(), 1));
    layout.add (std::make_unique<APF>   (pid (tracking), "Tracking",
                                         juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f,
                                         juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentSuffix)));

    // --- scale ------------------------------------------------------------
    layout.add (std::make_unique<APC>   (pid (key), "Key", getKeyNames(), 0));
    layout.add (std::make_unique<APC>   (pid (scale), "Scale", getScaleNames(), 0));

    // --- correction -------------------------------------------------------
    // Retune Speed is skewed so the musically dense 0-60 ms region - snap
    // through to natural - occupies most of the knob's travel.
    layout.add (std::make_unique<APF>   (pid (retuneSpeed), "Retune Speed",
                                         juce::NormalisableRange<float> (0.0f, 400.0f, 0.1f, 0.35f), 20.0f,
                                         juce::AudioParameterFloatAttributes().withStringFromValueFunction (msSuffix)));
    layout.add (std::make_unique<APF>   (pid (humanize), "Humanize",
                                         juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
                                         juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentSuffix)));
    layout.add (std::make_unique<APF>   (pid (flexTune), "Flex-Tune",
                                         juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
                                         juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentSuffix)));
    layout.add (std::make_unique<APF>   (pid (naturalVibrato), "Natural Vibrato",
                                         juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<APB>   (pid (targetIgnoresVib), "Targeting Ignores Vibrato", false));
    layout.add (std::make_unique<APInt> (pid (transpose), "Transpose", -12, 12, 0));
    layout.add (std::make_unique<APF>   (pid (detune), "Detune",
                                         juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f,
                                         juce::AudioParameterFloatAttributes().withStringFromValueFunction (centsSuffix)));
    layout.add (std::make_unique<APB>   (pid (classicMode), "Classic Mode", false));

    // --- formant ----------------------------------------------------------
    layout.add (std::make_unique<APB>   (pid (formantCorrect), "Formant Correction", true));
    layout.add (std::make_unique<APF>   (pid (throatLength), "Throat Length",
                                         juce::NormalisableRange<float> (0.5f, 2.0f, 0.001f, 1.0f), 1.0f));

    // --- vibrato ----------------------------------------------------------
    layout.add (std::make_unique<APC>   (pid (vibShape), "Vibrato Shape", getVibratoShapeNames(), 0));
    layout.add (std::make_unique<APF>   (pid (vibRate), "Vibrato Rate",
                                         juce::NormalisableRange<float> (0.1f, 10.0f, 0.01f, 0.6f), 5.5f,
                                         juce::AudioParameterFloatAttributes().withStringFromValueFunction (hzSuffix)));
    layout.add (std::make_unique<APF>   (pid (vibVariation), "Vibrato Variation",
                                         juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
                                         juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentSuffix)));
    layout.add (std::make_unique<APF>   (pid (vibOnsetDelay), "Onset Delay",
                                         juce::NormalisableRange<float> (0.0f, 1500.0f, 1.0f), 300.0f,
                                         juce::AudioParameterFloatAttributes().withStringFromValueFunction (msSuffix)));
    layout.add (std::make_unique<APF>   (pid (vibOnsetRate), "Onset Rate",
                                         juce::NormalisableRange<float> (0.0f, 1500.0f, 1.0f), 500.0f,
                                         juce::AudioParameterFloatAttributes().withStringFromValueFunction (msSuffix)));
    layout.add (std::make_unique<APF>   (pid (vibPitchAmount), "Vibrato Pitch",
                                         juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
                                         juce::AudioParameterFloatAttributes().withStringFromValueFunction (centsSuffix)));
    layout.add (std::make_unique<APF>   (pid (vibAmpAmount), "Vibrato Amplitude",
                                         juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
                                         juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentSuffix)));
    layout.add (std::make_unique<APF>   (pid (vibFormantAmount), "Vibrato Formant",
                                         juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
                                         juce::AudioParameterFloatAttributes().withStringFromValueFunction (centsSuffix)));

    // --- mode -------------------------------------------------------------
    layout.add (std::make_unique<APB>   (pid (graphMode), "Graph Mode", false));
    layout.add (std::make_unique<APB>   (pid (midiTarget), "MIDI Target Notes", false));

    return layout;
}

} // namespace helix::params
