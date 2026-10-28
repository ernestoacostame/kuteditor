cmake_minimum_required(VERSION 3.16)
project(kuteditor VERSION 1.4.3 LANGUAGES C CXX)
set(CPACK_PACKAGE_VERSION "1.4.3")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# ---------------------------------------------------------------------------
# Qt6 (obligatorio)
# ---------------------------------------------------------------------------
set(QT_NO_PRIVATE_MODULE_WARNING ON)
find_package(Qt6 6.7 REQUIRED COMPONENTS
    Core
    Quick
    QuickControls2
    Gui
    GuiPrivate
    Widgets
    Concurrent
    ShaderTools
    Network
)

# ---------------------------------------------------------------------------
# WASAPI (Windows)
# ---------------------------------------------------------------------------
if(WIN32)
    message(STATUS "Compilando en Windows: WASAPI habilitado")
else()
    message(FATAL_ERROR "Este proyecto ha sido refactorizado para usarse únicamente en Windows con WASAPI")
endif()

# ---------------------------------------------------------------------------
# FetchContent (para dependencias descargadas en tiempo de configuración)
# ---------------------------------------------------------------------------
include(FetchContent)
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()

# libsamplerate (FetchContent en Windows)
option(KUT_FETCH_SAMPLERATE "Descargar y compilar libsamplerate automáticamente" ON)
if(KUT_FETCH_SAMPLERATE)
    message(STATUS "Descargando libsamplerate desde GitHub...")
    FetchContent_Declare(
        samplerate
        GIT_REPOSITORY https://github.com/libsndfile/libsamplerate.git
        GIT_TAG 0.2.2
    )
    # Desactivar tests y ejemplos de libsamplerate
    set(LIBSAMPLERATE_EXAMPLES OFF CACHE INTERNAL "")
    set(LIBSAMPLERATE_INSTALL OFF CACHE INTERNAL "")
    FetchContent_MakeAvailable(samplerate)
    set(HAVE_SAMPLERATE ON)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SAMPLERATE IMPORTED_TARGET samplerate)
    set(HAVE_SAMPLERATE ${SAMPLERATE_FOUND})
endif()

# TagLib (FetchContent en Windows)
option(KUT_FETCH_TAGLIB "Descargar y compilar TagLib automáticamente" ON)
if(KUT_FETCH_TAGLIB)
    message(STATUS "Descargando TagLib desde GitHub...")
    FetchContent_Declare(
        taglib
        GIT_REPOSITORY https://github.com/taglib/taglib.git
        GIT_TAG v1.13.1
    )
    # Desactivar tests y utilidades de TagLib
    set(BUILD_TESTS OFF CACHE INTERNAL "")
    set(BUILD_SHARED OFF CACHE INTERNAL "")
    FetchContent_MakeAvailable(taglib)
    set(HAVE_TAGLIB ON)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(TAGLIB IMPORTED_TARGET taglib)
    set(HAVE_TAGLIB ${TAGLIB_FOUND})
endif()

# ---------------------------------------------------------------------------
# Whisper.cpp (opcional)
# ---------------------------------------------------------------------------
set(HAVE_WHISPER "AUTO" CACHE STRING "Habilitar Whisper (ON, OFF, AUTO)")
if(HAVE_WHISPER STREQUAL "ON")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(WHISPER REQUIRED IMPORTED_TARGET whisper)
    add_compile_definitions(HAVE_WHISPER)
    message(STATUS "whisper.cpp encontrado: transcripción habilitada")
elseif(HAVE_WHISPER STREQUAL "AUTO")
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(WHISPER QUIET IMPORTED_TARGET whisper)
    endif()
    if(WHISPER_FOUND)
        set(HAVE_WHISPER "ON")
        add_compile_definitions(HAVE_WHISPER)
        message(STATUS "whisper.cpp auto-detectado: transcripción habilitada")
    else()
        set(HAVE_WHISPER "OFF")
        message(STATUS "whisper.cpp no encontrado → transcripción desactivada")
    endif()
