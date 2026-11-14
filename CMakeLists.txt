cmake_minimum_required(VERSION 3.22)

project(IroncurveAutoEQ VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Auf macOS ein Universal Binary (Apple Silicon + Intel)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")

option(IRONCURVE_BUILD_TESTS "Tests fuer den Auto-EQ-Kern bauen" ON)
option(IRONCURVE_BUILD_PLUGIN "Plugin bauen (benoetigt JUCE)" ON)

# --- Kern ohne JUCE (testbar) -------------------------------------------------
add_library(ironcurve_core STATIC Source/DSP/AutoEqFitter.cpp)
target_include_directories(ironcurve_core PUBLIC Source)
set_target_properties(ironcurve_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(IRONCURVE_BUILD_TESTS)
    enable_testing()
    add_executable(fitter_tests tests/test_fitter.cpp)
    target_link_libraries(fitter_tests PRIVATE ironcurve_core)
    add_test(NAME fitter_tests COMMAND fitter_tests)
endif()

# --- Plugin -------------------------------------------------------------------
if(IRONCURVE_BUILD_PLUGIN)
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.9
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)

    juce_add_plugin(IroncurveAutoEQ
        COMPANY_NAME "Ironcurve"
        PRODUCT_NAME "Ironcurve Auto EQ"
        PLUGIN_MANUFACTURER_CODE Ircv
        PLUGIN_CODE IcEq
        FORMATS VST3 Standalone
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Fx EQ)

    target_sources(IroncurveAutoEQ PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/SpectrumAnalyzer.cpp)

    target_compile_definitions(IroncurveAutoEQ PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

    if(MSVC)
        target_compile_options(IroncurveAutoEQ PRIVATE /utf-8)
        target_compile_options(ironcurve_core PRIVATE /utf-8)
    endif()

    target_link_libraries(IroncurveAutoEQ
        PRIVATE
            ironcurve_core
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    # Optionaler End-to-End-Test (Lernen mit synthetischem Signal + Screenshot)
    option(IRONCURVE_BUILD_HARNESS "End-to-End-Testprogramm bauen" OFF)
    if(IRONCURVE_BUILD_HARNESS)
        juce_add_console_app(LearnHarness PRODUCT_NAME "LearnHarness")
        target_sources(LearnHarness PRIVATE
            tests/LearnHarness.cpp
            Source/PluginProcessor.cpp
            Source/PluginEditor.cpp
            Source/SpectrumAnalyzer.cpp)
        target_compile_definitions(LearnHarness PRIVATE
            JucePlugin_Name="Ironcurve Auto EQ"
            JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
        target_link_libraries(LearnHarness PRIVATE
            ironcurve_core juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
    endif()
endif()
