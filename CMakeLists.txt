cmake_minimum_required(VERSION 3.22)
project(TranscriberLive VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# macOS: uma passada de compilação POR ARQUITETURA (e não uma universal)
# ---------------------------------------------------------------------------
# Parece natural pedir "arm64;x86_64" de uma vez e deixar o compilador resolver.
# Não funciona, e o modo como falha é traiçoeiro: o ggml escolhe o SIMD olhando
# CMAKE_OSX_ARCHITECTURES, e o teste dele é de igualdade exata:
#
#     if     (CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")  -> ARM
#     elseif (CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64") -> x86
#     else() -> "Unknown CPU architecture. Falling back to generic..."
#               + -DGGML_CPU_GENERIC
#
# Com a lista "arm64;x86_64" nenhuma bate, e o ggml compila o caminho genérico
# escalar — sem AVX2, sem FMA, sem os kernels otimizados — nas DUAS fatias.
#
# O estrago não aparece no Apple Silicon, porque lá o Metal faz o trabalho
# pesado e esconde a CPU capada. No Intel aparece inteiro: medimos RTF 14 num
# MacBook Air 2019 com o modelo `base` — 1,1 s de fala levando 16 s.
#
# Por isso o CI compila uma vez com TL_OSX_ARCH=arm64, outra com x86_64 — aí o
# ggml reconhece cada uma e otimiza — e junta as fatias com `lipo`. O resultado
# continua sendo UM binário universal e UM instalador.
if(APPLE)
    if(NOT DEFINED TL_OSX_ARCH)
        set(TL_OSX_ARCH "arm64;x86_64")   # conveniência para build local
    endif()
    set(CMAKE_OSX_ARCHITECTURES "${TL_OSX_ARCH}" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)

    list(LENGTH CMAKE_OSX_ARCHITECTURES TL_NUM_ARCH)
    if(TL_NUM_ARCH GREATER 1)
        message(WARNING
            "Compilando ${CMAKE_OSX_ARCHITECTURES} numa passada so. O ggml vai cair "
            "no modo generico (sem AVX2/NEON) e ficar varias vezes mais lento, "
            "principalmente em Mac Intel. Para um build de distribuicao, compile "
            "uma vez com -DTL_OSX_ARCH=arm64, outra com -DTL_OSX_ARCH=x86_64 e junte "
            "as fatias com lipo (e o que o CI faz).")
    endif()
endif()

# fontes em UTF-8 (o MSVC precisa da flag explicita)
if(MSVC)
    add_compile_options(/utf-8)
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

# ---------------------------------------------------------------------------
# Nível de SIMD do ggml (x86)
# ---------------------------------------------------------------------------
# ATENÇÃO: GGML_NATIVE=OFF **não** significa "compatível com tudo". O ggml ainda
# liga AVX/AVX2/FMA/F16C por conta própria, e no MSVC isso vira /arch:AVX2 para o
# arquivo inteiro. Numa máquina sem AVX2 o resultado é uma "instrução ilegal" no
# instante em que o plugin é carregado — o host abre e fecha a janela sem
# mensagem nenhuma. O Display não usa o ggml, então continua abrindo normal: é
# exatamente esse o sintoma.
#
# TRANSCRIBER_PORTABLE_CPU=ON  -> roda em qualquer x86-64 (padrão, para vender)
# TRANSCRIBER_PORTABLE_CPU=OFF -> deixa o ggml usar AVX2/FMA (mais rápido, exige CPU 2013+)
option(TRANSCRIBER_PORTABLE_CPU "Compila o ggml sem AVX/AVX2 (roda em qualquer x86-64)" ON)

# No macOS isto NAO se aplica, de propósito (decisão consciente, não esquecimento):
# desligar AVX2 custaria de 1,5x a 2x de velocidade em TODO Mac Intel — incluindo
# os bons — só para salvar um Mac de 2013 sem AVX2. Não vale a troca. Em vez
# disso, o plugin detecta a CPU em tempo de execução e avisa, em vez de morrer
# com instrução ilegal (ver TL_REQUIRES_AVX2 abaixo e TranscriptionEngine.cpp).
# Em Apple Silicon nada disso importa: o arm64 usa NEON.
set(TL_CPU_DEFS "")
if(TRANSCRIBER_PORTABLE_CPU AND NOT APPLE)
    foreach(f GGML_AVX GGML_AVX2 GGML_AVX512 GGML_AVX_VNNI GGML_FMA GGML_F16C
              GGML_BMI2 GGML_AVX512_VBMI GGML_AVX512_VNNI GGML_AVX512_BF16)
        set(${f} OFF CACHE BOOL "" FORCE)
    endforeach()
else()
    # o ggml vai usar AVX2/FMA: o plugin precisa conferir antes de carregar o
    # modelo, senão a CPU antiga derruba o host inteiro sem mensagem
    set(TL_CPU_DEFS TL_REQUIRES_AVX2=1)
endif()

if(APPLE)
    set(GGML_METAL ON  CACHE BOOL "" FORCE)           # GPU Apple Silicon
    set(GGML_METAL_EMBED_LIBRARY ON CACHE BOOL "" FORCE)

    # ------------------------------------------------------------------------
    # Accelerate/BLAS DESLIGADO — de propósito, e com prova.
    # ------------------------------------------------------------------------
    # Isto estava ON e era a causa real dos fechamentos em Mac Intel. Três
    # relatórios do próprio macOS 12.7.4 (MacBook Air 2018), duas pilhas
    # diferentes, o mesmo culpado:
    #
    #   REAPER   SIGABRT em ggml_abort <- ggml_backend_sched_backend_id_from_cur
    #                                  <- whisper_init_state     (ao carregar o modelo)
    #   SuperRack SIGSEGV (salto para o endereço 0)
    #                                  <- ggml_backend_blas_graph_compute
    #                                  <- whisper_encode_internal (ao transcrever)
    #
    # O backend BLAS do ggml chama o Accelerate da Apple. Compilamos com um SDK
    # novo, onde esses símbolos mudaram (o "$NEWLAPACK" da Apple); no macOS 12 o
    # símbolo não existe e o stub preguiçoso fica nulo — daí o salto para 0. No
    # caminho do init, o agendador não acha backend para uma operação e chama
    # ggml_abort, que mata o processo do host inteiro.
    #
    # Não dava para descobrir isso por dedução: as duas hipóteses anteriores
    # (AVX2 e Metal) eram razoáveis e estavam erradas. Quem respondeu foi o
    # relatório de crash.
    #
    # O que perdemos: um pouco de velocidade no encoder em CPU. No Apple Silicon
    # quase nada, porque quem faz o trabalho pesado é o Metal.
    set(GGML_ACCELERATE OFF CACHE BOOL "" FORCE)
    set(GGML_BLAS       OFF CACHE BOOL "" FORCE)
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

# LTO: no MSVC o /GL + /LTCG junto com as libs estáticas do ggml já produziu
# miscompilação em plugin de áudio. O ganho não paga o risco.
if(MSVC)
    set(TL_LTO "")
else()
    set(TL_LTO juce::juce_recommended_lto_flags)
endif()

set(TL_COMPANY      "Jhonatan Miikael")
set(TL_BUNDLE_BASE  "com.jhonatanmiikael")
set(TL_ICON_BIG     "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon_1024.png")
set(TL_ICON_SMALL   "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon_256.png")

set(TL_COMMON_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    ${TL_CPU_DEFS})

# recursos compartilhados (página web do celular) + hub
juce_add_binary_data(TranscriberDisplayData SOURCES Resources/display.html Resources/logoheader.png)
set(TL_COMMON_SOURCES Source/Common/Hub.cpp Source/Common/License.cpp Source/Common/Log.cpp)

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
    juce::juce_cryptography
    juce::juce_dsp
    whisper
    PUBLIC
    juce::juce_recommended_config_flags
    ${TL_LTO}
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
    juce::juce_cryptography
    PUBLIC
    juce::juce_recommended_config_flags
    ${TL_LTO}
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
    juce::juce_cryptography
    juce::juce_gui_extra
    PUBLIC
    juce::juce_recommended_config_flags
    ${TL_LTO}
    juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Teste de console do motor (opcional): -DTRANSCRIBER_BUILD_TESTS=ON
# ---------------------------------------------------------------------------
option(TRANSCRIBER_BUILD_TESTS "Compila o EngineTest" OFF)
if(TRANSCRIBER_BUILD_TESTS)
    juce_add_console_app(EngineTest PRODUCT_NAME "EngineTest")
    target_sources(EngineTest PRIVATE Tests/EngineTest.cpp Source/Common/TranscriptionEngine.cpp Source/Common/Log.cpp)
    target_include_directories(EngineTest PRIVATE Source Source/Common)
    target_compile_definitions(EngineTest PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 TRANSCRIBER_DEBUG=1)
    target_link_libraries(EngineTest PRIVATE juce::juce_audio_formats juce::juce_gui_basics whisper
        PUBLIC juce::juce_recommended_config_flags)

    juce_add_console_app(LicenseTest PRODUCT_NAME "LicenseTest")
    target_sources(LicenseTest PRIVATE Tests/LicenseTest.cpp ${TL_COMMON_SOURCES})
    target_include_directories(LicenseTest PRIVATE Source)
    target_compile_definitions(LicenseTest PRIVATE ${TL_COMMON_DEFS})
    target_link_libraries(LicenseTest PRIVATE TranscriberDisplayData juce::juce_cryptography juce::juce_graphics juce::juce_events juce::juce_core
        PUBLIC juce::juce_recommended_config_flags)

    juce_add_gui_app(ReceiverTest PRODUCT_NAME "ReceiverTest")
    target_sources(ReceiverTest PRIVATE Tests/ReceiverTest.cpp Source/Receiver/ReceiverProcessor.cpp Source/Receiver/ReceiverEditor.cpp
        Source/Common/TranscriptionEngine.cpp ${TL_COMMON_SOURCES})
    target_include_directories(ReceiverTest PRIVATE Source)
    target_compile_definitions(ReceiverTest PRIVATE ${TL_COMMON_DEFS} JucePlugin_Name="Transcriber Live Receiver")
    target_link_libraries(ReceiverTest PRIVATE TranscriberDisplayData juce::juce_cryptography juce::juce_gui_extra juce::juce_audio_formats juce::juce_audio_utils juce::juce_dsp juce::juce_audio_processors whisper
        PUBLIC juce::juce_recommended_config_flags)
endif()
