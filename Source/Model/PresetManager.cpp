#include "PresetManager.h"
#include "Parameters.h"

namespace helix
{

namespace
{
    struct Setting { const char* id; float value; };

    struct FactoryPreset
    {
        const char* name;

        // Must own its elements. A std::initializer_list member does not: the
        // backing array lives only as long as the full-expression that created
        // it, so storing one leaves a dangling pointer the moment the table is
        // constructed, and loading a preset then walks freed memory.
        std::vector<Setting> settings;
    };

    // Values are in each parameter's own units, not normalised.
    const std::vector<FactoryPreset>& factoryPresets()
    {
        static const std::vector<FactoryPreset> presets =
        {
            { "Init", {
                { params::retuneSpeed, 20.0f }, { params::humanize, 0.0f },
                { params::flexTune, 0.0f }, { params::naturalVibrato, 0.0f },
                { params::transpose, 0.0f }, { params::detune, 0.0f },
                { params::formantCorrect, 1.0f }, { params::throatLength, 1.0f },
                { params::classicMode, 0.0f }, { params::vibPitchAmount, 0.0f },
                { params::pitchSmooth, 55.0f }, { params::sibilance, 60.0f },
                { params::correctionAmount, 100.0f }, { params::noteTransition, 0.0f },
                { params::harmOn, 0.0f } } },

            { "Natural Vocal", {
                { params::retuneSpeed, 48.0f }, { params::humanize, 45.0f },
                { params::flexTune, 40.0f }, { params::naturalVibrato, 2.0f },
                { params::targetIgnoresVib, 1.0f },
                { params::formantCorrect, 1.0f }, { params::sibilance, 70.0f },
                { params::pitchSmooth, 60.0f }, { params::classicMode, 0.0f } } },

            { "Transparent Polish", {
                { params::retuneSpeed, 95.0f }, { params::humanize, 65.0f },
                { params::flexTune, 65.0f }, { params::naturalVibrato, 4.0f },
                { params::targetIgnoresVib, 1.0f }, { params::sibilance, 80.0f },
                { params::pitchSmooth, 70.0f } } },

            { "Hard Tune", {
                { params::retuneSpeed, 0.0f }, { params::humanize, 0.0f },
                { params::flexTune, 0.0f }, { params::naturalVibrato, -12.0f },
                { params::sibilance, 25.0f }, { params::pitchSmooth, 40.0f },
                { params::formantCorrect, 1.0f }, { params::scale, 1.0f } } },

            { "Classic Hardware", {
                { params::retuneSpeed, 18.0f }, { params::classicMode, 1.0f },
                { params::humanize, 15.0f }, { params::flexTune, 0.0f },
                { params::sibilance, 40.0f } } },

            { "Robot", {
                { params::retuneSpeed, 0.0f }, { params::humanize, 0.0f },
                { params::flexTune, 0.0f }, { params::naturalVibrato, -12.0f },
                { params::formantCorrect, 0.0f }, { params::sibilance, 0.0f },
                { params::vibPitchAmount, 0.0f } } },

            { "Bigger Throat", {
                { params::retuneSpeed, 45.0f }, { params::throatLength, 1.35f },
                { params::formantCorrect, 1.0f }, { params::transpose, -2.0f } } },

            { "Lift & Brighten", {
                { params::retuneSpeed, 40.0f }, { params::throatLength, 0.82f },
                { params::formantCorrect, 1.0f }, { params::detune, 3.0f } } },

            { "Performed Vibrato", {
                { params::retuneSpeed, 25.0f }, { params::targetIgnoresVib, 1.0f },
                { params::vibPitchAmount, 34.0f }, { params::vibRate, 5.6f },
                { params::vibVariation, 30.0f }, { params::vibOnsetDelay, 420.0f },
                { params::vibOnsetRate, 600.0f }, { params::vibAmpAmount, 18.0f } } },
        };

        return presets;
    }

    // Harmony presets need per-voice values, which the sparse table cannot
    // express, so they are applied programmatically instead.
    struct HarmonyPreset
    {
        const char* name;
        int   degrees[params::numHarmonyVoices];
        bool  enabled[params::numHarmonyVoices];
        float pan[params::numHarmonyVoices];
        float formant[params::numHarmonyVoices];
        float level;
        float spread;
    };

