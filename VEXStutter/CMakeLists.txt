cmake_minimum_required(VERSION 3.22)

# macOS: Apple Silicon + Intel. Must be set before project().
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
endif()

project(VEXStutter VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows: the C++ runtime is linked statically (no Visual C++ Redistributable needed).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Single-config generators (Makefiles, Ninja) ignore "--config Release": without a build type the
# plug-in would be built without optimisation.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(VEX_BUILD_AU "Also build the Audio Unit (macOS; only after the VST3 is validated)" OFF)
option(VEX_BUILD_TESTS "Also build the test programs (tests/)" OFF)
set(VEX_JUCE_PATH "" CACHE PATH "Local JUCE folder (empty: ./JUCE if present, otherwise download)")
set(VEX_JUCE_TAG "8.0.6" CACHE STRING "JUCE release to download (git tag)")

# ------------------------------------------------------------------------------
#  JUCE: VEX_JUCE_PATH, then ./JUCE (e.g. a git submodule), otherwise the pinned release is downloaded
# ------------------------------------------------------------------------------
if(VEX_JUCE_PATH)
    add_subdirectory(${VEX_JUCE_PATH} JUCE)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG ${VEX_JUCE_TAG}
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ------------------------------------------------------------------------------
#  Plug-in
#  The manufacturer / plug-in codes and the bundle id are part of the plug-in's identity in every
#  host: never change them after a release.
# ------------------------------------------------------------------------------
set(VEX_FORMATS VST3 Standalone)
if(APPLE AND VEX_BUILD_AU)
    list(APPEND VEX_FORMATS AU)
endif()

juce_add_plugin(VEXStutter
    COMPANY_NAME "VEX"
    PLUGIN_MANUFACTURER_CODE Vexa
    PLUGIN_CODE Vxst
    FORMATS ${VEX_FORMATS}
    PRODUCT_NAME "VEX Stutter"
    BUNDLE_ID com.vex.stutter
    VST3_CATEGORIES Fx
    AU_MAIN_TYPE kAudioUnitType_Effect
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "VEX Stutter Standalone uses the audio input.")

set(VEX_PLUGIN_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/Parameters/ParameterIDs.h
    Source/Parameters/Choices.h
    Source/Parameters/Divisions.h
    Source/Parameters/ParameterLayout.cpp
    Source/Parameters/ParameterLayout.h
    Source/Parameters/StateUtils.h
    Source/DSP/CaptureBuffer.h
    Source/DSP/Fx.h
    Source/DSP/StutterEngine.h
    Source/Presets/PresetManager.cpp
    Source/Presets/PresetManager.h
    Source/UI/Theme.h
    Source/UI/Theme.cpp
    Source/UI/Icons.h
    Source/UI/Controls.h
    Source/UI/Controls.cpp
    Source/UI/WaveformView.h
    Source/UI/WaveformView.cpp)

# Embedded interface font: Inter (SIL Open Font License 1.1, Resources/fonts/OFL-Inter.txt)
juce_add_binary_data(VEXStutterAssets
    HEADER_NAME BinaryData.h
    NAMESPACE BinaryData
    SOURCES
        Resources/fonts/Inter-Regular.ttf
        Resources/fonts/Inter-Medium.ttf
        Resources/fonts/Inter-SemiBold.ttf)
set_target_properties(VEXStutterAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_sources(VEXStutter PRIVATE ${VEX_PLUGIN_SOURCES})
target_include_directories(VEXStutter PRIVATE Source)

set(VEX_DEFINITIONS
    VEX_VERSION_STRING="${PROJECT_VERSION}"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_compile_definitions(VEXStutter PUBLIC ${VEX_DEFINITIONS})
target_compile_options(VEXStutter PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

target_link_libraries(VEXStutter
    PRIVATE
        VEXStutterAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# ------------------------------------------------------------------------------
#  Tests (not part of the plug-in)
#    VEXStutter_EngineTests : the JUCE-free DSP engine
#    VEXStutter_Tests    : parameters, layouts, pass-through, state, editor; with --vst3 the built
#                          VST3 is scanned and loaded like a host loads it
#    VEXStutter_HostDemo : the built VST3 in a small host window, audio running (screenshots)
# ------------------------------------------------------------------------------
if(VEX_BUILD_TESTS)
    # the DSP engine on its own (no JUCE): timing, modes, clicks, bounds, FX, determinism
    add_executable(VEXStutter_EngineTests tests/engine_tests.cpp)
    target_include_directories(VEXStutter_EngineTests PRIVATE Source)
    target_compile_features(VEXStutter_EngineTests PRIVATE cxx_std_17)

    juce_add_console_app(VEXStutter_Tests PRODUCT_NAME "VEXStutter_Tests")
    target_sources(VEXStutter_Tests PRIVATE tests/plugin_tests.cpp ${VEX_PLUGIN_SOURCES})
    target_include_directories(VEXStutter_Tests PRIVATE Source)
    target_compile_definitions(VEXStutter_Tests PRIVATE
        ${VEX_DEFINITIONS}
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_PLUGINHOST_VST3=1
        JUCE_STANDALONE_APPLICATION=1)
    target_compile_options(VEXStutter_Tests PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
    target_link_libraries(VEXStutter_Tests
        PRIVATE
            VEXStutterAssets
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    # loads the built VST3 like a host, plays audio through it and shows its editor with live
    # host-side measurements (screenshots, manual checks)
    juce_add_console_app(VEXStutter_HostDemo PRODUCT_NAME "VEXStutter_HostDemo")
    target_sources(VEXStutter_HostDemo PRIVATE tests/host_demo.cpp)
    target_compile_definitions(VEXStutter_HostDemo PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_PLUGINHOST_VST3=1)
    target_link_libraries(VEXStutter_HostDemo
        PRIVATE
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()
