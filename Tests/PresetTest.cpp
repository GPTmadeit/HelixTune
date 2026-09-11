/*  Loads every factory preset through the real processor.

    This exists because of a crash report from FL Studio: selecting any preset
    took the host down with an access violation reading address 0x1. The cause
    was a FactoryPreset struct holding std::initializer_list members - those do
    not own their backing array, so the table dangled the moment it was built
    and loading a preset walked freed memory.

    A unit test over the table alone would not have caught it; the bug only
    shows when the presets are actually iterated. So this drives the same path
    the host does.

    It also covers the master Harmony switch, which lives in the processor's
    parameter-to-settings mapping and in its state migration - neither of which
    the DSP test can reach.
*/

#include "PluginProcessor.h"
#include "Model/PresetManager.h"
#include "Model/UpdateChecker.h"

#include <cmath>
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

    // On the heap deliberately: the processor carries a 16k-frame analysis
    // FIFO and a double-buffered note snapshot, which together come to well
    // over a megabyte - more than a default thread stack. The plugin always
    // heap-allocates it, so this matches how a host builds it.
    std::unique_ptr<HelixTuneProcessor> makeProcessor()
    {
        auto p = std::make_unique<HelixTuneProcessor>();
        p->prepareToPlay (44100.0, 512);
        return p;
    }

    void setParam (HelixTuneProcessor& p, const juce::String& id, float value)
    {
        if (auto* param = p.apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    float getParam (HelixTuneProcessor& p, const juce::String& id)
    {
        return *p.apvts.getRawParameterValue (id);
    }

    /** Two seconds of a harmonic tone through processBlock; returns the left
        channel. */
    std::vector<float> render (HelixTuneProcessor& p, double hz)
    {
        const double sr = 44100.0;
        const int blockSize = 512;
        const int total = (int) (sr * 2.0);

        std::vector<float> out;
        out.reserve ((size_t) total);

        juce::AudioBuffer<float> block (2, blockSize);
        juce::MidiBuffer midi;
        double phase = 0.0;

        for (int pos = 0; pos + blockSize <= total; pos += blockSize)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                double v = 0.0;
                for (int h = 1; h <= 6; ++h)
                    v += std::sin (phase * h) / (double) h;

                phase += 2.0 * juce::MathConstants<double>::pi * hz / sr;

                block.setSample (0, i, (float) (v * 0.15));
                block.setSample (1, i, (float) (v * 0.15));
            }

            midi.clear();
            p.processBlock (block, midi);
            out.insert (out.end(), block.getReadPointer (0), block.getReadPointer (0) + blockSize);
        }

        return out;
    }

    /** A state blob as 1.0.x would have saved it: no master switch entry. */
    juce::MemoryBlock legacyState (bool firstVoiceOn)
    {
        auto p = makeProcessor();

        for (int v = 0; v < params::numHarmonyVoices; ++v)
            setParam (*p, params::harmonyID (params::harmEnable, v), (firstVoiceOn && v == 0) ? 1.0f : 0.0f);

        juce::MemoryBlock saved;
        p->getStateInformation (saved);

        auto root = juce::ValueTree::readFromData (saved.getData(), saved.getSize());
        auto paramTree = root.getChildWithName (p->apvts.state.getType());
        paramTree.removeChild (paramTree.getChildWithProperty ("id", params::harmOn), nullptr);

        juce::MemoryBlock out;
        {
            juce::MemoryOutputStream stream (out, false);
            root.writeToStream (stream);
        }

        return out;
    }
}

