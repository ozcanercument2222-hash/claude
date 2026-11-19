cmake_minimum_required(VERSION 3.22)

# macOS: Apple Silicon + Intel (universal). project()'ten ONCE ayarlanmali.
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
endif()

project(VSTMTune VERSION 1.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows: C++ calisma zamani plugin'in icine gomulur (Visual C++ Redistributable gerekmez).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ------------------------------------------------------------------------------
#  JUCE: yoksa ilk derlemede otomatik indirilir (internet gerekir)
#        Elinde varsa:  cmake -B build -DVSTM_JUCE_PATH=/yol/JUCE
# ------------------------------------------------------------------------------
set(VSTM_JUCE_PATH "" CACHE PATH "Yerel JUCE klasoru (bos = otomatik indir)")
set(VSTM_JUCE_TAG "8.0.6" CACHE STRING "Indirilecek JUCE surumu (git tag)")

if(VSTM_JUCE_PATH)
    add_subdirectory(${VSTM_JUCE_PATH} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG ${VSTM_JUCE_TAG}
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ------------------------------------------------------------------------------
#  Gomulu yazi tipleri (Inter, SIL OFL 1.1 -> Resources/fonts/OFL-Inter.txt)
# ------------------------------------------------------------------------------
juce_add_binary_data(VSTMTuneAssets
    NAMESPACE VSTMBinary
    HEADER_NAME VSTMBinary.h
    SOURCES
        Resources/fonts/Inter-Regular.ttf
        Resources/fonts/Inter-Medium.ttf
        Resources/fonts/Inter-SemiBold.ttf)

if(NOT MSVC)
    set_target_properties(VSTMTuneAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

# ------------------------------------------------------------------------------
#  Plugin
# ------------------------------------------------------------------------------
set(VSTM_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND VSTM_FORMATS AU)
endif()

if(WIN32)
    option(VSTM_COPY_AFTER_BUILD "Derlemeden sonra eklentiyi plugin klasorune kopyala" OFF)
else()
    option(VSTM_COPY_AFTER_BUILD "Derlemeden sonra eklentiyi plugin klasorune kopyala" ON)
endif()

juce_add_plugin(VSTMTune
    COMPANY_NAME "VSTM Audio"
    PLUGIN_MANUFACTURER_CODE Vstm
    PLUGIN_CODE Vtn1
    FORMATS ${VSTM_FORMATS}
    PRODUCT_NAME "VSTM Tune"
    BUNDLE_ID com.vstmaudio.vstmtune
    VST3_CATEGORIES Fx Pitch Shift
    AU_MAIN_TYPE kAudioUnitType_Effect
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD ${VSTM_COPY_AFTER_BUILD}
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "VSTM Tune Standalone ses girisi icin mikrofona erisim ister.")

set(VSTM_PLUGIN_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/Parameters.h
    Source/Lang.cpp
    Source/Lang.h
    Source/PresetManager.cpp
    Source/PresetManager.h
    Source/ui/Theme.h
    Source/ui/Icons.h
    Source/ui/Controls.h
    Source/ui/RobotKnob.h
    Source/ui/PitchDisplay.h
    Source/ui/ScaleEditor.h)

set(VSTM_DSP_HEADERS
    Source/dsp/DspCommon.h
    Source/dsp/Scales.h
    Source/dsp/PitchDetector.h
    Source/dsp/KeyDetector.h
    Source/dsp/PitchCorrector.h
    Source/dsp/TuneEngine.h)

target_sources(VSTMTune PRIVATE ${VSTM_PLUGIN_SOURCES} ${VSTM_DSP_HEADERS})
target_include_directories(VSTMTune PRIVATE Source)

set(VSTM_DEFINITIONS
    VSTM_TUNE_VERSION="${PROJECT_VERSION}"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_compile_definitions(VSTMTune PUBLIC ${VSTM_DEFINITIONS})
target_compile_options(VSTMTune PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

target_link_libraries(VSTMTune
    PRIVATE
        VSTMTuneAssets
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# ------------------------------------------------------------------------------
#  Istege bagli testler (plugin'e dahil degildir)
#    cmake -B build -DVSTM_BUILD_TESTS=ON
#
#    VSTMTune_EngineTest : ses motoru testi (JUCE gerektirmez)
#    VSTMTune_PluginTest : parametreler, state, presetler, arayuz ekran goruntuleri
#    VSTMTune_WavProbe   : gercek WAV render + analiz kaydi (tests/vocal_metrics.py ile)
#    VSTMTune_AuditProbe : denetim olcumleri S1-S15 (JUCE gerektirmez)
# ------------------------------------------------------------------------------
option(VSTM_BUILD_TESTS "Test programlarini da olustur" OFF)
if(VSTM_BUILD_TESTS)
    add_executable(VSTMTune_EngineTest tests/engine_test.cpp)

    # gercek kayitlar icin cevrimdisi render + analiz kaydi (tests/vocal_metrics.py ile)
    add_executable(VSTMTune_WavProbe tests/wav_probe.cpp)
    target_include_directories(VSTMTune_WavProbe PRIVATE Source)
    target_compile_features(VSTMTune_WavProbe PRIVATE cxx_std_17)
    target_include_directories(VSTMTune_EngineTest PRIVATE Source)
    target_compile_features(VSTMTune_EngineTest PRIVATE cxx_std_17)
    target_compile_options(VSTMTune_EngineTest PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
    target_link_options(VSTMTune_EngineTest PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/STACK:16777216>")

    # denetim olcumleri S1-S15 (VSTM_TUNE_AUDIT.md): sentetik senaryolar -> JSONL + WAV, karar vermez
    add_executable(VSTMTune_AuditProbe tests/audit_probe.cpp)
    target_include_directories(VSTMTune_AuditProbe PRIVATE Source)
    target_compile_features(VSTMTune_AuditProbe PRIVATE cxx_std_17)
    target_compile_options(VSTMTune_AuditProbe PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

    juce_add_console_app(VSTMTune_PluginTest PRODUCT_NAME "VSTMTune_PluginTest")
    target_sources(VSTMTune_PluginTest PRIVATE tests/plugin_test.cpp ${VSTM_PLUGIN_SOURCES})
    target_include_directories(VSTMTune_PluginTest PRIVATE Source)
    target_compile_definitions(VSTMTune_PluginTest PRIVATE
        ${VSTM_DEFINITIONS}
        VSTM_PLUGIN_TEST=1
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_PLUGINHOST_VST3=1
        JUCE_STANDALONE_APPLICATION=1)
    target_compile_options(VSTMTune_PluginTest PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
    target_link_libraries(VSTMTune_PluginTest
        PRIVATE
            VSTMTuneAssets
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
    target_link_options(VSTMTune_PluginTest PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/STACK:16777216>")
endif()
