cmake_minimum_required(VERSION 3.22)

# macOS: Apple Silicon + Intel (universal). project()'ten ONCE ayarlanmali.
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
endif()

project(SCC VERSION 1.0.0 LANGUAGES C CXX)

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

target_sources(SCC PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/UIComponents.h
    Source/SCCDsp.h)

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
#  Istege bagli: DSP testi (JUCE gerektirmez, plugin'e dahil degildir)
#    cmake -B build -DSCC_BUILD_TESTS=ON
#    cmake --build build --config Release --target SCC_DspTest
# ------------------------------------------------------------------------------
option(SCC_BUILD_TESTS "DSP test programini de olustur" OFF)
if(SCC_BUILD_TESTS)
    add_executable(SCC_DspTest tests/dsp_test.cpp)
    target_compile_definitions(SCC_DspTest PRIVATE SCC_BUILD_DSP_TEST)
    target_compile_features(SCC_DspTest PRIVATE cxx_std_17)
endif()
