# KutEditor

KutEditor es el entorno nativo de escritorio para creadores de podcasts de la suite **KutStudio**. Diseñado desde cero para agilizar el flujo de trabajo, integra edición de audio, gestión de metadatos y publicación directa, todo bajo una misma interfaz fluida e intuitiva.

## Características Principales

* **Creación Desacoplada**: Genera, edita y diseña episodios localmente. Añade marcas de tiempo, capítulos (Podcasting 2.0), arte específico para el episodio y guiones.
* **Integración sin Fricciones**: Se conecta de manera transparente con tu servidor **KutPod** mediante API, permitiendo subir y publicar episodios con un solo clic.
* **Gestión Multimedia**: Herramientas para manipular archivos de audio y compilar metadatos de forma eficiente sin depender de otras aplicaciones pesadas.
* **Flujo Multicuenta**: Gestiona diferentes proyectos de podcast sin tener que iniciar o cerrar sesión repetidamente.
* **Interfaz Profesional**: Interfaz construida en **Qt**, asegurando alto rendimiento y consistencia visual en entornos de escritorio.

## Despliegue e Instalación

KutEditor provee herramientas y scripts para diferentes distribuciones Linux:

* **Arch Linux / Manjaro / CachyOS**: Compilación e instalación nativa con `PKGBUILD` (`makepkg -si`) o mediante `installer.sh`.
* **Debian / Ubuntu / Derivadas**: Generación de paquete fuente y binario `.deb` nativo mediante `compiler.sh` (`./compiler.sh`) o instalación rápida con `installer.sh`.
* **Fedora / RPM**: Empaquetado RPM mediante CPack (`cpack -G RPM`) o `installer.sh`.

### Compilación de paquete .deb para Debian/Ubuntu

Para generar un paquete `.deb` estándar con todas las dependencias resueltas:

```bash
./compiler.sh
```

Esto generará el paquete `kuteditor_<VERSION>-1_amd64.deb` listo para instalar con:

```bash
sudo apt install ./kuteditor_*.deb
```

> [!NOTE]
> **Compatibilidad de Audio (JACK y PipeWire)**:
> KutEditor utiliza la API de JACK para la reproducción y grabación de audio. 
> - Si tu distribución utiliza **PipeWire** por defecto (como Arch Linux, CachyOS, Fedora, Debian 12+, Ubuntu 23+), asegúrate de instalar el paquete de compatibilidad **`pipewire-jack`**.
> - Si utilizas un servidor **JACK2** tradicional, asegúrate de iniciar el servidor JACK antes de ejecutar la aplicación.

## Licencia

Este proyecto está licenciado bajo los términos de la **GNU General Public License v3.0 (GPL-3.0-or-later)**. Consulta el archivo [LICENSE](LICENSE) para más detalles.

## Parte de KutStudio

KutEditor no es una aplicación aislada, es el compañero de escritorio de **KutPod** (backend/server). KutEditor y KutPod se sincronizan a través de tokens Bearer para asegurar que los creadores puedan enfocarse en el contenido, mientras la plataforma gestiona la logística de almacenamiento, RSS y ActivityPub.
