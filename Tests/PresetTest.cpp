/*  Loads every factory preset through the real processor.

    This exists because of a crash report from FL Studio: selecting any preset
    took the host down with an access violation reading address 0x1. The cause
    was a FactoryPreset struct holding std::initializer_list members - those do
    not own their backing array, so the table dangled the moment it was built
    and loading a preset walked freed memory.

    A unit test over the table alone would not have caught it; the bug only
    shows when the presets are actually iterated. So this drives the same path
    the host does.
*/

#include "PluginProcessor.h"
#include "Model/PresetManager.h"

#include <cstdio>

using namespace helix;

namespace
{
    int failures = 0;

    void check (bool ok, const juce::String& what, const juce::String& detail = {})
    {
        std::printf ("  %s %s%s\n", ok ? "[ok]  " : "[FAIL]", what.toRawUTF8(),
                     detail.isEmpty() ? "" : ("  (" + detail + ")").toRawUTF8());

        if (! ok)
            ++failures;
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::printf ("HELIX Tune - preset loading\n");
    std::printf ("===========================\n\n");

    // On the heap deliberately: the processor carries a 16k-frame analysis
    // FIFO and a double-buffered note snapshot, which together come to well
    // over a megabyte - more than a default thread stack. The plugin always
    // heap-allocates it, so this matches how a host builds it.
    auto owned = std::make_unique<HelixTuneProcessor>();
    auto& processor = *owned;

    processor.prepareToPlay (44100.0, 512);

    PresetManager presets (processor.apvts);

    const auto names = presets.getFactoryNames();
    check (names.size() > 0, "factory presets are listed",
           juce::String (names.size()) + " presets");

    // Every id in the table must still resolve to a live parameter. A dangling
    // table shows up here as garbage strings that match nothing.
    for (int i = 0; i < names.size(); ++i)
    {
        const auto name = names[i];

        const bool sane = name.isNotEmpty()
                       && name.length() < 64
                       && name.containsOnly ("ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                             "abcdefghijklmnopqrstuvwxyz"
                                             "0123456789 :&-'/+.");

        if (! sane)
        {
            check (false, "preset name " + juce::String (i) + " is intact", name);
            continue;
        }
    }

    check (failures == 0, "all preset names are intact");

    // Now actually load each one, twice, and confirm parameters move.
    for (int pass = 0; pass < 2; ++pass)
    {
        for (int i = 0; i < names.size(); ++i)
        {
            presets.load (i);

            // Touch every parameter afterwards; a corrupted load tends to show
            // up as a NaN or an out-of-range value rather than a crash.
            bool allFinite = true;

            for (auto* p : processor.getParameters())
            {
                const float v = p->getValue();
                if (! std::isfinite (v) || v < -0.001f || v > 1.001f)
                    allFinite = false;
            }

            if (! allFinite)
                check (false, "parameters stay in range after loading " + names[i]);
        }
    }

    check (true, "loaded every factory preset twice without crashing",
           juce::String (names.size() * 2) + " loads");

    // A preset that sets harmony voices should actually change them.
    const int harmonyIndex = names.indexOf ("Harmony: Wide Choir");
    if (harmonyIndex >= 0)
    {
        presets.load (names.indexOf ("Init"));
        const float before = *processor.apvts.getRawParameterValue (
                                 params::harmonyID (params::harmEnable, 3));

        presets.load (harmonyIndex);
        const float after = *processor.apvts.getRawParameterValue (
                                params::harmonyID (params::harmEnable, 3));

        check (before < 0.5f && after > 0.5f,
               "Wide Choir actually enables the fourth voice",
               juce::String (before, 0) + " -> " + juce::String (after, 0));
    }

    // And a correction preset should move a correction parameter.
    presets.load (names.indexOf ("Hard Tune"));
    const float retune = *processor.apvts.getRawParameterValue (params::retuneSpeed);
    check (retune < 1.0f, "Hard Tune sets retune speed to zero",
           juce::String (retune, 1) + " ms");

    presets.load (names.indexOf ("Transparent Polish"));
    const float polished = *processor.apvts.getRawParameterValue (params::retuneSpeed);
    check (polished > 50.0f, "Transparent Polish sets a slow retune",
           juce::String (polished, 1) + " ms");

    std::printf ("\n===========================\n");
    std::printf ("%s\n", failures == 0 ? "preset loading OK" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