    const std::vector<HarmonyPreset>& harmonyPresets()
    {
        static const std::vector<HarmonyPreset> presets =
        {
            { "Harmony: Duet 3rd",
              {  2,  0,  0,  0 }, { true,  false, false, false },
              { -0.35f, 0.0f, 0.0f, 0.0f }, { 1.04f, 1.0f, 1.0f, 1.0f }, 70.0f, 30.0f },

            { "Harmony: Trio",
              {  2,  4,  0,  0 }, { true,  true,  false, false },
              { -0.5f, 0.5f, 0.0f, 0.0f }, { 1.05f, 0.95f, 1.0f, 1.0f }, 68.0f, 35.0f },

            { "Harmony: Wide Choir",
              {  2,  4, -7,  7 }, { true,  true,  true,  true },
              { -0.7f, 0.7f, -0.25f, 0.3f }, { 1.08f, 0.92f, 1.15f, 0.88f }, 62.0f, 60.0f },

            { "Harmony: Octaves",
              {  7, -7,  0,  0 }, { true,  true,  false, false },
              { -0.2f, 0.2f, 0.0f, 0.0f }, { 0.9f, 1.12f, 1.0f, 1.0f }, 75.0f, 20.0f },
        };

        return presets;
    }

    void setParam (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float value)
    {
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    }
}

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& state) : apvts (state) {}

int PresetManager::getNumFactory() const
{
    return (int) (factoryPresets().size() + harmonyPresets().size());
}

juce::StringArray PresetManager::getFactoryNames() const
{
    juce::StringArray names;

    for (const auto& p : factoryPresets())
        names.add (p.name);

    for (const auto& p : harmonyPresets())
        names.add (p.name);

    return names;
}

void PresetManager::loadFactory (int index)
{
    const auto& basic = factoryPresets();
    const auto& harmony = harmonyPresets();

    if (index < 0)
        return;

    if (index < (int) basic.size())
    {
        for (const auto& s : basic[(size_t) index].settings)
            setParam (apvts, s.id, s.value);

        return;
    }

    const int hi = index - (int) basic.size();
    if (hi >= (int) harmony.size())
        return;

    const auto& h = harmony[(size_t) hi];

    // A harmony preset that left the bus switched off would appear to do
    // nothing at all, so these always switch it on.
    setParam (apvts, params::harmOn, 1.0f);
    setParam (apvts, params::harmLevel, h.level);
    setParam (apvts, params::harmSpread, h.spread);

    for (int v = 0; v < params::numHarmonyVoices; ++v)
    {
        setParam (apvts, params::harmonyID (params::harmEnable, v), h.enabled[v] ? 1.0f : 0.0f);
        setParam (apvts, params::harmonyID (params::harmDegrees, v), (float) h.degrees[v]);
        setParam (apvts, params::harmonyID (params::harmPan, v), h.pan[v] * 100.0f);
        setParam (apvts, params::harmonyID (params::harmFormant, v), h.formant[v]);
        setParam (apvts, params::harmonyID (params::harmVoiceLevel, v), 75.0f);
    }
}

juce::File PresetManager::getUserDirectory() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Helix Audio")
               .getChildFile ("HELIX Tune")
               .getChildFile ("Presets");
}

juce::StringArray PresetManager::getUserNames() const
{
    juce::StringArray names;
    const auto dir = getUserDirectory();

    if (! dir.isDirectory())
        return names;

    for (const auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.helix"))
        names.add (f.getFileNameWithoutExtension());

    names.sort (true);
    return names;
}

bool PresetManager::saveUser (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty())
        return false;

    auto dir = getUserDirectory();
    if (! dir.exists() && ! dir.createDirectory())
        return false;

    const auto state = apvts.copyState();
    if (auto xml = state.createXml())
        return xml->writeTo (dir.getChildFile (clean + ".helix"));

    return false;
}

void PresetManager::loadUser (const juce::String& name)
{
    const auto file = getUserDirectory().getChildFile (name + ".helix");
    if (! file.existsAsFile())
        return;

    if (auto xml = juce::XmlDocument::parse (file))
    {
        const auto tree = juce::ValueTree::fromXml (*xml);

        // Replacing the state wholesale would bypass the host's automation
        // bookkeeping, so walk the parameters instead.
        if (tree.isValid())
        {
            for (auto* param : apvts.processor.getParameters())
                if (auto* withID = dynamic_cast<juce::AudioProcessorParameterWithID*> (param))
                {
                    const auto child = tree.getChildWithProperty ("id", withID->paramID);
                    if (child.isValid())
                        setParam (apvts, withID->paramID, (float) child.getProperty ("value"));
                }

            // Presets saved before the master switch existed get the same
            // migration as old sessions - see params::legacyHarmonyWasOn.
            if (! tree.getChildWithProperty ("id", params::harmOn).isValid())
                setParam (apvts, params::harmOn, params::legacyHarmonyWasOn (tree) ? 1.0f : 0.0f);
        }
    }
}

bool PresetManager::deleteUser (const juce::String& name)
{
    return getUserDirectory().getChildFile (name + ".helix").deleteFile();
}

juce::StringArray PresetManager::getAllNames() const
{
    auto names = getFactoryNames();
    names.addArray (getUserNames());
    return names;
}

void PresetManager::load (int comboIndex)
{
    const int numFactory = getNumFactory();

    if (comboIndex < numFactory)
        loadFactory (comboIndex);
    else
        loadUser (getUserNames()[comboIndex - numFactory]);
}

} // namespace helix
