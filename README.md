<img width="1498" height="960" alt="kuteditor" src="https://github.com/user-attachments/assets/07542c9d-029a-4b40-924a-5ba2dcf8acd4" />
<img width="1498" height="960" alt="kuteditor_editor" src="https://github.com/user-attachments/assets/18786c03-61f1-401f-bb2e-a6f0f486d8aa" />
<img width="1407" height="868" alt="kuteditor_chapters_editor" src="https://github.com/user-attachments/assets/569b2c0b-856d-400b-9fd0-6198a9015d7a" />

# KutEditor

<p align="center">
  <strong>🇬🇧 <a href="#english">English</a></strong> · <strong>🇪🇸 <a href="#español">Español</a></strong>
</p>

<p align="center">
  <em>The native, open-source multitrack audio editor and podcast production studio built with Qt & C++.</em>
</p>

---

<a name="english"></a>
## 🇬🇧 English

**KutEditor** is the native desktop workstation for podcasters and audio creators in the **KutStudio** suite. Designed from the ground up to streamline the podcasting workflow, it integrates multitrack recording and editing, metadata management (Podcasting 2.0 chapters, artwork, ID3 tags), and seamless one-click publishing to **KutPod** servers.

### Key Features

* **Multitrack Recording & Editing**: High-performance, low-latency audio capture and editing with independent track gain, panning, solo/mute controls, and clean timeline navigation.
* **Low-Latency Native Audio Engines**:
  * **Linux**: Native JACK API integration with full PipeWire (`pipewire-jack`) compatibility. Supports physical hardware interfaces (e.g. RØDECaster Pro/Duo, Streamer X) as well as virtual routing channels (UNIFY, CasterManager, Loopback).
  * **macOS**: Native CoreAudio HAL driver support for Apple Silicon & Intel.
  * **Windows**: Native WASAPI (Windows Audio Session API) endpoint management.
* **Podcasting 2.0 Ready**: Built-in visual editor for Podcasting 2.0 chapters, episode-specific cover art, timestamps, URLs, and show notes.
* **Seamless KutPod Cloud Sync**: Connects directly to your **KutPod** instance via REST API and Bearer tokens, allowing you to upload audio, generate RSS feeds, and publish episodes without opening a browser.
* **Multi-Account & Multi-Show Management**: Easily switch between shows and server instances without repeatedly logging in or out.
* **Modern & Lightweight Qt Interface**: Fast, responsive GUI built on Qt 6 with native dark theme support, cross-platform UI consistency, and zero bloat.

### Platform Support & Installation

#### Linux

KutEditor provides build scripts and native packages for major distributions:

* **Arch Linux / Manjaro / CachyOS**: Native package build via `PKGBUILD` or `installer.sh`:
  ```bash
  cd kuteditor_linux
  makepkg -si
  ```
* **Debian / Ubuntu / Derivatives**: Generate a standard `.deb` package with all dependencies resolved:
  ```bash
  cd kuteditor_linux
  ./compiler.sh
  sudo apt install ./kuteditor_*.deb
  ```
* **Fedora / RHEL**: Build native RPM packages using CPack (`cpack -G RPM`) or `installer.sh`.

> [!NOTE]
> **Audio Compatibility (JACK & PipeWire)**:
> KutEditor uses the native JACK API on Linux.
> * If your distribution uses **PipeWire** (Arch Linux, Fedora, Ubuntu 23+, Debian 12+), ensure the `pipewire-jack` package is installed.
> * If using a traditional **JACK2** server, start the JACK daemon before launching the application.

#### macOS

