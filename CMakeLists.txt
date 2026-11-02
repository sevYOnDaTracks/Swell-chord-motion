cmake_minimum_required(VERSION 3.22)

# Mac: build universal binaries (Apple Silicon + Intel). Must be set before project().
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")

project(SwellChordMotion VERSION 1.2.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows: link the C++ runtime statically so the plugin never depends on the
# Visual C++ Redistributable installed on the user's machine
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

set(SWELL_FORMATS Standalone VST3)
set(SWELL_COPY_AFTER_BUILD FALSE)
if(APPLE)
    list(APPEND SWELL_FORMATS AU)
    # On Mac the plugin folders live in ~/Library, so copying needs no admin rights
    set(SWELL_COPY_AFTER_BUILD TRUE)
endif()

juce_add_plugin(SwellChordMotion
    COMPANY_NAME "Swell"
    BUNDLE_ID com.swell.chordmotion
    PLUGIN_MANUFACTURER_CODE Swel
    PLUGIN_CODE Chmo
    FORMATS ${SWELL_FORMATS}
    PRODUCT_NAME "Swell Chord Motion"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${SWELL_COPY_AFTER_BUILD}
    VST3_CATEGORIES Instrument Generator
    AU_MAIN_TYPE kAudioUnitType_MusicDevice)

juce_add_binary_data(SwellChordMotionData SOURCES
    Resources/swell_logo.png)

target_sources(SwellChordMotion PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(SwellChordMotion PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(SwellChordMotion
    PRIVATE
        SwellChordMotionData
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
