# Synchrophonic — solfeggio drone instrument
#
# Framework-free C++17 DSP core (web-engine port) + JUCE 8 wrapper
# (VST3/AU/Standalone). No full JUCE compile is possible on the Linux dev
# box; the DSP core and a headless processor test compile and run here, the
# first true plugin compile happens on CI (see .github/workflows/build.yml).
#
# The Standalone target is the playable "test host" build: the same
# processor as a standalone app with identical defaults, no DAW required.

cmake_minimum_required(VERSION 3.22)
project(Synchrophonic VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.6
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(Synchrophonic
    COMPANY_NAME "Sabatino Audio"
    PLUGIN_NAME "Synchrophonic"
    DESCRIPTION "Solfeggio drone instrument: nine chakra voices, shruti-box drone, Deep Blue pads, tala rhythms."
    PLUGIN_MANUFACTURER_CODE SabA
    PLUGIN_CODE Syn9
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "Synchrophonic"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE
)

target_sources(Synchrophonic PRIVATE
    Sg9Processor.cpp
    Sg9Editor.cpp
    ../dsp/Sg9Dsp.cpp
)

# The chakra illustration for the photo-as-interface GUI (source photo
# supplied by Nathan; copied into the repo so the build is self-contained).
# NAMESPACE Sg9Assets: the editor refers to Sg9Assets::chakra_figure_jpg.
# (Without NAMESPACE, JUCE defaults the namespace to plain "BinaryData".)
juce_add_binary_data(Sg9Assets
    NAMESPACE Sg9Assets
    SOURCES assets/chakra-figure.jpg
)

target_include_directories(Synchrophonic PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_compile_definitions(Synchrophonic PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Synchrophonic PRIVATE
    Sg9Assets
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
)