Pre-built Universal binaries (`.dmg`) supporting macOS Monterey and later (Apple Silicon M1/M2/M3/M4 & Intel) can be downloaded from [kutstudio.org](https://kutstudio.org) or built using CMake under [`kuteditor_macos/`](kuteditor_macos/).

#### Windows

Windows 10/11 binaries and build instructions using MinGW/MSVC and CMake are available under [`kuteditor_windows/`](kuteditor_windows/).

### License

This project is licensed under the **GNU General Public License v3.0 (GPL-3.0-or-later)**. See the [LICENSE](LICENSE) file for more information.

---

<a name="español"></a>
## 🇪🇸 Español

**KutEditor** es el entorno nativo de escritorio para creadores de podcasts de la suite **KutStudio**. Diseñado desde cero para agilizar el flujo de trabajo, integra grabación y edición multipista, gestión de metadatos (capítulos Podcasting 2.0, arte específico, etiquetas ID3) y publicación directa con servidores **KutPod**.

### Características Principales

* **Grabación y Edición Multipista**: Captura y edición de audio de alto rendimiento y baja latencia con control de ganancia por pista, paneo, solo/silencio (Mute/Solo) y navegación fluida en la línea de tiempo.
* **Motores de Audio Nativos de Baja Latencia**:
  * **Linux**: API nativa de JACK con compatibilidad total con PipeWire (`pipewire-jack`). Soporta interfaces físicas (como RØDECaster Duo, Streamer X) y canales de audio virtuales (UNIFY, CasterManager, Loopback).
  * **macOS**: Soporte nativo de CoreAudio HAL optimizado para Apple Silicon e Intel.
  * **Windows**: Gestión de dispositivos mediante la API nativa WASAPI.
* **Soporte Podcasting 2.0**: Editor visual integrado para capítulos Podcasting 2.0, portadas individuales por episodio, marcas de tiempo, enlaces y notas del programa.
* **Sincronización Directa con KutPod**: Conexión transparente con tu servidor **KutPod** mediante API REST y tokens Bearer, permitiendo subir y publicar episodios con un solo clic sin necesidad de abrir el navegador.
* **Gestión Multicuenta y Multipodcast**: Administra múltiples proyectos y podcasts sin tener que iniciar o cerrar sesión constantemente.
* **Interfaz Profesional en Qt**: Desarrollado en Qt 6, garantizando un rendimiento sobresaliente, tema oscuro moderno y consistencia visual en todos los entornos de escritorio.

### Soporte de Plataformas e Instalación

#### Linux

KutEditor provee herramientas y scripts de empaquetado para las principales distribuciones:

* **Arch Linux / Manjaro / CachyOS**: Compilación e instalación nativa con `PKGBUILD` o `installer.sh`:
  ```bash
  cd kuteditor_linux
  makepkg -si
  ```
* **Debian / Ubuntu / Derivadas**: Generación de paquete fuente y binario `.deb` nativo mediante `compiler.sh`:
  ```bash
  cd kuteditor_linux
  ./compiler.sh
  sudo apt install ./kuteditor_*.deb
  ```
* **Fedora / RHEL**: Empaquetado RPM mediante CPack (`cpack -G RPM`) o `installer.sh`.

> [!NOTE]
> **Compatibilidad de Audio (JACK y PipeWire)**:
> KutEditor utiliza la API nativa de JACK en Linux.
> * Si tu distribución utiliza **PipeWire** (Arch Linux, Fedora, Ubuntu 23+, Debian 12+), asegúrate de instalar el paquete de compatibilidad `pipewire-jack`.
> * Si utilizas un servidor **JACK2** tradicional, inicia el servidor JACK antes de ejecutar la aplicación.

#### macOS

Los paquetes binarios universales (`.dmg`) compatibles con macOS Monterey y versiones superiores (Apple Silicon & Intel) están disponibles para descarga en [kutstudio.org](https://kutstudio.org) o para compilar mediante CMake en [`kuteditor_macos/`](kuteditor_macos/).

#### Windows

Instrucciones de compilación con MinGW/MSVC y CMake para Windows 10/11 disponibles en [`kuteditor_windows/`](kuteditor_windows/).

### Licencia

Este proyecto está licenciado bajo la **GNU General Public License v3.0 (GPL-3.0-or-later)**. Consulta el archivo [LICENSE](LICENSE) para más información.

---

## 💖 Support & Donations / Soporte y Donaciones

If you find this project useful and want to support its ongoing development, consider making a donation. Every contribution helps keep the project active, independent, and constantly improving!

Si este proyecto te resulta útil y deseas apoyar su desarrollo continuo, considera hacer una donación. ¡Cada aporte ayuda a mantener el proyecto activo, independiente y en constante evolución!

* ☕ **Buy Me a Coffee**: [buymeacoffee.com/ernestoacostame](https://www.buymeacoffee.com/ernestoacostame)
* 💖 **Ko-fi**: [ko-fi.com/ernestoacostame](https://ko-fi.com/ernestoacostame)
* 🅿️ **PayPal**: [paypal.com/paypalme/elav](https://www.paypal.com/paypalme/elav)

<p align="center">
  <a href="https://www.buymeacoffee.com/ernestoacostame" target="_blank">
    <img src="https://img.shields.io/badge/Buy%20Me%20a%20Coffee-Invítame%20un%20café-FFDD00?style=for-the-badge&logo=buy-me-a-coffee&logoColor=black" alt="Buy Me a Coffee">
  </a>
  &nbsp;&nbsp;
  <a href="https://ko-fi.com/ernestoacostame" target="_blank">
    <img src="https://img.shields.io/badge/Ko--fi-Apóyame-FF5E5B?style=for-the-badge&logo=ko-fi&logoColor=white" alt="Ko-fi">
  </a>
  &nbsp;&nbsp;
  <a href="https://www.paypal.com/paypalme/elav" target="_blank">
    <img src="https://img.shields.io/badge/PayPal-Donar%20%2F%20Donate-00457C?style=for-the-badge&logo=paypal&logoColor=white" alt="PayPal">
  </a>
</p>

