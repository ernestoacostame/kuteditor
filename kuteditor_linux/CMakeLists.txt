cmake_minimum_required(VERSION 3.16)
project(kuteditor VERSION 2.51 LANGUAGES C CXX)
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# ---------------------------------------------------------------------------
# Qt6 (obligatorio, Qt >= 6.7 para QRhi)
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
if(UNIX AND NOT APPLE)
    find_package(Qt6 REQUIRED COMPONENTS DBus)
endif()

# ---------------------------------------------------------------------------
# JACK (obligatorio para grabación; si no está, se compila en modo dummy).
# En Arch con PipeWire, `pipewire-jack` implementa esta API, así que
# pkg-config encontrará `jack` igual.
# ---------------------------------------------------------------------------
find_package(PkgConfig REQUIRED)
pkg_check_modules(JACK jack)
set(HAVE_JACK ${JACK_FOUND})
if(HAVE_JACK)
    message(STATUS "JACK encontrado: grabación habilitada (versión ${JACK_VERSION})")
else()
    message(WARNING "JACK NO encontrado: la app compilará pero Record/Play no harán nada")
endif()

# libsamplerate (opcional): resample de alta calidad para export 48↔44.1 kHz.
pkg_check_modules(SAMPLERATE samplerate)
set(HAVE_SAMPLERATE ${SAMPLERATE_FOUND})
if(HAVE_SAMPLERATE)
    message(STATUS "libsamplerate encontrada: resample de alta calidad habilitado")
else()
    message(STATUS "libsamplerate NO encontrada: fallback a resample lineal (calidad menor)")
endif()

# TagLib (opcional): escritura de CHAP frames ID3v2 con artwork embebido.
pkg_check_modules(TAGLIB taglib)
set(HAVE_TAGLIB ${TAGLIB_FOUND})
if(HAVE_TAGLIB)
    message(STATUS "TagLib encontrada: capítulos con artwork embebido habilitados")
else()
    message(STATUS "TagLib NO encontrada: capítulos MP3 sin artwork embebido")
endif()


# ---------------------------------------------------------------------------
# FetchContent (para dependencias descargadas en tiempo de configuración)
# ---------------------------------------------------------------------------
include(FetchContent)
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()

# ---------------------------------------------------------------------------
# Whisper.cpp (dependencia del sistema)
# ---------------------------------------------------------------------------
# Usa la librería whisper.cpp instalada en el sistema.
#
# Instalación según distro:
#   Arch:   pacman -S whisper.cpp (o compilar desde AUR/fuente)
#   Debian: compilar desde fuente e instalar con make install
#   Fedora: compilar desde fuente
#
# Para compilar whisper.cpp con GPU (recomendado):
#   cmake -B build -DGGML_VULKAN=ON    (AMD/Intel)
#   cmake -B build -DGGML_CUDA=ON      (NVIDIA)
#   cmake --build build && sudo cmake --install build
#
# KUT detecta en runtime qué GPU hay disponible y la reporta al usuario.
# ---------------------------------------------------------------------------
set(HAVE_WHISPER "AUTO" CACHE STRING "Habilitar Whisper (ON, OFF, AUTO)")

if(HAVE_WHISPER STREQUAL "ON")
    pkg_check_modules(WHISPER REQUIRED whisper)
    add_compile_definitions(HAVE_WHISPER)
    message(STATUS "whisper.cpp encontrado (sistema): transcripción habilitada")
elseif(HAVE_WHISPER STREQUAL "AUTO")
    # Intentar auto-detectar si whisper.cpp está instalado
    pkg_check_modules(WHISPER QUIET whisper)
    if(WHISPER_FOUND)
        set(HAVE_WHISPER "ON")
        add_compile_definitions(HAVE_WHISPER)
        message(STATUS "whisper.cpp auto-detectado (sistema): transcripción habilitada")
    else()
        set(HAVE_WHISPER "OFF")
        message(STATUS "whisper.cpp no encontrado → transcripción desactivada")
        message(STATUS "  Instálalo con: pacman -S whisper.cpp  (o compila desde fuente)")
    endif()
else()
    message(STATUS "whisper.cpp deshabilitado por el usuario")
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
# RNNoise – preferimos la librería del sistema si está instalada.
# Fallback: descarga y compila desde fuente.
# ---------------------------------------------------------------------------
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(RNNOISE IMPORTED_TARGET rnnoise)
endif()

if(RNNOISE_FOUND)
    message(STATUS "RNNoise del sistema encontrada: ${RNNOISE_LINK_LIBRARIES}")
    # Crear target interfaz que enlaza contra la librería del sistema
    add_library(rnnoise INTERFACE)
    target_link_libraries(rnnoise INTERFACE PkgConfig::RNNOISE)
else()
    message(STATUS "RNNoise del sistema no encontrada – compilando desde fuente")
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
    if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_C_COMPILER_ID MATCHES "Clang")
        target_compile_options(rnnoise PRIVATE "-w")
    endif()
endif()

