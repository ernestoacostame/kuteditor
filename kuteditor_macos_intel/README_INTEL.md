# KutEditor para macOS Intel (`x86_64`, Big Sur 11.0+)

Esta carpeta está preparada para compilar y empaquetar KutEditor dirigido a procesadores **Intel (`x86_64`)** con despliegue mínimo **macOS 11.0 (Big Sur)**.

## Qué cambia respecto a `kuteditor_macos`
* `CMakeLists.txt` fuerza `CMAKE_OSX_ARCHITECTURES=x86_64` y `CMAKE_OSX_DEPLOYMENT_TARGET=11.0`.
* Elimina las rutas fijas de Apple Silicon (`/opt/homebrew`) y usa el prefijo configurable `INTEL_DEPS_PREFIX` (por defecto `/usr/local`).
* `build_dmg.sh` genera `KutEditor-<versión>-Intel.dmg`.
* `src/Info.plist` apunta `SUFeedURL` a `https://kutstudio.org/download/appcast_intel.xml` para no mezclar actualizaciones entre Intel y Apple Silicon.
* Si no existe un binario `x86_64` en `3rdparty/deepfilternet/libdeepfilter.dylib`, DeepFilterNet se desactiva automáticamente en CMake sin romper la compilación.

## Requisitos previos (si compilas desde un Mac Apple Silicon)
No uses las librerías de `/opt/homebrew` (son solo `arm64` y compiladas para macOS 14+/26+). Necesitas dependencias `x86_64` compiladas con `MACOSX_DEPLOYMENT_TARGET=11.0`:

1. **Qt 6.7** (última rama compatible con Big Sur; los binarios oficiales de Qt incluyen `x86_64` + `arm64`).
2. **Dependencias `x86_64`** instaladas en `/usr/local` (Homebrew Intel bajo Rosetta) o en una carpeta aparte (ej. `~/deps-intel`):
   * `libsamplerate`
   * `taglib`
   * `rnnoise` *(opcional: si no se encuentra, CMake lo compila desde fuente automáticamente en `x86_64`)*
   * `whisper.cpp` / `ggml` *(opcional: si no se encuentra, la transcripción se desactiva)*
3. **Binarios opcionales en `3rdparty/`** (deben ser `x86_64` o Universal):
   * `3rdparty/ffmpeg/ffmpeg` (binario estático Intel)
   * `3rdparty/deepfilternet/libdeepfilter.dylib` (compilado para `x86_64` con `minos 11.0`)

## Cómo compilar el DMG
```bash
cd kuteditor_macos_intel
export QT_PREFIX="$HOME/Qt/6.7.3/macos"          # Ruta a tu Qt 6.7
export INTEL_DEPS_PREFIX="$HOME/deps-intel"      # O /usr/local si usas Homebrew Intel
./build_dmg.sh
```
