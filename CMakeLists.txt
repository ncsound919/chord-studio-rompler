cmake_minimum_required(VERSION 3.22)

project(Rompler VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)

include(CTest)   # enable_testing() unless -DBUILD_TESTING=OFF
option(ROMPLER_BUILD_TESTS "Build the headless DSP tests" ON)

# ---------------------------------------------------------------------------
# JUCE: use $JUCE_DIR (or the JUCE_DIR env var) if set, otherwise fetch it.
# ---------------------------------------------------------------------------
if(NOT DEFINED JUCE_DIR AND DEFINED ENV{JUCE_DIR})
    set(JUCE_DIR "$ENV{JUCE_DIR}" CACHE PATH "Path to the JUCE source tree")
endif()

if(JUCE_DIR)
    if(NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "JUCE not found at '${JUCE_DIR}'. Pass -DJUCE_DIR=<path>.")
    endif()
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.14
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
    set(JUCE_DIR "${juce_SOURCE_DIR}")
endif()

add_subdirectory("${JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# Web UI assets embedded via BinaryData.
# ---------------------------------------------------------------------------
file(GLOB_RECURSE ROMPLER_WEBUI_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/webui/*")

juce_add_binary_data(RomplerWebUI
    NAMESPACE RomplerWeb
    HEADER_NAME RomplerWeb.h
    SOURCES ${ROMPLER_WEBUI_FILES})

# ---------------------------------------------------------------------------
# Plugin: VST3 + Standalone.
# ---------------------------------------------------------------------------
juce_add_plugin(Rompler
    COMPANY_NAME "Uplift"
    VERSION "${PROJECT_VERSION}"
    PRODUCT_NAME "Chord Studio Rompler"
    DESCRIPTION "Rompler / granular sampler instrument"

    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    NEEDS_WEBVIEW2 TRUE

    PLUGIN_MANUFACTURER_CODE Uplf
    PLUGIN_CODE Romp

    FORMATS VST3 Standalone
    COPY_PLUGIN_AFTER_BUILD FALSE)

juce_generate_juce_header(Rompler)

target_sources(Rompler PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/SampleSource.cpp
    Source/SamplePlayerVoice.cpp
    Source/GrainCloud.cpp
    Source/StretchStage.cpp)

target_include_directories(Rompler PRIVATE
    Source
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/signalsmith-stretch")

target_compile_definitions(Rompler PRIVATE
    JUCE_WEB_BROWSER=1
    JUCE_USE_WIN_WEBVIEW2=1
    JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Rompler,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Rompler,JUCE_VERSION>")

target_link_libraries(Rompler PRIVATE
    RomplerWebUI
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Headless DSP tests.
# ---------------------------------------------------------------------------
if(ROMPLER_BUILD_TESTS)
    juce_add_console_app(RomplerTests PRODUCT_NAME "RomplerTests")
    juce_generate_juce_header(RomplerTests)

    target_sources(RomplerTests PRIVATE
        tests/RomplerTests.cpp
        Source/SampleSource.cpp
        Source/SamplePlayerVoice.cpp
        Source/GrainCloud.cpp
        Source/StretchStage.cpp)

    target_include_directories(RomplerTests PRIVATE
        Source
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/signalsmith-stretch")

    target_compile_definitions(RomplerTests PRIVATE
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_STANDALONE_APPLICATION=1)

    target_link_libraries(RomplerTests PRIVATE
        juce::juce_audio_basics
        juce::juce_dsp
        PUBLIC
        juce::juce_recommended_config_flags)

    if(BUILD_TESTING)
        add_test(NAME rompler_tests COMMAND $<TARGET_FILE:RomplerTests>)
    endif()
endif()
