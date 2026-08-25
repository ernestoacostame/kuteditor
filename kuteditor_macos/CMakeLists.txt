cmake_minimum_required(VERSION 3.16)
project(kuteditor VERSION 1.5.12 LANGUAGES C CXX)
set(CPACK_PACKAGE_VERSION "1.5.12")

set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "Minimum macOS version" FORCE)

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
# CoreAudio (macOS)
# ---------------------------------------------------------------------------
if(APPLE)
    message(STATUS "Compilando en macOS: CoreAudio habilitado")
else()
    message(FATAL_ERROR "Este proyecto ha sido refactorizado para usarse únicamente en macOS con CoreAudio")
endif()

find_package(PkgConfig REQUIRED)

# libsamplerate (opcional): resample de alta calidad para export 48↔44.1 kHz.
pkg_check_modules(SAMPLERATE IMPORTED_TARGET samplerate)
set(HAVE_SAMPLERATE ${SAMPLERATE_FOUND})
if(HAVE_SAMPLERATE)
    message(STATUS "libsamplerate encontrada: resample de alta calidad habilitado")
else()
    message(STATUS "libsamplerate NO encontrada: fallback a resample lineal (calidad menor)")
endif()

# TagLib (opcional): escritura de CHAP frames ID3v2 con artwork embebido.
pkg_check_modules(TAGLIB IMPORTED_TARGET taglib)
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
    pkg_check_modules(WHISPER REQUIRED IMPORTED_TARGET whisper)
    add_compile_definitions(HAVE_WHISPER)
    message(STATUS "whisper.cpp encontrado (sistema): transcripción habilitada")
elseif(HAVE_WHISPER STREQUAL "AUTO")
    # Intentar auto-detectar si whisper.cpp está instalado
    pkg_check_modules(WHISPER QUIET IMPORTED_TARGET whisper)
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
if(APPLE)
    set(HAVE_DEEPFILTERNET_DEFAULT ON)
else()
    set(HAVE_DEEPFILTERNET_DEFAULT ON)
endif()
option(HAVE_DEEPFILTERNET "Activar soporte para DeepFilterNet" ${HAVE_DEEPFILTERNET_DEFAULT})
if(HAVE_DEEPFILTERNET)
    add_compile_definitions(HAVE_DEEPFILTERNET)
    message(STATUS "DeepFilterNet habilitado: reducción de ruido avanzada")
    # Buscar libdeepfilter.so en 3rdparty/deepfilternet/
    find_library(DEEPFILTER_LIB NAMES deepfilter PATHS ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/deepfilternet/ NO_DEFAULT_PATH)
    if(DEEPFILTER_LIB)
        message(STATUS "Librería libdeepfilter encontrada: ${DEEPFILTER_LIB}")
    else()
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
    src/audio/CoreAudioManager.cpp
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
    src/audio/CoreAudioManager.h
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

