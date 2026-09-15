cmake_minimum_required(VERSION 3.22)
project(HarmonizerPlugin VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Universal binary on macOS so one download covers Apple Silicon and Intel.
if(APPLE AND NOT DEFINED CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "" FORCE)
endif()

# Point at an existing checkout to avoid re-cloning on every configure.
set(HARMONIZER_JUCE_PATH "" CACHE PATH "Existing JUCE checkout")

# Set by CI from the workflow run number so the updater can tell builds apart.
set(HARMONIZER_VERSION_CODE "0" CACHE STRING "Monotonic build number")
set(HARMONIZER_VERSION_NAME "dev" CACHE STRING "Human-readable version")

if(HARMONIZER_JUCE_PATH)
    add_subdirectory(${HARMONIZER_JUCE_PATH} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 8.0.15
            GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(HarmonizerPlugin
        PRODUCT_NAME "Harmonizer"
        COMPANY_NAME "Dylan Cleverdon"
        BUNDLE_ID com.dylan.harmonizer.plugin
        PLUGIN_MANUFACTURER_CODE Dylc
        PLUGIN_CODE Hrmz
        FORMATS AU VST3 Standalone

        # Not a synth: it processes incoming audio. But it does need MIDI, and
        # that combination is what makes the AU a "music effect" (aumf) rather
        # than a plain effect (aufx). Logic will only let you send notes to the
        # former -- get this wrong and there is no way to play the harmonies.
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        AU_MAIN_TYPE kAudioUnitType_MusicEffect
        VST3_CATEGORIES Fx Pitch\ Shift

        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE)

# The engine is shared verbatim with the Android app -- same files, one
# implementation. A fix to the DSP fixes both targets.
set(DSP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../app/src/main/cpp/dsp)

target_sources(HarmonizerPlugin PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/PluginUpdater.cpp
        ${DSP_DIR}/Fft.cpp
        ${DSP_DIR}/Resampler.cpp
        ${DSP_DIR}/Analysis.cpp
        ${DSP_DIR}/Voices.cpp
        ${DSP_DIR}/Harmonizer.cpp)

target_include_directories(HarmonizerPlugin PRIVATE ${DSP_DIR} Source)

target_compile_definitions(HarmonizerPlugin PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=1
        JUCE_VST3_CAN_REPLACE_VST2=0
        # Permitted while this project stays open source under the GPL; see the
        # licensing note in plugin/README.md.
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
        HARMONIZER_VERSION_CODE=${HARMONIZER_VERSION_CODE}
        HARMONIZER_VERSION_NAME="${HARMONIZER_VERSION_NAME}")

# JUCE only consults JUCE_USE_CURL on Linux -- macOS and Windows use native
# networking -- but when it does, libcurl has to be linked explicitly.
if(UNIX AND NOT APPLE)
    find_package(CURL REQUIRED)
    target_link_libraries(HarmonizerPlugin PRIVATE CURL::libcurl)
endif()

target_link_libraries(HarmonizerPlugin PRIVATE
        juce::juce_audio_utils
        juce::juce_cryptography
        juce::juce_dsp
        PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Headless checks on the plugin wrapper. The DSP has its own suite; this covers
# the glue that only exists here -- parameter mapping, host MIDI, channel
# summing, reported latency.
# ---------------------------------------------------------------------------
option(HARMONIZER_BUILD_TESTS "Build the plugin wrapper test harness" ON)

if(HARMONIZER_BUILD_TESTS)
    juce_add_console_app(HarmonizerPluginTest PRODUCT_NAME "HarmonizerPluginTest")

    target_sources(HarmonizerPluginTest PRIVATE
            Tests/PluginHarness.cpp
            Source/PluginProcessor.cpp
            Source/PluginEditor.cpp
            Source/PluginUpdater.cpp
            ${DSP_DIR}/Fft.cpp
            ${DSP_DIR}/Resampler.cpp
            ${DSP_DIR}/Analysis.cpp
            ${DSP_DIR}/Voices.cpp
            ${DSP_DIR}/Harmonizer.cpp)

    target_include_directories(HarmonizerPluginTest PRIVATE ${DSP_DIR} Source)

    target_compile_definitions(HarmonizerPluginTest PUBLIC
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=1
            JUCE_DISPLAY_SPLASH_SCREEN=0
            JUCE_REPORT_APP_USAGE=0
            JUCE_STANDALONE_APPLICATION=1
            HARMONIZER_VERSION_CODE=${HARMONIZER_VERSION_CODE}
            HARMONIZER_VERSION_NAME="${HARMONIZER_VERSION_NAME}")

    if(UNIX AND NOT APPLE)
        target_link_libraries(HarmonizerPluginTest PRIVATE CURL::libcurl)
    endif()

    target_link_libraries(HarmonizerPluginTest PRIVATE
            juce::juce_audio_utils
            juce::juce_cryptography
            juce::juce_dsp
            PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()
