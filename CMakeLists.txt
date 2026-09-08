cmake_minimum_required(VERSION 3.22)
project(TranscriberLive VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# macOS: build universal (Apple Silicon + Intel) e alvo mínimo 11.0
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

include(FetchContent)

# ---------------------------------------------------------------------------
# JUCE (framework do plugin)
# ---------------------------------------------------------------------------
# Se você já tem o JUCE clonado, aponte: -DJUCE_DIR=/caminho/JUCE
if(DEFINED JUCE_DIR AND EXISTS "${JUCE_DIR}/CMakeLists.txt")
    add_subdirectory("${JUCE_DIR}" juce)
else()
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.8
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ---------------------------------------------------------------------------
# whisper.cpp (motor de transcrição offline + VAD Silero)
# ---------------------------------------------------------------------------
set(WHISPER_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER   OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS      OFF CACHE BOOL "" FORCE)
set(GGML_NATIVE            OFF CACHE BOOL "" FORCE)   # binário portátil (não otimiza só p/ a CPU de build)
if(APPLE)
    set(GGML_METAL ON  CACHE BOOL "" FORCE)           # GPU Apple Silicon
    set(GGML_METAL_EMBED_LIBRARY ON CACHE BOOL "" FORCE)
    set(GGML_ACCELERATE ON CACHE BOOL "" FORCE)
endif()

if(DEFINED WHISPER_DIR AND EXISTS "${WHISPER_DIR}/CMakeLists.txt")
    add_subdirectory("${WHISPER_DIR}" whisper)
else()
    FetchContent_Declare(whisper
        GIT_REPOSITORY https://github.com/ggml-org/whisper.cpp.git
        GIT_TAG        master
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(whisper)
endif()

# ---------------------------------------------------------------------------
# Plugins: Receiver (um por canal) e Display (conversa + servidor web)
# ---------------------------------------------------------------------------
option(TRANSCRIBER_COPY_PLUGIN "Copia os plugins p/ a pasta de plugins do sistema após o build" ON)

set(TL_COMMON_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

# ---- Receiver ----
juce_add_plugin(TranscriberLive
    COMPANY_NAME                "Jhonatan Audio"
    BUNDLE_ID                   "com.jhonatanaudio.transcriberlive"
    PLUGIN_MANUFACTURER_CODE    Jhna
    PLUGIN_CODE                 Trlv
    FORMATS                     VST3 AU Standalone
    PRODUCT_NAME                "Transcriber Live Receiver"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD     ${TRANSCRIBER_COPY_PLUGIN}
    VST3_CATEGORIES             Analyzer Tools
    AU_MAIN_TYPE                kAudioUnitType_Effect)

target_sources(TranscriberLive PRIVATE
    Source/Receiver/ReceiverProcessor.cpp
    Source/Receiver/ReceiverEditor.cpp
    Source/Common/TranscriptionEngine.cpp)

target_include_directories(TranscriberLive PRIVATE Source)
target_compile_definitions(TranscriberLive PUBLIC ${TL_COMMON_DEFS})
target_link_libraries(TranscriberLive PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    whisper
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ---- Display ----
juce_add_binary_data(TranscriberDisplayData SOURCES Resources/display.html)

juce_add_plugin(TranscriberLiveDisplay
    COMPANY_NAME                "Jhonatan Audio"
    BUNDLE_ID                   "com.jhonatanaudio.transcriberlivedisplay"
    PLUGIN_MANUFACTURER_CODE    Jhna
    PLUGIN_CODE                 Trld
    FORMATS                     VST3 AU Standalone
    PRODUCT_NAME                "Transcriber Live Display"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD     ${TRANSCRIBER_COPY_PLUGIN}
    VST3_CATEGORIES             Analyzer Tools
    AU_MAIN_TYPE                kAudioUnitType_Effect)

target_sources(TranscriberLiveDisplay PRIVATE
    Source/Display/DisplayProcessor.cpp
    Source/Display/DisplayEditor.cpp)

target_include_directories(TranscriberLiveDisplay PRIVATE Source)
target_compile_definitions(TranscriberLiveDisplay PUBLIC ${TL_COMMON_DEFS})
target_link_libraries(TranscriberLiveDisplay PRIVATE
    TranscriberDisplayData
    juce::juce_audio_utils
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Teste de console do motor (opcional): -DTRANSCRIBER_BUILD_TESTS=ON
# ---------------------------------------------------------------------------
option(TRANSCRIBER_BUILD_TESTS "Compila o EngineTest" OFF)
if(TRANSCRIBER_BUILD_TESTS)
    juce_add_console_app(EngineTest PRODUCT_NAME "EngineTest")
    target_sources(EngineTest PRIVATE Tests/EngineTest.cpp Source/Common/TranscriptionEngine.cpp)
    target_include_directories(EngineTest PRIVATE Source Source/Common)
    target_compile_definitions(EngineTest PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 TRANSCRIBER_DEBUG=1)
    target_link_libraries(EngineTest PRIVATE juce::juce_audio_formats juce::juce_gui_basics whisper
        PUBLIC juce::juce_recommended_config_flags)
endif()