if(APPLE)
    set(MACOSX_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/kuteditor.icns")
    set_source_files_properties(${MACOSX_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")

    list(APPEND SOURCES src/SparkleUpdater.mm)
    list(APPEND HEADERS src/SparkleUpdater.h)

    set(MACOSX_BUNDLE_EXECUTABLE_NAME "kuteditor")
    set(MACOSX_BUNDLE_ICON_FILE "kuteditor.icns")
    set(MACOSX_BUNDLE_GUI_IDENTIFIER "com.kutstudio.kuteditor")
    set(MACOSX_BUNDLE_BUNDLE_NAME "KutEditor")
    set(MACOSX_BUNDLE_SHORT_VERSION_STRING "${CPACK_PACKAGE_VERSION}")
    set(MACOSX_BUNDLE_BUNDLE_VERSION "${CPACK_PACKAGE_VERSION}")
    set(MACOSX_BUNDLE_MINIMUM_SYSTEM_VERSION "${CMAKE_OSX_DEPLOYMENT_TARGET}")

    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Info.plist"
        "${CMAKE_CURRENT_BINARY_DIR}/Info.plist"
    )

    add_executable(kuteditor MACOSX_BUNDLE ${SOURCES} ${HEADERS} ${MACOSX_ICON})
    set_target_properties(kuteditor PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info.plist"
        MACOSX_BUNDLE_ICON_FILE "kuteditor.icns"
        XCODE_ATTRIBUTE_OTHER_CODE_SIGN_FLAGS "--deep"
    )
else()
    add_executable(kuteditor ${SOURCES} ${HEADERS})
endif()

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
target_compile_definitions(kuteditor PRIVATE HAVE_COREAUDIO=1)
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

if(HAVE_DEEPFILTERNET AND DEEPFILTER_LIB)
    target_link_libraries(kuteditor PRIVATE ${DEEPFILTER_LIB})
endif()



if(APPLE)
    # --- Integration of Sparkle 2 ---
    include(FetchContent)
    FetchContent_Declare(
        Sparkle
        URL "https://github.com/sparkle-project/Sparkle/releases/download/2.6.4/Sparkle-2.6.4.tar.xz"
    )
    FetchContent_GetProperties(sparkle)
    if(NOT sparkle_POPULATED)
        FetchContent_Populate(sparkle)
    endif()

    set(SPARKLE_FRAMEWORK_PATH "${sparkle_SOURCE_DIR}/Sparkle.framework")
    message(STATUS "Sparkle.framework path: ${SPARKLE_FRAMEWORK_PATH}")

    # Set compiler/linker options for Sparkle framework path
    target_compile_options(kuteditor PRIVATE "-F${sparkle_SOURCE_DIR}")
    target_link_options(kuteditor PRIVATE "-F${sparkle_SOURCE_DIR}")

    find_library(COREAUDIO_LIBRARY CoreAudio REQUIRED)
    find_library(AUDIOTOOLBOX_LIBRARY AudioToolbox REQUIRED)
    find_library(AUDIOUNIT_LIBRARY AudioUnit REQUIRED)
    target_include_directories(kuteditor PRIVATE /opt/homebrew/include)
    target_link_libraries(kuteditor PRIVATE
        ${COREAUDIO_LIBRARY}
        ${AUDIOTOOLBOX_LIBRARY}
        ${AUDIOUNIT_LIBRARY}
        "${SPARKLE_FRAMEWORK_PATH}"
    )

    # Copiar ffmpeg embebido dentro de la app si existe en 3rdparty/ffmpeg/ffmpeg
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/ffmpeg/ffmpeg")
        message(STATUS "ffmpeg embebido encontrado: se incluirá en el paquete macOS")
        add_custom_command(TARGET kuteditor POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
                "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/ffmpeg/ffmpeg"
                "$<TARGET_FILE_DIR:kuteditor>/ffmpeg"
            COMMAND ${CMAKE_COMMAND} -E copy
                "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/ffmpeg/ffmpeg"
                "${CMAKE_CURRENT_BINARY_DIR}/ffmpeg"
            COMMENT "Copiando ffmpeg embebido al App Bundle y al directorio de compilacion"
        )
    endif()

    # Copiar plugins de backend dinámicos de ggml (CPU/Metal/BLAS) al App Bundle junto al ejecutable
    if(EXISTS "/opt/homebrew/opt/ggml/libexec")
        message(STATUS "Plugins de backend de ggml encontrados en Homebrew, copiando al App Bundle...")
        add_custom_command(TARGET kuteditor POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "/opt/homebrew/opt/ggml/libexec"
                "$<TARGET_FILE_DIR:kuteditor>"
            COMMENT "Copiando plugins de backend de ggml al App Bundle junto al ejecutable"
        )
    endif()

    # Copiar y vincular libdeepfilter dentro de la app
    if(HAVE_DEEPFILTERNET AND DEEPFILTER_LIB)
        message(STATUS "DeepFilterNet habilitado: copiando libdeepfilter al App Bundle...")
        add_custom_command(TARGET kuteditor POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
                "${DEEPFILTER_LIB}"
                "$<TARGET_FILE_DIR:kuteditor>/libdeepfilter.dylib"
            COMMAND install_name_tool -change
                "/usr/local/lib/libdeepfilter.0.5.dylib"
                "@executable_path/libdeepfilter.dylib"
                "$<TARGET_FILE:kuteditor>"
            COMMENT "Copiando y vinculando libdeepfilter al App Bundle"
        )
    endif()
endif()

if(HAVE_SAMPLERATE)
    target_link_libraries(kuteditor PRIVATE PkgConfig::SAMPLERATE)
endif()

if(HAVE_TAGLIB)
    target_link_libraries(kuteditor PRIVATE PkgConfig::TAGLIB)
endif()

if(HAVE_WHISPER)
    target_link_libraries(kuteditor PRIVATE PkgConfig::WHISPER)
endif()

# CPack is not needed as DMG packaging is handled via build_dmg.sh

