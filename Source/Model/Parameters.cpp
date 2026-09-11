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
    // Order must match InputType, and new entries go on the end - the host
    // stores this parameter as an index.
    return { "Soprano", "Alto / Tenor", "Low Male", "Instrument", "Bass Instrument",
             "Generic (All Ranges)" };
}

juce::StringArray getVibratoShapeNames()
{
    return { "Sine", "Triangle", "Square", "Saw Up", "Saw Down" };
}

static juce::String msSuffix (float v, int)      { return juce::String (v, 1) + " ms"; }
static juce::String centsSuffix (float v, int)   { return juce::String (v, 1) + " ct"; }
static juce::String percentSuffix (float v, int) { return juce::String (v, 0) + " %"; }
static juce::String hzSuffix (float v, int)      { return juce::String (v, 2) + " Hz"; }

/** Scale steps read as interval names, because "+2" means a third here and
    nobody counts scale degrees from zero in their head. */
static juce::String intervalName (int degrees, int)
{
    if (degrees == 0)
        return "Unison";

    static const char* names[] = { "8ve", "2nd", "3rd", "4th", "5th", "6th", "7th" };

    const int mag = std::abs (degrees);
    const int octaves = mag / 7;
    const int step = mag % 7;

    juce::String text (names[step]);
    if (step == 0)
        text = (octaves > 1) ? juce::String (octaves) + " 8ve" : juce::String ("8ve");
    else if (octaves > 0)
        text += " +" + juce::String (octaves) + "8ve";

    return (degrees > 0 ? "+" : "-") + text;
}

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

    // --- advanced tracking ------------------------------------------------
    layout.add (std::make_unique<APF>   (pid (pitchSmooth), "Pitch Stability",
                                         juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 55.0f,
                                         juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentSuffix)));
    layout.add (std::make_unique<APF>   (pid (sibilance), "Sibilance Guard",
                                         juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 60.0f,
                                         juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentSuffix)));
    layout.add (std::make_unique<APB>   (pid (autoKey), "Auto-Key", false));
    layout.add (std::make_unique<APB>   (pid (midiOut), "MIDI Pitch Out", false));

    // --- harmony ----------------------------------------------------------
    layout.add (std::make_unique<APF>   (pid (harmLevel), "Harmony Level",
                                         juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 70.0f,
                                         juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentSuffix)));
    layout.add (std::make_unique<APF>   (pid (harmSpread), "Harmony Spread",
                                         juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 35.0f,
                                         juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentSuffix)));

    // Sensible opening chord: a third and a fifth up, an octave down, spread
    // across the image, so switching harmony on immediately does something
    // musical instead of nothing.
    const int    defaultDegrees[numHarmonyVoices] = {  2,  4, -7,  7 };
    const float  defaultPan[numHarmonyVoices]     = { -0.55f, 0.55f, -0.2f, 0.25f };
    const float  defaultFormant[numHarmonyVoices] = { 1.06f, 0.94f, 1.12f, 0.9f };
    const float  defaultDetune[numHarmonyVoices]  = { -6.0f, 5.0f, 8.0f, -9.0f };

    for (int v = 0; v < numHarmonyVoices; ++v)
    {
        const auto suffix = juce::String (" ") + juce::String (v + 1);

        layout.add (std::make_unique<APB> (
            juce::ParameterID { harmonyID (harmEnable, v), 1 }, "Harmony" + suffix + " On",
            v == 0));

        layout.add (std::make_unique<APInt> (
            juce::ParameterID { harmonyID (harmDegrees, v), 1 }, "Harmony" + suffix + " Interval",
            -14, 14, defaultDegrees[v],
            juce::AudioParameterIntAttributes().withStringFromValueFunction (intervalName)));

        layout.add (std::make_unique<APF> (
            juce::ParameterID { harmonyID (harmVoiceLevel, v), 1 }, "Harmony" + suffix + " Level",
            juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 75.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentSuffix)));

        layout.add (std::make_unique<APF> (
            juce::ParameterID { harmonyID (harmPan, v), 1 }, "Harmony" + suffix + " Pan",
            juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), defaultPan[v] * 100.0f));

        layout.add (std::make_unique<APF> (
            juce::ParameterID { harmonyID (harmFormant, v), 1 }, "Harmony" + suffix + " Formant",
            juce::NormalisableRange<float> (0.5f, 2.0f, 0.001f, 1.0f), defaultFormant[v]));

        layout.add (std::make_unique<APF> (
            juce::ParameterID { harmonyID (harmDetune, v), 1 }, "Harmony" + suffix + " Detune",
            juce::NormalisableRange<float> (-50.0f, 50.0f, 0.1f), defaultDetune[v],
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (centsSuffix)));
    }

    // Last in the layout so no existing parameter moves. Off by default: a
    // pitch corrector dropped on a vocal should correct it, not add singers.
    layout.add (std::make_unique<APB> (pid (harmOn), "Harmony", false));

    // A choice rather than a float, so the knob clicks between note values
    // instead of sweeping through the milliseconds between them.
    layout.add (std::make_unique<APC> (pid (noteTransition), "Note Transition",
                                       getTransitionNames(), 0));

    layout.add (std::make_unique<APF> (pid (correctionAmount), "Correction Amount",
                                       juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
                                       juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentSuffix)));

    return layout;
}

juce::StringArray getTransitionNames()
{
    juce::StringArray names { "Off", "1/16T", "1/16", "1/8T", "1/8", "1/4T", "1/4" };
    jassert (names.size() == kNumTransitionSteps);
    return names;
}

bool legacyHarmonyWasOn (const juce::ValueTree& parameterState)
{
    for (int v = 0; v < numHarmonyVoices; ++v)
    {
        const auto child = parameterState.getChildWithProperty ("id", harmonyID (harmEnable, v));

        // A voice missing from the state was at its default, and only the
        // first voice defaults on.
        const bool on = child.isValid() ? (float) child.getProperty ("value") > 0.5f
                                        : v == 0;
        if (on)
            return true;
    }

    return false;
}

} // namespace helix::params
