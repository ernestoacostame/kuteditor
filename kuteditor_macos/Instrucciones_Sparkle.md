# Guía de Uso de Sparkle 2 en KutEditor (macOS)

Esta guía detalla el proceso para gestionar las claves de firma (Ed25519), transferirlas entre equipos y firmar nuevas versiones de la aplicación para el sistema de actualización automática de Sparkle 2.

Las herramientas de Sparkle se encuentran en el directorio:
`kuteditor_macos/3rdparty/sparkle_tools/bin/`

---

## 1. Conceptos Básicos

Sparkle 2 utiliza firmas de curva elíptica **Ed25519 (EdDSA)** para verificar la autenticidad de las actualizaciones:
*   **Clave Privada:** Se almacena de manera segura en el **Llavero de macOS (Keychain)** del desarrollador. Se usa para firmar las actualizaciones (`.dmg` o `.zip`).
*   **Clave Pública:** Se incrusta en el archivo `Info.plist` de la aplicación. Se usa para comprobar que la firma del archivo de actualización es válida.
*   **Firma del Appcast:** Cada versión publicada en el XML del appcast debe llevar su firma correspondiente generada con la clave privada.

> [!CRITICAL]
> **No pierdas tu clave privada.** Si la pierdes, no podrás volver a enviar actualizaciones automáticas a tus usuarios existentes a menos que les pidas descargar manualmente una versión con una nueva clave pública.

---

## 2. Migración: Compartir o Mover Claves entre Macs

Dado que la clave privada se guarda en el Llavero de macOS y no en un archivo dentro del proyecto, si compilas desde otro Mac debes migrar la clave utilizando los comandos de Sparkle.

### En el Mac de origen (donde se generó la clave originalmente):
1. Ejecuta el comando de exportación para guardar la clave privada temporalmente en un archivo:
   ```bash
   /Users/elav/Developer/KutStudio/kuteditor/kuteditor_macos/3rdparty/sparkle_tools/bin/generate_keys -x ~/Desktop/sparkle_private_key
   ```
2. Mueve de forma segura el archivo `sparkle_private_key` (creado en tu Escritorio) al Mac de destino (ej. vía AirDrop o pendrive). *No lo envíes por correos o servidores públicos sin cifrar.*

### En el Mac de destino (donde deseas importar la clave):
1. Coloca el archivo en tu Escritorio y ejecútalo con el comando de importación:
   ```bash
   /Users/elav/Developer/KutStudio/kuteditor/kuteditor_macos/3rdparty/sparkle_tools/bin/generate_keys -f ~/Desktop/sparkle_private_key
   ```
2. **Importante:** Elimina el archivo `sparkle_private_key` de tu Escritorio en ambos ordenadores una vez completado el proceso.

---

## 3. Firmar Actualizaciones (Solo al publicar versiones)

No necesitas ejecutar este comando para compilaciones locales de prueba. Solo hazlo cuando generes el instalador definitivo (`.dmg`) que subirás a producción para tus usuarios.

### Pasos para firmar:
1. Compila la aplicación y genera el archivo `.dmg` (por ejemplo, `KutEditor-1.5.2.dmg`).
2. Ejecuta la herramienta de firma pasándole el `.dmg` como argumento:
   ```bash
   /Users/elav/Developer/KutStudio/kuteditor/kuteditor_macos/3rdparty/sparkle_tools/bin/sign_update /ruta/a/tu/KutEditor-1.5.2.dmg
   ```
   *(La primera vez, macOS solicitará permiso para acceder a tu Llavero. Elige "Permitir siempre" para que no vuelva a preguntar).*
3. El comando devolverá un texto como este:
   ```text
   sparkle:edSignature="pNFd7KbcQSu+Mq7UYrbQXTPq82luht2ACXm/r2utp1u/Uv/5hWqctdT2jwQgMejW7DRoeV/hVr6J4VdZYdwWDw=="
   ```
4. Copia toda la cadena `sparkle:edSignature="..."` y agrégala al elemento `<enclosure>` de la versión correspondiente en tu archivo XML de actualización (`appcast.xml`).

---

## 4. Notas Técnicas Adicionales

### Errores de Advertencia al Compilar (macdeployqt)
Anteriormente, durante el proceso de empaquetado, la herramienta de Qt (`macdeployqt`) arrojaba los siguientes errores:
*   `ERROR: Cannot resolve rpath "@rpath/Sparkle.framework/Versions/B/Sparkle"`
*   `ERROR: "error: ... can't open file: /usr/local/lib/libdeepfilter.0.5.dylib"`

**Estos errores ya han sido solucionados** en el script `build_dmg.sh` copiando `Sparkle.framework` y corrigiendo el ID de `libdeepfilter.dylib` antes de invocar `macdeployqt`. Ahora el empaquetado se realiza de forma limpia y sin advertencias de dependencias no resueltas.