endif()

# ---------------------------------------------------------------------------
# KutPod Online Library (opcional)
# ---------------------------------------------------------------------------
option(HAVE_KUTPOD "Activar soporte para biblioteca online KutPod" ON)
if(HAVE_KUTPOD)
    add_compile_definitions(HAVE_KUTPOD)
    message(STATUS "KutPod habilitado: cliente de biblioteca online incluido")
else()
    message(STATUS "KutPod deshabilitado por el usuario")
endif()

# ---------------------------------------------------------------------------
# RNNoise (compilado desde fuente)
# ---------------------------------------------------------------------------
message(STATUS "RNNoise – descargando y compilando desde fuente")
FetchContent_Declare(
    rnnoise
    GIT_REPOSITORY https://github.com/xiph/rnnoise.git
    GIT_TAG master
)
FetchContent_GetProperties(rnnoise)
if(NOT rnnoise_POPULATED)
    FetchContent_Populate(rnnoise)
endif()

add_library(rnnoise STATIC
    ${rnnoise_SOURCE_DIR}/src/denoise.c
    ${rnnoise_SOURCE_DIR}/src/rnn.c
    ${rnnoise_SOURCE_DIR}/src/pitch.c
    ${rnnoise_SOURCE_DIR}/src/kiss_fft.c
    ${rnnoise_SOURCE_DIR}/src/celt_lpc.c
    ${rnnoise_SOURCE_DIR}/src/rnn_data.c
)
if(EXISTS "${rnnoise_SOURCE_DIR}/src/rnn_reader.c")
    target_sources(rnnoise PRIVATE ${rnnoise_SOURCE_DIR}/src/rnn_reader.c)
endif()
target_include_directories(rnnoise PUBLIC ${rnnoise_SOURCE_DIR}/include)
target_include_directories(rnnoise PRIVATE ${rnnoise_SOURCE_DIR}/src)
if(MSVC)
    target_compile_options(rnnoise PRIVATE "/wd4244" "/wd4305")
else()
    target_compile_options(rnnoise PRIVATE "-w")
endif()

# ---------------------------------------------------------------------------
# DeepFilterNet (3rdparty)
# ---------------------------------------------------------------------------
option(HAVE_DEEPFILTERNET "Activar soporte para DeepFilterNet" ON)
if(HAVE_DEEPFILTERNET)
    add_compile_definitions(HAVE_DEEPFILTERNET)
    message(STATUS "DeepFilterNet habilitado: reducción de ruido avanzada")
    # Buscar deepfilter.lib en 3rdparty/deepfilternet/
    find_library(DEEPFILTER_LIB NAMES deepfilter PATHS ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/deepfilternet/ NO_DEFAULT_PATH)
    if(DEEPFILTER_LIB)
        message(STATUS "Librería deepfilter encontrada: ${DEEPFILTER_LIB}")
    else()
        message(WARNING "deepfilter.lib NO encontrada. Colócala en 3rdparty/deepfilternet/ para habilitar el enlazado")
    endif()
else()
    message(STATUS "DeepFilterNet deshabilitado por el usuario")
endif()

# ---------------------------------------------------------------------------
# Fuentes
# ---------------------------------------------------------------------------
set(SOURCES
    src/main.cpp
    src/audio/AudioEngine.cpp
    src/audio/WasapiAudioManager.cpp
    src/audio/AudioMipmap.cpp
    src/audio/GpuMipmapCompute.cpp
    src/audio/MmapAudioBuffer.cpp
    src/ui/TrackModel.cpp
    src/ui/WaveformItem.cpp
    src/audio/fx/RNNoiseEffect.cpp
    src/ui/UndoManager.cpp
    src/io/ExportManager.cpp
    src/io/ProjectIO.cpp
    src/transcription/TranscriptionManager.cpp
    src/transcription/WhisperWorker.cpp
    src/transcription/RNNoiseWorker.cpp
)

