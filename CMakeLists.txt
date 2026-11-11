cmake_minimum_required(VERSION 3.22)

# Version is injected by CI from the git tag (-DAXSYNTH_VERSION=1.2.3).
set(AXSYNTH_VERSION "0.0.0" CACHE STRING "AxSynth version (from git tag)")

project(AxSynth VERSION ${AXSYNTH_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static CRT (/MT) so the installer needs no VC redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(AXSYNTH_BUILD_TESTS "Build test executables" ON)

add_subdirectory(external/JUCE EXCLUDE_FROM_ALL)

juce_add_plugin(AxSynth
    COMPANY_NAME "AxSynth"
    PRODUCT_NAME "AxSynth"
    VERSION ${PROJECT_VERSION}
    PLUGIN_MANUFACTURER_CODE Axyn
    PLUGIN_CODE AxSy
    FORMATS VST3
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Synth)

file(GLOB_RECURSE AXSYNTH_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h)

set(AXSYNTH_MODULES
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_basics
    juce::juce_cryptography)

set(AXSYNTH_DEFINES
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

set(AXSYNTH_SYSLIBS winhttp crypt32)

target_sources(AxSynth PRIVATE ${AXSYNTH_SOURCES})
target_include_directories(AxSynth PRIVATE Source)
target_compile_definitions(AxSynth PUBLIC ${AXSYNTH_DEFINES})
target_link_libraries(AxSynth
    PRIVATE ${AXSYNTH_MODULES} ${AXSYNTH_SYSLIBS}
    PUBLIC juce::juce_recommended_config_flags
           juce::juce_recommended_lto_flags
           juce::juce_recommended_warning_flags)

# Spec: no compiler warnings at /W4. Enforced, so a new warning fails the build.
option(AXSYNTH_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
if(AXSYNTH_WARNINGS_AS_ERRORS AND MSVC)
    target_compile_options(AxSynth PRIVATE /WX)
endif()

if(AXSYNTH_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
