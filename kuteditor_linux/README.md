# KutEditor

KutEditor es el entorno nativo de escritorio para creadores de podcasts de la suite **KutStudio**. Diseñado desde cero para agilizar el flujo de trabajo, integra edición de audio, gestión de metadatos y publicación directa, todo bajo una misma interfaz fluida e intuitiva.

## Características Principales

* **Creación Desacoplada**: Genera, edita y diseña episodios localmente. Añade marcas de tiempo, capítulos (Podcasting 2.0), arte específico para el episodio y guiones.
* **Integración sin Fricciones**: Se conecta de manera transparente con tu servidor **KutPod** mediante API, permitiendo subir y publicar episodios con un solo clic.
* **Gestión Multimedia**: Herramientas para manipular archivos de audio y compilar metadatos de forma eficiente sin depender de otras aplicaciones pesadas.
* **Flujo Multicuenta**: Gestiona diferentes proyectos de podcast sin tener que iniciar o cerrar sesión repetidamente.
* **Interfaz Profesional**: Interfaz construida en **Qt**, asegurando alto rendimiento y consistencia visual en entornos de escritorio.

## Despliegue e Instalación

KutEditor provee scripts de instalación nativa (como `installer.sh`) y scripts de generación de empaquetados para distribuciones Linux (`PKGBUILD`, RPM, DEB vía CPack), garantizando una instalación profesional que incluye íconos de sistema, entradas de menú y un ejecutable estandarizado (`kuteditor`).

> [!NOTE]
> **Compatibilidad de Audio (JACK y PipeWire)**:
> KutEditor utiliza la API de JACK para la reproducción y grabación de audio. 
> - Si tu distribución utiliza **PipeWire** por defecto (como Arch Linux, CachyOS, Fedora, Ubuntu reciente, etc.), asegúrate de instalar el paquete de compatibilidad **`pipewire-jack`** (este reemplazará a `jack2`).
> - Si utilizas un servidor **JACK2** tradicional, asegúrate de iniciar el servidor JACK antes de ejecutar la aplicación.


## Parte de KutStudio

KutEditor no es una aplicación aislada, es el compañero de escritorio de **KutPod** (backend/server). KutEditor y KutPod se sincronizan a través de tokens Bearer para asegurar que los creadores puedan enfocarse en el contenido, mientras la plataforma gestiona la logística de almacenamiento, RSS y ActivityPub.