if(HAVE_KUTPOD)
    list(APPEND SOURCES src/net/KutPodClient.cpp)
endif()

set(HEADERS
    src/audio/AudioEngine.h
    src/audio/WasapiAudioManager.h
    src/audio/AudioMipmap.h
    src/audio/GpuMipmapCompute.h
    src/audio/MmapAudioBuffer.h
    src/audio/fx/MasterEffect.h
    src/audio/fx/CompressorEffect.h
    src/audio/fx/LimiterEffect.h
    src/audio/fx/HighPassFilter.h
    src/audio/fx/LowPassFilter.h
    src/audio/fx/NotchFilter.h
    src/audio/fx/NoiseGateEffect.h
    src/audio/fx/DeNoiserEffect.h
    src/audio/fx/DeepFilterEffect.h
    src/audio/fx/DeEsserEffect.h
    src/audio/fx/ExpanderEffect.h
    src/audio/fx/AutoDuckEffect.h
    src/audio/fx/AutoGainEffect.h
    src/audio/fx/EqualizerEffect.h
    src/audio/fx/StereoWidener.h
    src/audio/fx/PhaseInvertEffect.h
    src/audio/fx/MonoMixerEffect.h
    src/audio/fx/TrimGainEffect.h
    src/audio/TrackFxChain.h
    src/ui/TrackModel.h
    src/ui/ChapterModel.h
    src/ui/UndoManager.h
    src/ui/WaveformItem.h
    src/io/ExportManager.h
    src/io/ProjectIO.h
    src/io/FileHelper.h
    src/io/ChapterTagger.h
    src/transcription/TranscriptionManager.h
    src/transcription/WhisperWorker.h
)

if(HAVE_KUTPOD)
    list(APPEND HEADERS src/net/KutPodClient.h)
endif()

add_executable(kuteditor ${SOURCES} ${HEADERS})

# ── Compile GPU mipmap compute shader to .qsb ──
qt_add_shaders(kuteditor "mipmap_compute_shader"
    NOGLSL
    NOHLSL
    NOMSL
    PREFIX "/shaders"
    FILES
        src/shaders/mipmap_build.comp
)

# Flags de compilación
target_compile_definitions(kuteditor PRIVATE HAVE_WASAPI=1)
if(HAVE_SAMPLERATE)
    target_compile_definitions(kuteditor PRIVATE HAVE_SAMPLERATE=1)
endif()
if(HAVE_TAGLIB)
    target_compile_definitions(kuteditor PRIVATE HAVE_TAGLIB=1)
endif()

# Versión y features para el diálogo "Acerca de"
target_compile_definitions(kuteditor PRIVATE
    APP_VERSION="${CPACK_PACKAGE_VERSION}"
    APP_FEATURE_AUDIO=1
    APP_FEATURE_SAMPLERATE=$<BOOL:${HAVE_SAMPLERATE}>
    APP_FEATURE_TAGLIB=$<BOOL:${HAVE_TAGLIB}>
    APP_FEATURE_WHISPER=$<IF:$<STREQUAL:${HAVE_WHISPER},ON>,1,0>
    APP_FEATURE_KUTPOD=$<BOOL:${HAVE_KUTPOD}>
    APP_FEATURE_DEEPFILTERNET=$<BOOL:${HAVE_DEEPFILTERNET}>
)

