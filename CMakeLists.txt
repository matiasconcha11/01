cmake_minimum_required(VERSION 3.22)

# Must be set before project(). Builds are native-only by default; release builds pass
# -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" for a universal binary.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(ZeroOne VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.4
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(ZeroOne
    COMPANY_NAME "Rhizr"
    COMPANY_WEBSITE "https://rhizr.dev"
    BUNDLE_ID dev.rhizr.zeroone
    # Same manufacturer code as the other Rhizr plugins.
    PLUGIN_MANUFACTURER_CODE Mcon
    PLUGIN_CODE Zr01
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "01"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE)

set(ZEROONE_SOURCES
    Source/ChoiceSelector.cpp
    Source/LookAndFeel.cpp
    Source/Message.cpp
    Source/MessageSynth.cpp
    Source/PluginEditor.cpp
    Source/PluginProcessor.cpp
    Source/Schedule.cpp
    Source/Spectrogram.cpp)

target_sources(ZeroOne PRIVATE ${ZEROONE_SOURCES})
target_compile_definitions(ZeroOne PRIVATE ZEROONE_VERSION="${PROJECT_VERSION}")

target_compile_definitions(ZeroOne PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(ZeroOne
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

option(ZEROONE_BUILD_TESTS "Build the unit tests" ON)

if(ZEROONE_BUILD_TESTS)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.16.1
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(Catch2)

    # The tests compile the same sources as the plugin, without a plugin wrapper around them.
    add_executable(ZeroOneTests
        ${ZEROONE_SOURCES}
        Tests/ZeroOneTests.cpp)

    target_link_libraries(ZeroOneTests PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
        Catch2::Catch2WithMain)

    target_include_directories(ZeroOneTests PRIVATE Source)
    target_compile_definitions(ZeroOneTests PRIVATE
        ZEROONE_VERSION="${PROJECT_VERSION}"
        JUCE_STANDALONE_APPLICATION=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)

    include(CTest)
    include(${catch2_SOURCE_DIR}/extras/Catch.cmake)
    catch_discover_tests(ZeroOneTests)
endif()
