cmake_minimum_required(VERSION 3.22)
project(TranscriberLive VERSION 0.3.0 LANGUAGES C CXX)

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
# Plugins: Receiver (um por canal) e Display (conversa) + app Display (sem áudio)
# ---------------------------------------------------------------------------
option(TRANSCRIBER_COPY_PLUGIN "Copia os plugins p/ a pasta de plugins do sistema após o build" ON)

set(TL_COMPANY      "Jhonatan Miikael")
set(TL_BUNDLE_BASE  "com.jhonatanmiikael")
set(TL_ICON_BIG     "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon_1024.png")
set(TL_ICON_SMALL   "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon_256.png")

set(TL_COMMON_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

# recursos compartilhados (página web do celular) + hub
juce_add_binary_data(TranscriberDisplayData SOURCES Resources/display.html)
set(TL_COMMON_SOURCES Source/Common/Hub.cpp)

# ---- Receiver ----
juce_add_plugin(TranscriberLive
    COMPANY_NAME                "${TL_COMPANY}"
    BUNDLE_ID                   "${TL_BUNDLE_BASE}.transcriberlive.receiver"
    PLUGIN_MANUFACTURER_CODE    Jhmk
    PLUGIN_CODE                 Trlv
    FORMATS                     VST3 AU
    PRODUCT_NAME                "Transcriber Live Receiver"
    ICON_BIG                    "${TL_ICON_BIG}"
    ICON_SMALL                  "${TL_ICON_SMALL}"
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
    Source/Common/TranscriptionEngine.cpp
    ${TL_COMMON_SOURCES})

target_include_directories(TranscriberLive PRIVATE Source)
target_compile_definitions(TranscriberLive PUBLIC ${TL_COMMON_DEFS})
target_link_libraries(TranscriberLive PRIVATE
    TranscriberDisplayData
    juce::juce_audio_utils
    juce::juce_dsp
    whisper
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ---- Display (plugin) ----
juce_add_plugin(TranscriberLiveDisplay
    COMPANY_NAME                "${TL_COMPANY}"
    BUNDLE_ID                   "${TL_BUNDLE_BASE}.transcriberlive.display"
    PLUGIN_MANUFACTURER_CODE    Jhmk
    PLUGIN_CODE                 Trld
    FORMATS                     VST3 AU
    PRODUCT_NAME                "Transcriber Live Display"
    ICON_BIG                    "${TL_ICON_BIG}"
    ICON_SMALL                  "${TL_ICON_SMALL}"
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
    Source/Display/DisplayEditor.cpp
    ${TL_COMMON_SOURCES})

target_include_directories(TranscriberLiveDisplay PRIVATE Source)
target_compile_definitions(TranscriberLiveDisplay PUBLIC ${TL_COMMON_DEFS})
target_link_libraries(TranscriberLiveDisplay PRIVATE
    TranscriberDisplayData
    juce::juce_audio_utils
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ---- Display (app Standalone, sem dispositivo de áudio) ----
juce_add_gui_app(TranscriberLiveDisplayApp
    COMPANY_NAME    "${TL_COMPANY}"
    BUNDLE_ID       "${TL_BUNDLE_BASE}.transcriberlive.displayapp"
    PRODUCT_NAME    "Transcriber Live Display"
    ICON_BIG        "${TL_ICON_BIG}"
    ICON_SMALL      "${TL_ICON_SMALL}")

target_sources(TranscriberLiveDisplayApp PRIVATE
    Source/Display/DisplayApp.cpp
    Source/Display/DisplayProcessor.cpp
    Source/Display/DisplayEditor.cpp
    ${TL_COMMON_SOURCES})

target_include_directories(TranscriberLiveDisplayApp PRIVATE Source)
target_compile_definitions(TranscriberLiveDisplayApp PRIVATE ${TL_COMMON_DEFS} TRANSCRIBER_DISPLAY_APP=1 JUCE_APPLICATION_NAME_STRING="Transcriber Live Display" JUCE_APPLICATION_VERSION_STRING="0.3.0")
target_link_libraries(TranscriberLiveDisplayApp PRIVATE
    TranscriberDisplayData
    juce::juce_audio_utils
    juce::juce_gui_extra
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

    juce_add_gui_app(ReceiverTest PRODUCT_NAME "ReceiverTest")
    target_sources(ReceiverTest PRIVATE Tests/ReceiverTest.cpp Source/Receiver/ReceiverProcessor.cpp Source/Receiver/ReceiverEditor.cpp
        Source/Common/TranscriptionEngine.cpp ${TL_COMMON_SOURCES})
    target_include_directories(ReceiverTest PRIVATE Source)
    target_compile_definitions(ReceiverTest PRIVATE ${TL_COMMON_DEFS} JucePlugin_Name="Transcriber Live Receiver")
    target_link_libraries(ReceiverTest PRIVATE TranscriberDisplayData juce::juce_gui_extra juce::juce_audio_formats juce::juce_audio_utils juce::juce_dsp juce::juce_audio_processors whisper
        PUBLIC juce::juce_recommended_config_flags)
endif()
