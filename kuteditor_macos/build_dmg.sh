#!/bin/bash
# Script de compilación y empaquetado para KutEditor en macOS
set -e

# Directorios
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

# Obtener la versión desde CMakeLists.txt
VERSION=$(grep -o 'set(CPACK_PACKAGE_VERSION "[^"]*")' "${SCRIPT_DIR}/CMakeLists.txt" | cut -d'"' -f2)
if [ -z "${VERSION}" ]; then
    VERSION="unknown"
fi

OUT_DMG="${SCRIPT_DIR}/KutEditor-${VERSION}.dmg"

echo "=== 1. Limpiando directorios anteriores ==="
rm -rf "${BUILD_DIR}"
rm -f "${SCRIPT_DIR}/KutEditor-*.dmg"
rm -f "${OUT_DMG}"

echo "=== 2. Creando directorio de compilación ==="
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

echo "=== 3. Configurando CMake ==="
cmake ..

echo "=== 4. Compilando aplicación ==="
make clean
make -j$(sysctl -n hw.ncpu)

echo "=== 5. Desplegando dependencias de Qt (macdeployqt) ==="
# 1. Copiar Sparkle.framework al bundle antes de macdeployqt para evitar error de resolución de rpath
mkdir -p kuteditor.app/Contents/Frameworks
if [ -d "${BUILD_DIR}/_deps/sparkle-src/Sparkle.framework" ]; then
    cp -R "${BUILD_DIR}/_deps/sparkle-src/Sparkle.framework" kuteditor.app/Contents/Frameworks/
fi

# 2. Corregir el ID de libdeepfilter.dylib dentro del bundle para evitar que macdeployqt intente buscarlo en /usr/local/lib
if [ -f kuteditor.app/Contents/MacOS/libdeepfilter.dylib ]; then
    install_name_tool -id "@executable_path/libdeepfilter.dylib" kuteditor.app/Contents/MacOS/libdeepfilter.dylib
fi

# macdeployqt busca y copia los frameworks de Qt necesarios dentro de la app
macdeployqt kuteditor.app -qmldir="${SCRIPT_DIR}/src/ui"

# Asegurar que el Info.plist correcto con los metadatos y versiones se mantenga en el bundle
cp Info.plist kuteditor.app/Contents/Info.plist

# Copiar plugins de backend dinámicos de ggml (CPU/Metal/BLAS) para que funcionen dentro del bundle (junto al ejecutable)
echo "=== 5b. Copiando plugins de backend de ggml ==="
if [ -d "/opt/homebrew/opt/ggml/libexec" ]; then
    cp /opt/homebrew/opt/ggml/libexec/libggml-*.so kuteditor.app/Contents/MacOS/
fi

python3 "${SCRIPT_DIR}/fix_dependencies.py" kuteditor.app

echo "=== 6. Firmando la aplicación (Ad-hoc) ==="
codesign --force --deep --sign - kuteditor.app

echo "=== 7. Creando el instalador DMG ==="
hdiutil create -volname "KutEditor" -srcfolder kuteditor.app -ov -format UDZO "${OUT_DMG}"

echo "=== 8. Limpiando archivos temporales ==="
cd "${SCRIPT_DIR}"
rm -rf "${BUILD_DIR}"

echo "=== ¡Listo! El archivo DMG se ha generado con éxito en: ${OUT_DMG} ==="
