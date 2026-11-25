cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary: Apple Silicon + Intel")

project(TB303Gen VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.7
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# on CI there is no plug-in folder to copy into
if(DEFINED ENV{CI})
    set(TB303_COPY FALSE)
else()
    set(TB303_COPY TRUE)
endif()

if(APPLE)
    set(TB303_FORMATS VST3 AU Standalone)
else()
    set(TB303_FORMATS VST3 Standalone)      # Audio Units only exist on macOS
endif()

# Put a square PNG (1024x1024 is best) at Assets/icon.png and the standalone app gets it as its icon.
set(TB303_ICON_ARGS "")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Assets/icon.png")
    set(TB303_ICON_ARGS ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Assets/icon.png" ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Assets/icon.png")
endif()

juce_add_plugin(TB303Gen
    ${TB303_ICON_ARGS}
    COMPANY_NAME "TB303Gen"
    PLUGIN_MANUFACTURER_CODE Tbgn
    PLUGIN_CODE Tb30
    FORMATS ${TB303_FORMATS}
    PRODUCT_NAME "BASSLINER303"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    COPY_PLUGIN_AFTER_BUILD ${TB303_COPY})

target_sources(TB303Gen PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Widgets.cpp)

target_compile_definitions(TB303Gen PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(TB303Gen
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