# ---------------------------------------------------------------------------
# DeepFilterNet (3rdparty)
# ---------------------------------------------------------------------------
option(HAVE_DEEPFILTERNET "Activar soporte para DeepFilterNet" ON)
if(HAVE_DEEPFILTERNET)
    # Buscar libdeepfilter.so en 3rdparty/deepfilternet/
    find_library(DEEPFILTER_LIB NAMES deepfilter libdeepfilter.so.0.5 PATHS ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/deepfilternet/ NO_DEFAULT_PATH)
    if(DEEPFILTER_LIB)
        add_compile_definitions(HAVE_DEEPFILTERNET)
        message(STATUS "DeepFilterNet habilitado: reducción de ruido avanzada (${DEEPFILTER_LIB})")
    else()
        set(HAVE_DEEPFILTERNET OFF)
        message(WARNING "libdeepfilter NO encontrada. Descarga la release de GitHub y colócala en 3rdparty/deepfilternet/")
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
    src/audio/JackManager.cpp
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
    src/audio/JackManager.h
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

# Flags según lo encontrado
if(HAVE_JACK)
    target_compile_definitions(kuteditor PRIVATE HAVE_JACK=1)
endif()
if(HAVE_SAMPLERATE)
    target_compile_definitions(kuteditor PRIVATE HAVE_SAMPLERATE=1)
endif()
if(HAVE_TAGLIB)
    target_compile_definitions(kuteditor PRIVATE HAVE_TAGLIB=1)
endif()

# Versión y features para el diálogo "Acerca de"
target_compile_definitions(kuteditor PRIVATE
    APP_VERSION="${CPACK_PACKAGE_VERSION}"
    APP_FEATURE_JACK=$<BOOL:${HAVE_JACK}>
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
        src/ui/fonts/MaterialSymbolsOutlined-ExtraLight.ttf
)

# Icono de la aplicación. Se añade solo si el archivo existe en disco,
# para no romper el build si todavía no lo has colocado.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/kuteditor.svg")
    qt_add_resources(kuteditor "app_icon"
        PREFIX "/"
        FILES
            resources/kuteditor.svg
    )
    message(STATUS "App icon: resources/kuteditor.svg (encontrado)")
else()
    message(STATUS "App icon: resources/kuteditor.svg NO encontrado, saltando")
endif()

target_include_directories(kuteditor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

set_target_properties(kuteditor PROPERTIES
    INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib/kuteditor"
    BUILD_WITH_INSTALL_RPATH FALSE
    SKIP_BUILD_RPATH FALSE
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

if(UNIX AND NOT APPLE)
    target_link_libraries(kuteditor PRIVATE Qt6::DBus)
endif()

if(HAVE_DEEPFILTERNET AND DEEPFILTER_LIB)
    target_link_libraries(kuteditor PRIVATE ${DEEPFILTER_LIB})
endif()



if(HAVE_JACK)
    target_include_directories(kuteditor PRIVATE ${JACK_INCLUDE_DIRS})
    target_link_libraries(kuteditor PRIVATE ${JACK_LIBRARIES})
    target_compile_options(kuteditor PRIVATE ${JACK_CFLAGS_OTHER})
endif()

if(HAVE_SAMPLERATE)
    target_include_directories(kuteditor PRIVATE ${SAMPLERATE_INCLUDE_DIRS})
    target_link_libraries(kuteditor PRIVATE ${SAMPLERATE_LIBRARIES})
    target_compile_options(kuteditor PRIVATE ${SAMPLERATE_CFLAGS_OTHER})
endif()

if(HAVE_TAGLIB)
    target_include_directories(kuteditor PRIVATE ${TAGLIB_INCLUDE_DIRS})
    target_link_libraries(kuteditor PRIVATE ${TAGLIB_LIBRARIES})
    target_compile_options(kuteditor PRIVATE ${TAGLIB_CFLAGS_OTHER})
endif()

if(HAVE_WHISPER)
    target_include_directories(kuteditor PRIVATE ${WHISPER_INCLUDE_DIRS})
    target_link_libraries(kuteditor PRIVATE ${WHISPER_LIBRARIES})
    target_compile_options(kuteditor PRIVATE ${WHISPER_CFLAGS_OTHER})
endif()

install(TARGETS kuteditor DESTINATION bin)
install(FILES resources/kuteditor.desktop DESTINATION share/applications)
install(FILES resources/kuteditor.svg DESTINATION share/icons/hicolor/scalable/apps)

if(HAVE_DEEPFILTERNET AND DEEPFILTER_LIB)
    install(FILES 
        ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/deepfilternet/libdeepfilter.so.0.5 
        DESTINATION lib/kuteditor
    )
endif()

# ---------------------------------------------------------------------------
# CPack (Debian/RPM Support)
# ---------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME "kuteditor")
# CPACK_PACKAGE_VERSION se define al inicio del archivo
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Editor de podcast con soporte JACK y Qt6")
set(CPACK_PACKAGE_VENDOR "KutEditor")
set(CPACK_PACKAGE_CONTACT "support@kuteditor.com")

set(CPACK_DEBIAN_PACKAGE_DEPENDS "pipewire-jack | jackd2, libtag2 | libtag1v5, libsamplerate0, librnnoise0")
set(CPACK_RPM_PACKAGE_REQUIRES "pipewire-jack-audio-connection-kit, taglib, libsamplerate, rnnoise")
set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")

include(CPack)