# ---------------------------------------------------------------------------
# Recursos QML
# ---------------------------------------------------------------------------
qt_add_resources(kuteditor "qml_files"
    PREFIX "/"
    FILES
        src/ui/main.qml
        src/ui/Icons.qml
        src/ui/ToolTip.qml
        src/ui/components/TrackComponent.qml
        src/ui/components/VUMeter.qml
        src/ui/components/TimeDisplay.qml
        src/ui/components/PeakMeter.qml
        src/ui/components/ExportDialog.qml
        src/ui/components/Ruler.qml
        src/ui/components/Library.qml
        src/ui/components/Knob.qml
        src/ui/components/MetalKnob.qml
        src/ui/components/BigMetalKnob.qml
        src/ui/components/EqualizerPanel.qml
        src/ui/components/FxKnobPanel.qml
        src/ui/components/NoiseGatePanel.qml
        src/ui/components/AutoDuckPanel.qml
        src/ui/components/TruncateSilencePanel.qml
        src/ui/components/TrackFxDialog.qml
        src/ui/components/TranscriptionPanel.qml
        src/ui/components/TranscriptionSettings.qml
        src/ui/components/ReductionDial.qml
        src/ui/components/SpectralDisplay.qml
        src/ui/components/ChapterMarkers.qml
        src/ui/components/ChapterEditor.qml
        src/ui/components/OnlineLibrary.qml
        src/ui/components/PublishWindow.qml
        src/ui/components/ToolTip.qml
        src/ui/fonts/MaterialSymbolsOutlined-ExtraLight.ttf
)

# Icono de la aplicación
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/kuteditor.svg")
    qt_add_resources(kuteditor "app_icon"
        PREFIX "/"
        FILES
            resources/kuteditor.svg
    )
endif()

target_include_directories(kuteditor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(kuteditor
    PRIVATE
    Qt6::Core
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Gui
    Qt6::GuiPrivate
    Qt6::Widgets
    Qt6::Concurrent
    Qt6::ShaderTools
    Qt6::Network
    rnnoise
)

# Enlace de APIs del sistema Windows (COM, multimedia)
target_link_libraries(kuteditor PRIVATE ole32 oleaut32 uuid avrt)

if(HAVE_DEEPFILTERNET AND DEEPFILTER_LIB)
    target_link_libraries(kuteditor PRIVATE ${DEEPFILTER_LIB})
endif()

if(HAVE_SAMPLERATE)
    if(KUT_FETCH_SAMPLERATE)
        target_link_libraries(kuteditor PRIVATE samplerate)
    else()
        target_link_libraries(kuteditor PRIVATE PkgConfig::SAMPLERATE)
    endif()
endif()

if(HAVE_TAGLIB)
    if(KUT_FETCH_TAGLIB)
        target_link_libraries(kuteditor PRIVATE tag)
    else()
        target_link_libraries(kuteditor PRIVATE PkgConfig::TAGLIB)
    endif()
endif()

if(HAVE_WHISPER)
    target_link_libraries(kuteditor PRIVATE PkgConfig::WHISPER)
endif()

# ---------------------------------------------------------------------------
# Instalación y Empaquetado en Windows
# ---------------------------------------------------------------------------
install(TARGETS kuteditor DESTINATION bin)

if(HAVE_DEEPFILTERNET AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/deepfilternet/deepfilter.dll")
    install(FILES 
        ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/deepfilternet/deepfilter.dll
        DESTINATION bin
    )
endif()

# Copiar ffmpeg embebido dentro del directorio de instalación si existe en 3rdparty/ffmpeg
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/ffmpeg/ffmpeg.exe")
    install(FILES 
        ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/ffmpeg/ffmpeg.exe
        DESTINATION bin
    )
endif()

# ---------------------------------------------------------------------------
# CPack Setup (Generador NSIS para Windows)
# ---------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME "KutEditor")
# CPACK_PACKAGE_VERSION se define al inicio del archivo
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Editor de podcast multipista con soporte WASAPI y Qt6")
set(CPACK_PACKAGE_VENDOR "KutStudio")
set(CPACK_PACKAGE_CONTACT "support@kuteditor.com")

if(WIN32)
    set(CPACK_GENERATOR "NSIS")
    set(CPACK_NSIS_PACKAGE_NAME "KutEditor")
    set(CPACK_NSIS_DISPLAY_NAME "KutEditor - Podcast Creator")
    set(CPACK_NSIS_HELP_LINK "https://www.kuteditor.com")
    set(CPACK_NSIS_URL_INFO_ABOUT "https://www.kuteditor.com")
    set(CPACK_NSIS_MODIFY_PATH ON)
endif()

include(CPack)