static void testPresetLoading()
{
    std::printf ("Factory presets\n");

    auto owned = makeProcessor();
    auto& processor = *owned;

    PresetManager presets (processor.apvts);

    const auto names = presets.getFactoryNames();
    check (names.size() > 0, "factory presets are listed",
           juce::String (names.size()) + " presets");

    // Every id in the table must still resolve to a live parameter. A dangling
    // table shows up here as garbage strings that match nothing.
    const int failuresBefore = failures;

    for (int i = 0; i < names.size(); ++i)
    {
        const auto name = names[i];

        const bool sane = name.isNotEmpty()
                       && name.length() < 64
                       && name.containsOnly ("ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                             "abcdefghijklmnopqrstuvwxyz"
                                             "0123456789 :&-'/+.");

        if (! sane)
            check (false, "preset name " + juce::String (i) + " is intact", name);
    }

    check (failures == failuresBefore, "all preset names are intact");

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
        setParam (processor, params::harmonyID (params::harmEnable, 3), 0.0f);
        presets.load (harmonyIndex);

        check (getParam (processor, params::harmonyID (params::harmEnable, 3)) > 0.5f,
               "Wide Choir actually enables the fourth voice");
    }

    // Every harmony preset has to switch the bus on, or it would load and
    // appear to do nothing.
    bool allOn = true;
    int harmonyPresets = 0;

    for (int i = 0; i < names.size(); ++i)
    {
        if (! names[i].startsWith ("Harmony:"))
            continue;

        ++harmonyPresets;
        setParam (processor, params::harmOn, 0.0f);
        presets.load (i);
        allOn = allOn && getParam (processor, params::harmOn) > 0.5f;
    }

    check (harmonyPresets > 0 && allOn, "every harmony preset switches the harmony bus on",
           juce::String (harmonyPresets) + " presets");

    presets.load (names.indexOf ("Init"));
    check (getParam (processor, params::harmOn) < 0.5f, "Init switches harmony off");

    // And a correction preset should move a correction parameter.
    presets.load (names.indexOf ("Hard Tune"));
    const float retune = getParam (processor, params::retuneSpeed);
    check (retune < 1.0f, "Hard Tune sets retune speed to zero",
           juce::String (retune, 1) + " ms");

    presets.load (names.indexOf ("Transparent Polish"));
    const float polished = getParam (processor, params::retuneSpeed);
    check (polished > 50.0f, "Transparent Polish sets a slow retune",
           juce::String (polished, 1) + " ms");
}

static void testHarmonyMasterSwitch()
{
    std::printf ("\nHarmony master switch\n");

    // Voice 1 defaults on. Before the master switch existed, that meant every
    // freshly inserted instance added a third above the vocal.
    auto fresh = makeProcessor();
    check (getParam (*fresh, params::harmOn) < 0.5f, "a fresh instance starts with harmony off");

    auto leadOnly = makeProcessor();
    for (int v = 0; v < params::numHarmonyVoices; ++v)
        setParam (*leadOnly, params::harmonyID (params::harmEnable, v), 0.0f);

    const auto freshOut = render (*fresh, 220.0);
    const auto leadOut  = render (*leadOnly, 220.0);

    float maxDiff = 0.0f;
    for (size_t i = 0; i < freshOut.size(); ++i)
        maxDiff = juce::jmax (maxDiff, std::abs (freshOut[i] - leadOut[i]));

    check (maxDiff == 0.0f, "with the switch off, an enabled voice changes nothing",
           "max difference " + juce::String (maxDiff, 9));

    auto on = makeProcessor();
    setParam (*on, params::harmOn, 1.0f);
    const auto onOut = render (*on, 220.0);

    double energy = 0.0;
    for (size_t i = onOut.size() / 2; i < onOut.size(); ++i)
    {
        const double d = onOut[i] - leadOut[i];
        energy += d * d;
    }

    const double rms = std::sqrt (energy / (double) (onOut.size() / 2));
    check (rms > 0.01, "switching it on renders the voices",
           "harmony rms " + juce::String (rms, 4));

    // Sessions saved by 1.0.x have no value for the switch. Those versions
    // played harmony whenever a voice was on, so that has to survive reopening.
    for (bool voiceOn : { true, false })
    {
        const auto blob = legacyState (voiceOn);

        auto p = makeProcessor();
        setParam (*p, params::harmOn, voiceOn ? 0.0f : 1.0f);   // start opposite
        p->setStateInformation (blob.getData(), (int) blob.getSize());

        const bool restored = getParam (*p, params::harmOn) > 0.5f;
        check (restored == voiceOn,
               voiceOn ? "1.0.x session with a voice on reopens with harmony on"
                       : "1.0.x session with every voice off reopens with harmony off");
    }

    // A current session must round-trip the switch as saved, not re-infer it.
    {
        auto p = makeProcessor();
        setParam (*p, params::harmOn, 0.0f);   // voice 1 still on

        juce::MemoryBlock saved;
        p->getStateInformation (saved);

        auto q = makeProcessor();
        setParam (*q, params::harmOn, 1.0f);
        q->setStateInformation (saved.getData(), (int) saved.getSize());

        check (getParam (*q, params::harmOn) < 0.5f,
               "a saved 'harmony off' is kept even with voices enabled");
    }
}

