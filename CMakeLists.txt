cmake_minimum_required(VERSION 3.22)

project(LiveRig VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build the C++ runtime into the program, so Airloom.exe runs on any Windows PC
# without installing Microsoft's Visual C++ Redistributable first.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Location of the JUCE framework (a git clone of https://github.com/juce-framework/JUCE).
set(JUCE_DIR "C:/JUCE" CACHE PATH "Path to the JUCE framework")
add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)

juce_add_gui_app(LiveRig
    PRODUCT_NAME "Airloom"
    COMPANY_NAME "Airloom"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/AppIcon.png"
    VERSION ${PROJECT_VERSION})

# The icon again, for the logo in the app's header.
juce_add_binary_data(AirloomData SOURCES Resources/AppIcon.png)

juce_generate_juce_header(LiveRig)

target_sources(LiveRig PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/LoopPlayer.cpp
    Source/LoopChannel.cpp
    Source/LiveSet.cpp
    Source/Effects.cpp
    Source/InputChannel.cpp
    Source/Looper.cpp
    Source/InstrumentChannel.cpp
    Source/MidiMap.cpp)

target_compile_definitions(LiveRig PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:LiveRig,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:LiveRig,JUCE_VERSION>")

target_link_libraries(LiveRig
    PRIVATE
        AirloomData
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_audio_devices
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
