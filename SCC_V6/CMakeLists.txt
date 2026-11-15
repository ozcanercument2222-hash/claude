cmake_minimum_required(VERSION 3.22)

# macOS: Apple Silicon + Intel (universal). project()'ten ONCE ayarlanmali.
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
endif()

project(SCC VERSION 6.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ------------------------------------------------------------------------------
#  JUCE: bilgisayarinda yoksa ilk derlemede otomatik indirilir (internet gerekir)
#        Elinde varsa:  cmake -B build -DSCC_JUCE_PATH=/yol/JUCE
# ------------------------------------------------------------------------------
set(SCC_JUCE_PATH "" CACHE PATH "Yerel JUCE klasoru (bos = otomatik indir)")
set(SCC_JUCE_TAG "8.0.6" CACHE STRING "Indirilecek JUCE surumu (git tag)")

if(SCC_JUCE_PATH)
    add_subdirectory(${SCC_JUCE_PATH} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG ${SCC_JUCE_TAG}
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ------------------------------------------------------------------------------
#  Plugin
# ------------------------------------------------------------------------------
set(SCC_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND SCC_FORMATS AU)
endif()

# Windows'ta sistem klasorune kopyalamak yonetici izni ister, o yuzden varsayilan kapali.
if(WIN32)
    option(SCC_COPY_AFTER_BUILD "Derlemeden sonra eklentiyi plugin klasorune kopyala" OFF)
else()
    option(SCC_COPY_AFTER_BUILD "Derlemeden sonra eklentiyi plugin klasorune kopyala" ON)
endif()

juce_add_plugin(SCC
    COMPANY_NAME "SCC Audio"
    PLUGIN_MANUFACTURER_CODE Sccx
    PLUGIN_CODE Scc1
    FORMATS ${SCC_FORMATS}
    PRODUCT_NAME "SCC"
    BUNDLE_ID com.sccaudio.scc
    VST3_CATEGORIES Fx Dynamics
    AU_MAIN_TYPE kAudioUnitType_Effect
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD ${SCC_COPY_AFTER_BUILD}
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "SCC Standalone ses girisi icin mikrofona erisim ister.")

# Arayuz + plugin katmani
set(SCC_PLUGIN_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/CharacterDesigner.cpp
    Source/CharacterDesigner.h
    Source/CharacterPresetManager.cpp
    Source/CharacterPresetManager.h
    Source/UIComponents.h
    Source/SCCKnobStyle.h
    Source/SCCKnobFont.h
    Source/AnalyzerViews.h)

# DSP cekirdegi (JUCE'dan bagimsiz, yalnizca baslik dosyalari)
set(SCC_DSP_HEADERS
    Source/SCCDsp.h
    Source/SCCManualCompressor.h
    Source/SCCEngine.h
    Source/SCCCharacterTypes.h
    Source/SCCCharacterEngine.h
    Source/SCCCharacterAnalysis.h
    Source/SCCHarmonicGenerator.h
    Source/SCCSaturationProcessor.h
    Source/SCCTransientProcessor.h
    Source/SCCCharacterToneProcessor.h
    Source/SCCShaper.h
    Source/SCCFilters.h
    Source/SCCOversampler.h
    Source/SCCSpectrumAnalyzer.h)

target_sources(SCC PRIVATE ${SCC_PLUGIN_SOURCES} ${SCC_DSP_HEADERS})

target_compile_definitions(SCC PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

# Kaynak dosyalardaki Turkce karakterler icin (MSVC)
target_compile_options(SCC PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

target_link_libraries(SCC
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# ------------------------------------------------------------------------------
#  Istege bagli: testler (plugin'e dahil degildir)
#    cmake -B build -DSCC_BUILD_TESTS=ON
#    cmake --build build --config Release --target SCC_DspTest SCC_V6Test SCC_PluginTest
#
#    SCC_DspTest    : V5 kompresor testi (JUCE gerektirmez; V5 davranisinin korundugunu dogrular)
#    SCC_V6Test     : V6 DSP testi (JUCE gerektirmez)
#    SCC_PluginTest : plugin katmani testi (parametreler, state, presetler, arayuz goruntuleri;
#                     istege bagli olarak derlenmis SCC.vst3'u bir host gibi yukleyip sinar)
# ------------------------------------------------------------------------------
option(SCC_BUILD_TESTS "Test programlarini da olustur" OFF)
if(SCC_BUILD_TESTS)
    add_executable(SCC_DspTest tests/dsp_test.cpp)
    target_compile_definitions(SCC_DspTest PRIVATE SCC_BUILD_DSP_TEST)
    target_compile_features(SCC_DspTest PRIVATE cxx_std_17)

    add_executable(SCC_V6Test tests/v6_test.cpp)
    target_compile_definitions(SCC_V6Test PRIVATE SCC_BUILD_DSP_TEST)
    target_compile_features(SCC_V6Test PRIVATE cxx_std_17)

    juce_add_console_app(SCC_PluginTest PRODUCT_NAME "SCC_PluginTest")
    target_sources(SCC_PluginTest PRIVATE tests/plugin_test.cpp ${SCC_PLUGIN_SOURCES})
    target_include_directories(SCC_PluginTest PRIVATE Source)
    target_compile_definitions(SCC_PluginTest PRIVATE
        SCC_BUILD_PLUGIN_TEST=1
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_PLUGINHOST_VST3=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1)
    target_compile_options(SCC_PluginTest PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
    target_link_libraries(SCC_PluginTest
        PRIVATE
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()
