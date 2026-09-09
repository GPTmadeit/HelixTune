#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace helix
{

/** Factory and user presets over the parameter tree.

    Factory presets are stored as sparse parameter lists rather than full
    states: a preset says what it changes and stays silent about the rest, so
    adding a parameter later does not require editing every preset, and loading
    an old preset does not quietly reset controls it never knew about.
*/
class PresetManager
{
public:
    explicit PresetManager (juce::AudioProcessorValueTreeState& state);

    juce::StringArray getFactoryNames() const;
    void loadFactory (int index);

    juce::StringArray getUserNames() const;
    void loadUser (const juce::String& name);
    bool saveUser (const juce::String& name);
    bool deleteUser (const juce::String& name);

    /** Combined list as shown in the browser: factory first, then user. */
    juce::StringArray getAllNames() const;
    void load (int comboIndex);
    int  getNumFactory() const;

    juce::File getUserDirectory() const;

private:
    juce::AudioProcessorValueTreeState& apvts;
};

} // namespace helix
