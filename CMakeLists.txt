cmake_minimum_required(VERSION 3.22)

project(HelixTune VERSION 1.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static-link the MSVC runtime so the .vst3 loads on machines without the
# redistributable installed. Hosts dlopen() us; a missing CRT is a silent failure.
if (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(FetchContent)

# ---------------------------------------------------------------------------
# JUCE
# ---------------------------------------------------------------------------
# Pinned to a release tag: JUCE's develop branch breaks API compatibility
# often enough that an unpinned fetch will eventually stop building.
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.6
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------
# Plugin target
# ---------------------------------------------------------------------------
juce_add_plugin(HelixTune
    COMPANY_NAME                "Helix Audio"
    BUNDLE_ID                   "com.helixaudio.helixtune"
    PLUGIN_MANUFACTURER_CODE    Hlix
    PLUGIN_CODE                 Hxtn
    FORMATS                     VST3 Standalone
    PRODUCT_NAME                "HELIX Tune"
    DESCRIPTION                 "Real-time pitch correction with graphical note editing"

    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            TRUE      # MIDI target-note mode
    NEEDS_MIDI_OUTPUT           TRUE      # emits the detected pitch as MIDI
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE      # graph editor uses key commands

    # JUCE copies to C:\Program Files\Common Files\VST3, which needs admin and
    # would fail every build for a non-elevated user. Install is a separate,
    # deliberate step instead - see README.
    COPY_PLUGIN_AFTER_BUILD     FALSE

    VST3_CATEGORIES             "Fx" "Pitch Shift"
)

juce_generate_juce_header(HelixTune)

target_sources(HelixTune PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp

    Source/DSP/PitchDetector.cpp
    Source/DSP/PitchStabilizer.cpp
    Source/DSP/PsolaShifter.cpp
    Source/DSP/FormantProcessor.cpp
    Source/DSP/KeyDetector.cpp
    Source/DSP/TransientGuard.cpp
    Source/DSP/HarmonyEngine.cpp
    Source/DSP/ScaleQuantizer.cpp
    Source/DSP/RetuneEngine.cpp
    Source/DSP/VibratoGenerator.cpp
    Source/DSP/CorrectionEngine.cpp

    Source/Model/Parameters.cpp
    Source/Model/PitchTrack.cpp
    Source/Model/GraphModel.cpp
    Source/Model/PresetManager.cpp
    Source/Model/UpdateChecker.cpp

    Source/UI/FuturisticLookAndFeel.cpp
    Source/UI/Widgets/NeonKnob.cpp
    Source/UI/Widgets/ScaleKeyboard.cpp
    Source/UI/Widgets/PitchScope.cpp
    Source/UI/Widgets/AutoKeyDisplay.cpp
    Source/UI/AutoModePanel.cpp
    Source/UI/HarmonyPanel.cpp
    Source/UI/Graph/GraphEditorPanel.cpp
    Source/UI/Graph/PianoRollView.cpp
)

target_include_directories(HelixTune PRIVATE Source)

# The editor prints the version. JucePlugin_VersionString lives in the
# generated JucePluginDefines.h, which juce_audio_processors.h does not pull in,
# so pass it explicitly rather than relying on transitive include order.
target_compile_definitions(HelixTune PRIVATE
    HELIX_VERSION="${PROJECT_VERSION}")

target_compile_definitions(HelixTune PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    # The editor polls a lock-free FIFO; it must not allocate on the audio thread.
    JUCE_STRICT_REFCOUNTEDPOINTER=1
)

target_link_libraries(HelixTune PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

if (MSVC)
    target_compile_options(HelixTune PRIVATE /fp:fast)
else()
    target_compile_options(HelixTune PRIVATE -ffast-math)
endif()

# ---------------------------------------------------------------------------
# Offline DSP verification
# ---------------------------------------------------------------------------
# Console app over the same DSP sources. Confirms the chain actually corrects
# pitch, which a plugin conformance test cannot tell you.
juce_add_console_app(HelixTuneDspTest PRODUCT_NAME "HelixTuneDspTest")

target_sources(HelixTuneDspTest PRIVATE
    Tests/DspTest.cpp
    Source/DSP/PitchDetector.cpp
    Source/DSP/PitchStabilizer.cpp
    Source/DSP/PsolaShifter.cpp
    Source/DSP/FormantProcessor.cpp
    Source/DSP/KeyDetector.cpp
    Source/DSP/TransientGuard.cpp
    Source/DSP/HarmonyEngine.cpp
    Source/DSP/ScaleQuantizer.cpp
    Source/DSP/RetuneEngine.cpp
    Source/DSP/VibratoGenerator.cpp
    Source/DSP/CorrectionEngine.cpp
    Source/Model/PitchTrack.cpp
    Source/Model/GraphModel.cpp
)

target_include_directories(HelixTuneDspTest PRIVATE Source)

target_compile_definitions(HelixTuneDspTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)

target_link_libraries(HelixTuneDspTest PRIVATE
    juce::juce_dsp
    juce::juce_audio_basics
    juce::juce_data_structures
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

enable_testing()
add_test(NAME dsp COMMAND HelixTuneDspTest)

# ---------------------------------------------------------------------------
# Repository artwork
# ---------------------------------------------------------------------------
# Banner and icon are rendered from the plugin's own palette and drawing code,
# so the branding cannot drift away from the product.
juce_add_console_app(HelixTuneBanner PRODUCT_NAME "HelixTuneBanner")

target_sources(HelixTuneBanner PRIVATE
    Tools/BannerRenderer.cpp
    Source/UI/FuturisticLookAndFeel.cpp)
target_include_directories(HelixTuneBanner PRIVATE Source)

target_compile_definitions(HelixTuneBanner PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)

target_link_libraries(HelixTuneBanner PRIVATE
    juce::juce_gui_basics
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Preset loading regression test
# ---------------------------------------------------------------------------
# Drives the real processor, because the crash this guards against only appears
# when the preset table is actually iterated.
juce_add_console_app(HelixTunePresetTest PRODUCT_NAME "HelixTunePresetTest")

target_sources(HelixTunePresetTest PRIVATE
    Tests/PresetTest.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/DSP/PitchDetector.cpp
    Source/DSP/PitchStabilizer.cpp
    Source/DSP/PsolaShifter.cpp
    Source/DSP/FormantProcessor.cpp
    Source/DSP/KeyDetector.cpp
    Source/DSP/TransientGuard.cpp
    Source/DSP/HarmonyEngine.cpp
    Source/DSP/ScaleQuantizer.cpp
    Source/DSP/RetuneEngine.cpp
    Source/DSP/VibratoGenerator.cpp
    Source/DSP/CorrectionEngine.cpp
    Source/Model/Parameters.cpp
    Source/Model/PitchTrack.cpp
    Source/Model/GraphModel.cpp
    Source/Model/PresetManager.cpp
    Source/Model/UpdateChecker.cpp
    Source/UI/FuturisticLookAndFeel.cpp
    Source/UI/Widgets/NeonKnob.cpp
    Source/UI/Widgets/ScaleKeyboard.cpp
    Source/UI/Widgets/PitchScope.cpp
    Source/UI/Widgets/AutoKeyDisplay.cpp
    Source/UI/AutoModePanel.cpp
    Source/UI/HarmonyPanel.cpp
    Source/UI/Graph/GraphEditorPanel.cpp
    Source/UI/Graph/PianoRollView.cpp)

target_include_directories(HelixTunePresetTest PRIVATE Source)

target_compile_definitions(HelixTunePresetTest PRIVATE
    HELIX_VERSION="${PROJECT_VERSION}"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)

target_link_libraries(HelixTunePresetTest PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

add_test(NAME presets COMMAND HelixTunePresetTest)
