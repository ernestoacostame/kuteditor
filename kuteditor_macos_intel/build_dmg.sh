#!/bin/bash
# Script de compilación y empaquetado para KutEditor en macOS Intel (x86_64, Big Sur 11.0+)
set -e

# Directorios
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

# Prefijos automáticos para compilación macOS Intel (x86_64, Big Sur 11.0+):
#   QT_PREFIX: ruta a Qt 6.7 compatible con Big Sur
#   INTEL_DEPS_PREFIX: prefijo con dependencias x86_64
export INTEL_DEPS_PREFIX="${INTEL_DEPS_PREFIX:-$HOME/deps-intel}"
if [ -z "${QT_PREFIX:-}" ]; then
    if [ -d "$HOME/Qt/6.7.3/macos" ]; then
        export QT_PREFIX="$HOME/Qt/6.7.3/macos"
    elif [ -d "/usr/local/opt/qt" ]; then
        export QT_PREFIX="/usr/local/opt/qt"
    fi
else
    export QT_PREFIX
fi
export PATH="${QT_PREFIX:+${QT_PREFIX}/bin:}${INTEL_DEPS_PREFIX}/bin:${PATH}"

# Auto-detectar macdeployqt
if [ -n "${QT_PREFIX}" ] && [ -x "${QT_PREFIX}/bin/macdeployqt" ]; then
    MACDEPLOYQT="${QT_PREFIX}/bin/macdeployqt"
elif [ -x "/usr/local/opt/qt/bin/macdeployqt" ]; then
    MACDEPLOYQT="/usr/local/opt/qt/bin/macdeployqt"
    QT_PREFIX="/usr/local/opt/qt"
else
    MACDEPLOYQT="$(command -v macdeployqt || true)"
fi

if [ -z "${MACDEPLOYQT}" ]; then
    echo "ERROR: No se encontró macdeployqt. Exporta QT_PREFIX apuntando a tu instalación de Qt (ej. Qt 6.7)."
    exit 1
fi

# Obtener la versión desde CMakeLists.txt
VERSION=$(grep -o 'set(CPACK_PACKAGE_VERSION "[^"]*")' "${SCRIPT_DIR}/CMakeLists.txt" | cut -d'"' -f2)
if [ -z "${VERSION}" ]; then
    VERSION="unknown"
fi

OUT_DMG="${SCRIPT_DIR}/KutEditor-${VERSION}-Intel.dmg"

echo "=== 1. Limpiando directorios anteriores ==="
rm -rf "${BUILD_DIR}"
rm -f "${SCRIPT_DIR}/KutEditor-*-Intel.dmg"
rm -f "${OUT_DMG}"

echo "=== 2. Creando directorio de compilación ==="
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

echo "=== 3. Configurando CMake (x86_64, macOS 11.0+) ==="
CMAKE_ARGS=(
    "-DCMAKE_OSX_ARCHITECTURES=x86_64"
    "-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0"
    "-DINTEL_DEPS_PREFIX=${INTEL_DEPS_PREFIX}"
)

PREFIX_PATHS="${INTEL_DEPS_PREFIX}"
if [ -n "${QT_PREFIX}" ]; then
    PREFIX_PATHS="${QT_PREFIX};${PREFIX_PATHS}"
fi
CMAKE_ARGS+=("-DCMAKE_PREFIX_PATH=${PREFIX_PATHS}")

export PKG_CONFIG_PATH="${INTEL_DEPS_PREFIX}/lib/pkgconfig:${INTEL_DEPS_PREFIX}/share/pkgconfig:${PKG_CONFIG_PATH:-}"

cmake "${CMAKE_ARGS[@]}" ..

echo "=== 4. Compilando aplicación ==="
make -j$(sysctl -n hw.ncpu)

echo "=== 5. Desplegando dependencias de Qt (macdeployqt) ==="
mkdir -p kuteditor.app/Contents/Frameworks
if [ -d "${BUILD_DIR}/_deps/sparkle-src/Sparkle.framework" ]; then
    cp -R "${BUILD_DIR}/_deps/sparkle-src/Sparkle.framework" kuteditor.app/Contents/Frameworks/
fi

if [ -f kuteditor.app/Contents/MacOS/libdeepfilter.dylib ]; then
    install_name_tool -id "@executable_path/libdeepfilter.dylib" kuteditor.app/Contents/MacOS/libdeepfilter.dylib
fi

install_name_tool -add_rpath "@executable_path/../Frameworks" kuteditor.app/Contents/MacOS/kuteditor 2>/dev/null || true

"${MACDEPLOYQT}" kuteditor.app -qmldir="${SCRIPT_DIR}/src/ui"

# Asegurar que el Info.plist correcto con los metadatos y versiones se mantenga en el bundle
cp Info.plist kuteditor.app/Contents/Info.plist

echo "=== 5b. Copiando plugins de backend de ggml ==="
if [ -d "${INTEL_DEPS_PREFIX}/opt/ggml/libexec" ]; then
    cp "${INTEL_DEPS_PREFIX}/opt/ggml/libexec"/libggml-*.so kuteditor.app/Contents/MacOS/ 2>/dev/null || true
elif [ -d "${INTEL_DEPS_PREFIX}/libexec" ]; then
    cp "${INTEL_DEPS_PREFIX}/libexec"/libggml-*.so kuteditor.app/Contents/MacOS/ 2>/dev/null || true
fi

INTEL_DEPS_PREFIX="${INTEL_DEPS_PREFIX}" python3 "${SCRIPT_DIR}/fix_dependencies.py" kuteditor.app

echo "=== 6. Firmando la aplicación (Ad-hoc con entitlements) ==="
codesign --force --deep --options runtime --entitlements "${SCRIPT_DIR}/src/kuteditor.entitlements" --sign - kuteditor.app

echo "=== 7. Creando el instalador DMG ==="
hdiutil create -volname "KutEditor" -srcfolder kuteditor.app -ov -format UDZO "${OUT_DMG}"

echo "=== 8. Limpiando archivos temporales ==="
cd "${SCRIPT_DIR}"
rm -rf "${BUILD_DIR}"

echo "=== ¡Listo! El archivo DMG Intel se ha generado con éxito en: ${OUT_DMG} ==="
