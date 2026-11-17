cmake_minimum_required(VERSION 3.22)
project(VSTMOfflineHost VERSION 0.1.0 LANGUAGES C CXX)

if(NOT JUCE_DIR)
  message(FATAL_ERROR "Pass -DJUCE_DIR=/path/to/JUCE")
endif()
add_subdirectory("${JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")

juce_add_console_app(VSTMOfflineHost PRODUCT_NAME "VSTM Offline Host")
target_sources(VSTMOfflineHost PRIVATE Source/Main.cpp)
target_compile_features(VSTMOfflineHost PRIVATE cxx_std_17)
target_compile_definitions(VSTMOfflineHost PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_PLUGINHOST_VST3=1)
target_link_libraries(VSTMOfflineHost PRIVATE
  juce::juce_audio_utils
  juce::juce_audio_processors
  juce::juce_audio_formats
  juce::juce_core
)
