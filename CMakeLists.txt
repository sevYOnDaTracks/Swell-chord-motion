cmake_minimum_required(VERSION 3.22)

# Mac: build universal binaries (Apple Silicon + Intel). Must be set before project().
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")

project(SwellChordMotion VERSION 1.4.2)

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
# Monocypher (BSD-2 / CC0) : vérification Ed25519 des fichiers de licence.
# Pas de CMakeLists chez Monocypher : SOURCE_SUBDIR inexistant = télécharger seulement, la cible est déclarée ici.
FetchContent_Declare(monocypher GIT_REPOSITORY https://github.com/LoupVaillant/Monocypher.git GIT_TAG 4.0.3 GIT_SHALLOW TRUE SOURCE_SUBDIR none)
FetchContent_MakeAvailable(monocypher)
add_library(monocypher STATIC "${monocypher_SOURCE_DIR}/src/monocypher.c" "${monocypher_SOURCE_DIR}/src/optional/monocypher-ed25519.c")
target_include_directories(monocypher PUBLIC "${monocypher_SOURCE_DIR}/src" "${monocypher_SOURCE_DIR}/src/optional")
set_target_properties(monocypher PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(SWELL_FORMATS Standalone VST3)
option(SWELL_COPY_AFTER_BUILD "Install built plug-ins in the user plug-in folders" ${APPLE})
if(APPLE)
    list(APPEND SWELL_FORMATS AU)
endif()

juce_add_plugin(SwellChordMotion
    ICON_BIG Resources/chord_motion_icon.png
    COMPANY_NAME "Swell-audio"
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
    Resources/chord_motion_icon.png)

target_sources(SwellChordMotion PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp Source/Licence/SwellLicence.cpp)

target_compile_definitions(SwellChordMotion PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(SwellChordMotion
    PRIVATE monocypher juce::juce_cryptography
        SwellChordMotionData
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

option(SWELL_BUILD_TESTS "Build chord palette and MIDI regression tests" OFF)
if(SWELL_BUILD_TESTS)
    enable_testing()
    add_executable(SwellChordParity Tests/ChordParity.cpp)
    target_link_libraries(SwellChordParity PRIVATE SwellChordMotion)
    target_include_directories(SwellChordParity PRIVATE Source
        $<TARGET_PROPERTY:SwellChordMotion,INCLUDE_DIRECTORIES>)
    target_compile_definitions(SwellChordParity PRIVATE
        $<TARGET_PROPERTY:SwellChordMotion,COMPILE_DEFINITIONS>)
    add_test(NAME ChordParity COMMAND SwellChordParity
        "${CMAKE_CURRENT_SOURCE_DIR}/Tests/ReferenceVoicings.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/Tests/OnceRanges.txt")
endif()