/** The updater downloads a file and runs it with admin rights, so what it
    will accept has to be pinned down exactly. */
static void testUpdaterSafety()
{
    std::printf ("\nUpdater safety\n");

    check (UpdateChecker::isTrustedInstallerUrl (
               "https://github.com/GPTmadeit/HelixTune/releases/download/v1.1.1/HELIX-Tune-1.1.1-Windows.exe"),
           "accepts this repository's own release installer");

    const char* hostile[] =
    {
        "http://github.com/GPTmadeit/HelixTune/releases/download/v1.1.1/HELIX-Tune-1.1.1-Windows.exe",
        "https://github.com.evil.example/GPTmadeit/HelixTune/releases/download/v1/x.exe",
        "https://github.com@evil.example/GPTmadeit/HelixTune/releases/download/v1/x.exe",
        "https://evil.example/GPTmadeit/HelixTune/releases/download/v1/x.exe",
        "https://github.com/SomeoneElse/HelixTune/releases/download/v1/x.exe",
        "https://github.com/GPTmadeit/OtherRepo/releases/download/v1/x.exe",
        "https://github.com/GPTmadeit/HelixTune/releases/download/../../../evil/x.exe",
        "https://github.com/GPTmadeit/HelixTune/releases/download/v1/%2e%2e/x.exe",
        "https://github.com/GPTmadeit/HelixTune/releases/download/v1/x.exe?next=https://evil.example",
        "https://github.com/GPTmadeit/HelixTune/releases/download/v1/x.exe#fragment",
        "https://github.com/GPTmadeit/HelixTune/releases/download/v1/sub/x.exe",
        "https://github.com/GPTmadeit/HelixTune/releases/download/v1/x.zip",
        "https://github.com/GPTmadeit/HelixTune/releases/download/v1/",
        "HTTPS://GITHUB.COM/GPTmadeit/HelixTune/releases/download/v1/x.exe",
        ""
    };

    int accepted = 0;
    for (const auto* u : hostile)
    {
        if (UpdateChecker::isTrustedInstallerUrl (u))
        {
            ++accepted;
            std::printf ("         wrongly accepted: %s\n", u);
        }
    }

    check (accepted == 0, "refuses every other host, owner, scheme, path and file type",
           juce::String ((int) (sizeof (hostile) / sizeof (hostile[0]))) + " hostile URLs");

    const juce::String abc ("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    check (UpdateChecker::parseSha256Digest ("sha256:" + abc) == abc
               && UpdateChecker::parseSha256Digest ("sha256:" + abc.toUpperCase()) == abc
               && UpdateChecker::parseSha256Digest ("md5:" + abc).isEmpty()
               && UpdateChecker::parseSha256Digest ("sha256:xyz").isEmpty()
               && UpdateChecker::parseSha256Digest ({}).isEmpty(),
           "parses only well-formed sha256 digests");

    // The published SHA-256 test vector: "abc".
    const auto file = juce::File::createTempFile (".bin");
    file.replaceWithData ("abc", 3);

    check (UpdateChecker::verifyDownload (file, 3, abc), "a file matching size and SHA-256 is accepted");
    check (! UpdateChecker::verifyDownload (file, 4, abc), "a size mismatch is refused");
    check (! UpdateChecker::verifyDownload (file, 3, abc.replaceCharacter ('b', 'c')), "a digest mismatch is refused");
    check (! UpdateChecker::verifyDownload (file, 3, {}), "a missing digest is refused, not waved through");

    file.deleteFile();
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::printf ("HELIX Tune - processor checks\n");
    std::printf ("=============================\n\n");

    testPresetLoading();
    testHarmonyMasterSwitch();
    testUpdaterSafety();

    std::printf ("\n=============================\n");
    std::printf ("%s\n", failures == 0 ? "all processor checks OK" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
