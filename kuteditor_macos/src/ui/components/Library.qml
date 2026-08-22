import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import KutIcons 1.0


/**
 * Library.qml — Pantalla inicial de KutEditor.
 *
 * Muestra los podcasts del usuario como tarjetas. Al abrir un podcast,
 * muestra los episodios. Al abrir un episodio, emite openEpisode(path).
 *
 * Estructura en disco:
 *   <projectsDir>/
 *     Mi Podcast/
 *       podcast.json     ← { "name": "...", "author": "...", "created": "..." }
 *       cover.jpg        ← portada (opcional)
 *       Episodio 1/
 *         episodio.kutproj
 *       Episodio 2/
 *         episodio.kutproj
 */
Rectangle {
    id: lib
    color: palette.window

    // Directorio base de los proyectos.
    property string projectsDir: ""

    // Podcast abierto actualmente (null = lista de podcasts).
    property var currentPodcast: null

    // Señales
    signal openEpisode(string path)
    signal skipLibrary()
    signal podcastCreated()
    signal recoverSessionRequested()

    // Datos
    property var podcasts: []
    property var episodes: []
    property int podcastListRevision: 0
    property int coverRevision: 0


    Component.onCompleted: refreshPodcasts()

    function getBaseDir() {
        if (projectsDir !== "") return projectsDir
        // Fallback a ~/KutProjects
        const home = FileHelper.homeDir()
        return home + "/KutProjects"
    }

    function refreshPodcasts() {
        const dir = getBaseDir()
        FileHelper.ensureDir(dir)
        const folders = FileHelper.listDirs(dir)
        const list = []
        for (let i = 0; i < folders.length; i++) {
            const f = folders[i]
            const jsonPath = dir + "/" + f + "/podcast.json"
            let meta = { name: f, author: "", created: "",
                         rememberPrefs: false, lastEpisode: 0,
                         lastSeason: 1, lastTracks: [] }
            if (FileHelper.fileExists(jsonPath)) {
                try {
                    const raw = FileHelper.readTextFile(jsonPath)
                    const parsed = JSON.parse(raw)
                    if (parsed.name) meta.name = parsed.name
                    if (parsed.author) meta.author = parsed.author
                    if (parsed.created) meta.created = parsed.created
                    if (parsed.rememberPrefs !== undefined)
                        meta.rememberPrefs = !!parsed.rememberPrefs
                    if (parsed.lastEpisode !== undefined)
                        meta.lastEpisode = Number(parsed.lastEpisode)
                    if (parsed.lastSeason !== undefined)
                        meta.lastSeason = Number(parsed.lastSeason)
                    if (parsed.lastTracks !== undefined)
                        meta.lastTracks = parsed.lastTracks
                } catch(e) {}
            }
            meta.folder = f
            meta.path = dir + "/" + f
            // Buscar portada con cualquier extensión
            meta.hasCover = false
            meta.coverPath = ""
            const exts = [".jpg", ".jpeg", ".png", ".webp"]
            for (let e = 0; e < exts.length; e++) {
                const cp = dir + "/" + f + "/cover" + exts[e]
                if (FileHelper.fileExists(cp)) {
                    meta.hasCover = true
                    meta.coverPath = cp
                    break
                }
            }
            list.push(meta)
        }
        podcasts = list
        podcastListRevision++
    }

    function openPodcast(podcast) {
        currentPodcast = podcast
        refreshEpisodes()
    }

    function refreshEpisodes() {
        if (!currentPodcast) { episodes = []; return }
        const dir = currentPodcast.path
        const folders = FileHelper.listDirs(dir)
        const list = []
        for (let i = 0; i < folders.length; i++) {
            const f = folders[i]
            const epDir = dir + "/" + f

            // Buscar cualquier .kutproj en la carpeta del episodio
            const files = FileHelper.listFiles(epDir, "*.kutproj")
            if (files.length > 0) {
                const projPath = epDir + "/" + files[0]
                const mtime = FileHelper.lastModified(projPath)
                list.push({ name: f, path: projPath, folder: f, mtime: mtime })
            }
        }
        // Ordenar por fecha de modificación (más reciente primero)
        list.sort(function(a, b) { return b.mtime - a.mtime })
        episodes = list
    }

    function createPodcast(name, author) {
        const dir = getBaseDir() + "/" + name
        FileHelper.ensureDir(dir)
        const meta = JSON.stringify({
            name: name,
            author: author,
            created: new Date().toISOString(),
            rememberPrefs: true,
            lastEpisode: 0,
            lastSeason: 1,
            lastTracks: []
        })
        FileHelper.writeTextFile(dir + "/podcast.json", meta)

        // Crear también un .kutproj raíz del podcast con la metadata
        // pre-rellenada. Sirve como plantilla: cuando se creen episodios,
        // heredarán esta configuración base.
        const safeName = name.replace(/[\/\\:*?"<>|]/g, "_")
        const rootProj = {
            version: 1,
            sampleRate: 48000,
            metadata: {
                title:       "",
                podcast:     name,
                podcaster:   author,
                description: "",
                episode:     0,
                season:      1,
                coverPath:   ""
            },
            tracks: []
        }
        FileHelper.writeTextFile(dir + "/" + safeName + ".kutproj",
                                 JSON.stringify(rootProj, null, 2))

        refreshPodcasts()
        podcastCreated()
    }

    function createEpisode(episodeName) {
        if (!currentPodcast) return
        const dir = currentPodcast.path + "/" + episodeName
        FileHelper.ensureDir(dir)

        // Nombre del archivo = nombre del podcast (sanitizado)
        const safeName = currentPodcast.name.replace(/[\/\\:*?"<>|]/g, "_")
        const projFile = dir + "/" + safeName + ".kutproj"

        // Determinar episodio y temporada
        let epNum = episodes.length + 1
        let seasonNum = 1
        let tracks = []

        if (currentPodcast.rememberPrefs) {
            epNum = currentPodcast.lastEpisode + 1
            seasonNum = currentPodcast.lastSeason
            if (currentPodcast.lastTracks && currentPodcast.lastTracks.length > 0)
                tracks = currentPodcast.lastTracks
        }

        const meta = {
            version: 1,
            sampleRate: 48000,
            metadata: {
                title:       episodeName,
                podcast:     currentPodcast.name,
                podcaster:   currentPodcast.author || "",
                description: "",
                episode:     epNum,
                season:      seasonNum,
                coverPath:   currentPodcast.hasCover ? currentPodcast.coverPath : ""
            },
            tracks: tracks
        }
        FileHelper.writeTextFile(projFile, JSON.stringify(meta, null, 2))
        refreshEpisodes()
    }

    /// Guarda los metadatos del podcast (rememberPrefs, lastEpisode, etc.)
    function savePodcastMeta(podcast) {
        if (!podcast) return
        const jsonPath = podcast.path + "/podcast.json"
        const data = {
            name:           podcast.name,
            author:         podcast.author || "",
            created:        podcast.created || "",
            rememberPrefs:  podcast.rememberPrefs || false,
            lastEpisode:    podcast.lastEpisode || 0,
            lastSeason:     podcast.lastSeason || 1,
            lastTracks:     podcast.lastTracks || []
        }
        FileHelper.writeTextFile(jsonPath, JSON.stringify(data, null, 2))
    }

    /// Actualiza las preferencias del podcast tras exportar un episodio.
    /// Llamada desde main.qml al terminar la exportación.
    function updatePodcastAfterExport(podcastPath, episodeNum, seasonNum, trackConfigs) {
        if (!podcastPath) return
        const jsonPath = podcastPath + "/podcast.json"
        if (!FileHelper.fileExists(jsonPath)) return
        try {
            const raw = FileHelper.readTextFile(jsonPath)
            const parsed = JSON.parse(raw)
            if (!parsed.rememberPrefs) return
            parsed.lastEpisode = episodeNum
            parsed.lastSeason = seasonNum
            parsed.lastTracks = trackConfigs
            FileHelper.writeTextFile(jsonPath, JSON.stringify(parsed, null, 2))
        } catch(e) {}
    }

    /// Renombrar un episodio (carpeta + título en el .kutproj)
    function renameEpisode(episode, newName) {
        if (!currentPodcast || !episode) return
        const oldProjPath = episode.path
        const oldDir = oldProjPath.substring(0, oldProjPath.lastIndexOf("/"))
        const parentDir = currentPodcast.path
        const oldFolderName = episode.folder

        const newDir = parentDir + "/" + newName
        const fileName = FileHelper.fileName(oldProjPath)
        const newProjPath = newDir + "/" + fileName

        // 1. Renombrar carpeta
        if (oldFolderName !== newName) {
            const ok = FileHelper.renameDir(oldDir, newName)
            if (!ok) return
        }

        // 2. Actualizar título en el .kutproj (C++ streaming, sin cargar audio a RAM)
        ProjectIO.updateProjectPathAndTitle(oldProjPath, newProjPath, newName)

        refreshEpisodes()
    }

    function deletePodcast(podcast) {
        // Solo elimina el podcast.json (no borra los episodios por seguridad)
        // En realidad mover a papelera sería mejor, pero por ahora solo refresh
        refreshPodcasts()
    }

    // ================================================================
    //  UI
    // ================================================================

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        TabBar {
            id: libTabs
            Layout.fillWidth: true
            background: Rectangle { color: palette.window }

            TabButton {
                text: qsTr("Local")
                width: 150
                topPadding: 8
                bottomPadding: 8
                background: Rectangle {
                    color: "transparent"
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 2
                        color: palette.highlight
                        visible: libTabs.currentIndex === 0
                    }
                }
            }
            TabButton {
                text: qsTr("Online (KutPod)")
                visible: FeatureKutPod
                width: visible ? 150 : 0
                topPadding: 8
                bottomPadding: 8
                background: Rectangle {
                    color: "transparent"
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 2
                        color: palette.highlight
                        visible: libTabs.currentIndex === 1
                    }
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: libTabs.currentIndex

            // Pestaña 1: Biblioteca local
            Item {
                id: localTab

                StackView {
                    id: stack
                    anchors.fill: parent
                    initialItem: podcastListPage
                }
            }

            // Pestaña 2: Biblioteca remota
            Loader {
                Layout.fillWidth: true
                Layout.fillHeight: true
                active: FeatureKutPod
                sourceComponent: Component {
                    OnlineLibrary {
                        onBackToLocal: libTabs.setCurrentIndex(0)
                    }
                }
            }
        }
    }

    // --- Página 1: Lista de podcasts ---
    Component {
        id: podcastListPage
        Item {
            ScrollView {
                anchors.fill: parent
                anchors.margins: 20
                clip: true

                Column {
                    width: parent.width
                    spacing: 20

                    // Banner de recuperación pendiente si existe una sesión previa no guardada
                    Rectangle {
                        width: parent.width
                        height: 52
                        radius: 8
                        color: "#d35400"
                        visible: ProjectIO.hasRecoveryProject()

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 16
                            anchors.rightMargin: 16
                            spacing: 12

                            Label {
                                text: "⚠️ " + qsTr("Se detectó una sesión de grabación no guardada pendiente de recuperar.")
                                color: "#ffffff"
                                font.bold: true
                                font.pixelSize: 13
                                Layout.fillWidth: true
                            }

                            Button {
                                text: qsTr("Recuperar audio")
                                palette.buttonText: "#d35400"
                                palette.button: "#ffffff"
                                onClicked: lib.recoverSessionRequested()
                            }

                            Button {
                                text: qsTr("Descartar copia")
                                flat: true
                                palette.buttonText: "#f1c40f"
                                onClicked: ProjectIO.discardRecovery()
                            }
                        }
                    }

                    // Cabecera
                    RowLayout {
                        width: parent.width
                        Label {
                            text: qsTr("Mis Podcasts")
                            color: root.textPri
                            font.pixelSize: 28
                            font.bold: true
                            Layout.fillWidth: true
                        }
                        Button {
                            text: "+ " + qsTr("Nuevo podcast")
                            onClicked: newPodcastDialog.open()
                        }
                    }

                    // Grid de podcasts (alineados a la izquierda)
                    Flow {
                        width: parent.width
                        spacing: 16

                        Repeater {
                            model: lib.podcasts
                            delegate: Rectangle {
                                required property var modelData
                                required property int index
                                width: 200
                                height: 240
                                radius: 8
                                color: podcastHover.hovered ? palette.midlight : palette.base
                                border.color: podcastHover.hovered ? palette.highlight : palette.mid
                                border.width: 1

                                Column {
                                    anchors.fill: parent
                                    anchors.margins: 10
                                    spacing: 8

                                    // Portada
                                    Rectangle {
                                        width: parent.width
                                        height: 140
                                        radius: 6
                                        color: palette.window
                                        clip: true

                                        Image {
                                            anchors.fill: parent
                                            source: modelData.hasCover
                                                ? "file://" + modelData.coverPath + "?v=" + lib.podcastListRevision
                                                : ""
                                            sourceSize: Qt.size(200, 200)
                                            asynchronous: true
                                            fillMode: Image.PreserveAspectCrop
                                            visible: modelData.hasCover
                                            cache: false
                                        }

                                        Label {
                                            anchors.centerIn: parent
                                            text: "🎙"
                                            font.pixelSize: 48
                                            visible: !modelData.hasCover
                                            color: root.textPri
                                        }
                                    }

                                    // Nombre
                                    Label {
                                        text: modelData.name
                                        color: root.textPri
                                        font.pixelSize: 14
                                        font.bold: true
                                        elide: Text.ElideRight
                                        width: parent.width
                                    }

                                    // Autor
                                    Label {
                                        text: modelData.author || qsTr("Sin autor")
                                        color: root.textSec
                                        font.pixelSize: 11
                                        elide: Text.ElideRight
                                        width: parent.width
                                    }
                                }

                                // Botón eliminar (esquina superior derecha)
                                Button {
                                    id: deleteBtn
                                    anchors.top: parent.top
                                    anchors.right: parent.right
                                    anchors.margins: 6
                                    width: 28; height: 28
                                    flat: true
                                    visible: podcastHover.hovered || deleteBtn.hovered
                                    z: 10
                                    hoverEnabled: true
                                    onClicked: {
                                        deletePodcastDialog.podcastToDelete = modelData
                                        deletePodcastDialog.open()
                                    }
                                    background: Rectangle {
                                        radius: 14
                                        color: deleteBtn.hovered
                                            ? Qt.rgba(1, 0.2, 0.2, 1)
                                            : Qt.rgba(0.9, 0.2, 0.2, 0.7)
                                    }
                                    contentItem: Label {
                                        anchors.centerIn: parent
                                        text: "✕"
                                        color: palette.brightText
                                        font.pixelSize: 14
                                        font.bold: true
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                }

                                HoverHandler {
                                    id: podcastHover
                                }

                                MouseArea {
                                    id: podcastMA
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        lib.openPodcast(modelData)
                                        stack.push(episodeListPage)
                                    }
                                }
                            }
                        }
                    }

                    // Mensaje si no hay podcasts + botón Skip
                    Column {
                        visible: lib.podcasts.length === 0
                        width: parent.width
                        spacing: 16
                        topPadding: 60

                        Label {
                            text: qsTr("No hay podcasts todavía.\nCrea uno con el botón de arriba, o salta al editor.")
                            color: root.textSec
                            font.pixelSize: 14
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            width: parent.width
                        }

                        Button {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: qsTr("Ir al editor (no mostrar esta pantalla de nuevo)")
                            onClicked: lib.skipLibrary()
                        }
                    }
                }
            }
        }
    }

    // --- Página 2: Episodios de un podcast ---
    Component {
        id: episodeListPage
        Item {

            ScrollView {
                anchors.fill: parent
                clip: true

                Flickable {
                    contentWidth: parent.width
                    contentHeight: epContent.height + 40

                    Column {
                        id: epContent
                        x: 20
                        width: parent.width - 40
                        spacing: 20
                        topPadding: 16

                        // Botón volver
                        Button {
                            text: "← " + qsTr("Volver")
                            onClicked: {
                                lib.currentPodcast = null
                                lib.refreshPodcasts()
                                stack.pop()
                            }
                        }

                        // Header: portada + info del podcast
                        Row {
                            width: parent.width
                            spacing: 20

                            // Portada (click para cambiar)
                            Rectangle {
                                width: 140
                                height: 140
                                radius: 8
                                color: palette.base
                                border.color: coverMA.containsMouse ? palette.highlight : palette.mid
                                border.width: 1
                                clip: true

                                Image {
                                    id: coverImage
                                    anchors.fill: parent
                                    anchors.margins: 1
                                    source: (lib.currentPodcast && lib.currentPodcast.hasCover)
                                        ? "file://" + lib.currentPodcast.coverPath + "?v=" + coverRevision
                                        : ""
                                    sourceSize: Qt.size(300, 300)
                                    asynchronous: true
                                    fillMode: Image.PreserveAspectCrop
                                    visible: lib.currentPodcast && lib.currentPodcast.hasCover
                                    cache: false
                                }

                                Column {
                                    anchors.centerIn: parent
                                    visible: !(lib.currentPodcast && lib.currentPodcast.hasCover)
                                    spacing: 4
                                    Label {
                                        text: "🎙"
                                        font.pixelSize: 40
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        color: root.textPri
                                    }
                                    Label {
                                        text: qsTr("Añadir portada")
                                        color: root.textSec
                                        font.pixelSize: 10
                                        anchors.horizontalCenter: parent.horizontalCenter
                                    }
                                }

                                Rectangle {
                                    anchors.fill: parent
                                    color: Qt.rgba(0, 0, 0, 0.5)
                                    visible: coverMA.containsMouse
                                    radius: 8
                                    Label {
                                        anchors.centerIn: parent
                                        text: qsTr("Cambiar portada")
                                        color: palette.brightText
                                        font.pixelSize: 12
                                        font.bold: true
                                    }
                                }

                                MouseArea {
                                    id: coverMA
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        console.log("Cambiar portada MouseArea clicked")
                                        const src = FileHelper.getOpenFileName(
                                            qsTr("Seleccionar portada"),
                                            FileHelper.homeDir() + "/Pictures",
                                            "Im\u00e1genes (*.jpg *.jpeg *.png *.webp)"
                                        )
                                        if (lib.currentPodcast && src !== "") {
                                            console.log("Selected cover path from C++ dialog: " + src)
                                            const ext = src.substring(src.lastIndexOf(".")).toLowerCase()
                                            const dst = lib.currentPodcast.path + "/cover" + ext
                                            const exts = [".jpg", ".jpeg", ".png", ".webp"]
                                            for (let e = 0; e < exts.length; e++) {
                                                const cp = lib.currentPodcast.path + "/cover" + exts[e]
                                                if (FileHelper.fileExists(cp)) {
                                                    FileHelper.removeFile(cp)
                                                }
                                            }
                                            const ok = FileHelper.copyFile(src, dst)
                                            if (ok) {
                                                lib.currentPodcast.hasCover = true
                                                lib.currentPodcast.coverPath = dst
                                                lib.currentPodcast = Object.assign({}, lib.currentPodcast)
                                                lib.refreshPodcasts()
                                                lib.coverRevision++
                                            }
                                        }
                                    }
                                }
                            }

                            // Info del podcast
                            Column {
                                spacing: 8
                                width: parent.width - 160

                                Label {
                                    text: lib.currentPodcast ? lib.currentPodcast.name : ""
                                    color: root.textPri
                                    font.pixelSize: 24
                                    font.bold: true
                                    wrapMode: Text.WordWrap
                                    width: parent.width
                                }

                                Label {
                                    text: lib.currentPodcast ? (lib.currentPodcast.author || "") : ""
                                    color: root.textSec
                                    font.pixelSize: 13
                                    visible: text !== ""
                                }

                                Item { width: 1; height: 4 }

                                Button {
                                    text: "+ " + qsTr("Nuevo episodio")
                                    onClicked: newEpisodeDialog.open()
                                }

                                CheckBox {
                                    id: rememberPrefsCheck
                                    text: qsTr("Recordar preferencias del último episodio")
                                    checked: lib.currentPodcast ? (lib.currentPodcast.rememberPrefs || false) : false
                                    palette.windowText: palette.text
                                    palette.text: palette.text
                                    onToggled: {
                                        if (lib.currentPodcast) {
                                            lib.currentPodcast.rememberPrefs = checked
                                            lib.savePodcastMeta(lib.currentPodcast)
                                        }
                                    }
                                    ToolTip.text: qsTr("Al crear un episodio nuevo, heredar las pistas, dispositivos,\nnúmero de episodio y temporada del último episodio exportado.")
                                    ToolTip.visible: hovered
                                    ToolTip.delay: 600
                                }
                            }
                        }

                        // Separador
                        Rectangle { width: parent.width; height: 1; color: palette.mid }

                        // Label "Episodios"
                        Label {
                            text: qsTr("Episodios")
                            color: root.textPri
                            font.pixelSize: 16
                            font.bold: true
                        }

                        // Lista de episodios
                        Column {
                            width: parent.width
                            spacing: 8

                            Repeater {
                                model: lib.episodes
                                delegate: Rectangle {
                                    required property var modelData
                                    required property int index
                                    width: parent.width
                                    height: 52
                                    radius: 6
                                    color: epHover.hovered ? palette.midlight : palette.base
                                    border.color: epHover.hovered ? palette.highlight : palette.mid
                                    border.width: 1

                                    HoverHandler { id: epHover }

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 12
                                        anchors.rightMargin: 8
                                        spacing: 10

                                        Text {
                                            text: Icons.microphone
                                            color: root.textPri
                                            font.family: "Material Symbols Outlined"
                                            font.pixelSize: 24
                                            font.weight: Font.Thin
                                            renderType: Text.NativeRendering
                                            opacity: 0.85
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                            Layout.preferredWidth: 24; Layout.preferredHeight: 24
                                        }

                                        Label {
                                            text: modelData.name
                                            color: root.textPri
                                            font.pixelSize: 13
                                            font.bold: true
                                            Layout.fillWidth: true
                                            elide: Text.ElideRight
                                        }

                                        Button {
                                            text: qsTr("Abrir")
                                            onClicked: lib.openEpisode(modelData.path)
                                        }

                                        Button {
                                            flat: true
                                            implicitWidth: 32; implicitHeight: 32
                                            onClicked: {
                                                renameEpisodeDialog.episodeToRename = modelData
                                                renameEpisodeField.text = modelData.name
                                                renameEpisodeDialog.open()
                                            }
                                            ToolTip.text: qsTr("Renombrar episodio")
                                            ToolTip.visible: hovered
                                            background: Rectangle { radius: 4; color: parent.hovered ? Qt.rgba(1,1,1,0.08) : "transparent" }
                                            contentItem: Text { anchors.centerIn: parent; text: Icons.pencil; color: root.textPri; font.family: "Material Symbols Outlined"; font.pixelSize: 24; font.weight: Font.Thin; renderType: Text.NativeRendering; opacity: 0.85; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                        }

                                        Button {
                                            flat: true
                                            implicitWidth: 32; implicitHeight: 32
                                            onClicked: {
                                                deleteEpisodeDialog.episodeToDelete = modelData
                                                deleteEpisodeDialog.open()
                                            }
                                            ToolTip.text: qsTr("Eliminar episodio")
                                            ToolTip.visible: hovered
                                            background: Rectangle { radius: 4; color: parent.hovered ? Qt.rgba(1,1,1,0.08) : "transparent" }
                                            contentItem: Text { anchors.centerIn: parent; text: Icons.trash; color: root.textPri; font.family: "Material Symbols Outlined"; font.pixelSize: 24; font.weight: Font.Thin; renderType: Text.NativeRendering; opacity: 0.85; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                        }
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: lib.openEpisode(modelData.path)
                                        z: -1
                                    }
                                }
                            }
                        }

                        // Mensaje si no hay episodios
                        Label {
                            visible: lib.episodes.length === 0
                            text: qsTr("No hay episodios en este podcast.\nCrea uno con el botón «+ Nuevo episodio».")
                            color: root.textSec
                            font.pixelSize: 14
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            width: parent.width
                            topPadding: 20
                        }
                    }
                }
            }
        }
    }

    // --- Diálogo: Nuevo podcast ---
    Dialog {
        id: newPodcastDialog
        title: qsTr("Nuevo podcast")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.NoButton
        width: 350
        palette.window: "#2a2d31"
        palette.windowText: "#ecf0f1"
        palette.text: "#ecf0f1"
        palette.base: "#1a1d21"
        palette.button: "#34383d"
        palette.buttonText: "#ecf0f1"
        palette.toolTipBase: "#1a1d21"
        palette.toolTipText: "#ecf0f1"

        contentItem: Column {
            spacing: 12
            padding: 16

            Label { text: qsTr("Nombre del podcast"); color: root.textPri }
            TextField {
                id: newPodcastName
                width: parent.width - 32
                placeholderText: qsTr("Mi podcast")
                placeholderTextColor: root.textSec
                color: root.textPri
            }

            Label { text: qsTr("Autor / Podcaster"); color: root.textPri }
            TextField {
                id: newPodcastAuthor
                width: parent.width - 32
                placeholderText: qsTr("Tu nombre")
                placeholderTextColor: root.textSec
                color: root.textPri
            }

            Row {
                spacing: 8
                anchors.right: parent.right
                anchors.rightMargin: 16
                Button {
                    text: qsTr("Crear")
                    onClicked: {
                        if (newPodcastName.text.trim() !== "") {
                            lib.createPodcast(newPodcastName.text.trim(), newPodcastAuthor.text.trim())
                            newPodcastName.text = ""
                            newPodcastAuthor.text = ""
                            newPodcastDialog.close()
                        }
                    }
                }
                Button {
                    text: qsTr("Cancelar")
                    onClicked: newPodcastDialog.close()
                }
            }
        }
    }

    // --- Diálogo: Nuevo episodio ---
    Dialog {
        id: newEpisodeDialog
        title: qsTr("Nuevo episodio")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.NoButton
        width: 350
        palette.window: "#2a2d31"
        palette.windowText: "#ecf0f1"
        palette.text: "#ecf0f1"
        palette.base: "#1a1d21"
        palette.button: "#34383d"
        palette.buttonText: "#ecf0f1"
        palette.toolTipBase: "#1a1d21"
        palette.toolTipText: "#ecf0f1"

        contentItem: Column {
            spacing: 12
            padding: 16

            Label { text: qsTr("Nombre del episodio"); color: root.textPri }
            TextField {
                id: newEpisodeName
                width: parent.width - 32
                placeholderText: qsTr("Episodio 1")
                placeholderTextColor: root.textSec
                color: root.textPri
                onAccepted: {
                    if (text.trim() !== "") {
                        lib.createEpisode(text.trim())
                        text = ""
                        newEpisodeDialog.close()
                    }
                }
            }

            Row {
                spacing: 8
                anchors.right: parent.right
                anchors.rightMargin: 16
                Button {
                    text: qsTr("Crear")
                    onClicked: {
                        if (newEpisodeName.text.trim() !== "") {
                            lib.createEpisode(newEpisodeName.text.trim())
                            newEpisodeName.text = ""
                            newEpisodeDialog.close()
                        }
                    }
                }
                Button {
                    text: qsTr("Cancelar")
                    onClicked: newEpisodeDialog.close()
                }
            }
        }
    }

    // --- Diálogo: Confirmar eliminación de podcast ---
    Dialog {
        id: deletePodcastDialog
        title: qsTr("Eliminar podcast")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.NoButton
        width: 380
        palette.window: "#2a2d31"
        palette.windowText: "#ecf0f1"
        palette.text: "#ecf0f1"
        palette.base: "#1a1d21"
        palette.button: "#34383d"
        palette.buttonText: "#ecf0f1"
        palette.toolTipBase: "#1a1d21"
        palette.toolTipText: "#ecf0f1"

        property var podcastToDelete: null

        contentItem: Column {
            spacing: 16
            padding: 16

            Label {
                text: deletePodcastDialog.podcastToDelete
                    ? qsTr("¿Estás seguro de que quieres eliminar el podcast «%1»?\n\nSe eliminarán todos los episodios y archivos de audio. Esta acción no se puede deshacer.").arg(deletePodcastDialog.podcastToDelete.name)
                    : ""
                color: root.textPri
                wrapMode: Text.WordWrap
                width: parent.width - 32
            }

            Row {
                spacing: 8
                anchors.right: parent.right
                anchors.rightMargin: 16
                Button {
                    text: qsTr("Sí, eliminar")
                    onClicked: {
                        if (deletePodcastDialog.podcastToDelete) {
                            FileHelper.removeDir(deletePodcastDialog.podcastToDelete.path)
                            lib.refreshPodcasts()
                        }
                        deletePodcastDialog.close()
                    }
                    palette.buttonText: "white"
                    palette.button: "#c0392b"
                }
                Button {
                    text: qsTr("No, cancelar")
                    onClicked: deletePodcastDialog.close()
                }
            }
        }
    }

    // --- Diálogo: Confirmar eliminación de episodio ---
    Dialog {
        id: deleteEpisodeDialog
        title: qsTr("Eliminar episodio")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.NoButton
        width: 380
        palette.window: "#2a2d31"
        palette.windowText: "#ecf0f1"
        palette.text: "#ecf0f1"
        palette.base: "#1a1d21"
        palette.button: "#34383d"
        palette.buttonText: "#ecf0f1"
        palette.toolTipBase: "#1a1d21"
        palette.toolTipText: "#ecf0f1"

        property var episodeToDelete: null

        contentItem: Column {
            spacing: 16
            padding: 16

            Label {
                text: deleteEpisodeDialog.episodeToDelete
                    ? qsTr("¿Estás seguro de que quieres eliminar el episodio «%1»?\n\nSe eliminarán los archivos de audio. Esta acción no se puede deshacer.").arg(deleteEpisodeDialog.episodeToDelete.name)
                    : ""
                color: root.textPri
                wrapMode: Text.WordWrap
                width: parent.width - 32
            }

            Row {
                spacing: 8
                anchors.right: parent.right
                anchors.rightMargin: 16
                Button {
                    text: qsTr("Sí, eliminar")
                    palette.buttonText: "white"
                    palette.button: "#c0392b"
                    onClicked: {
                        if (deleteEpisodeDialog.episodeToDelete) {
                            // Borrar la carpeta del episodio
                            const epPath = deleteEpisodeDialog.episodeToDelete.path
                            const epDir = epPath.substring(0, epPath.lastIndexOf("/"))
                            FileHelper.removeDir(epDir)
                            lib.refreshEpisodes()
                        }
                        deleteEpisodeDialog.close()
                    }
                }
                Button {
                    text: qsTr("No, cancelar")
                    onClicked: deleteEpisodeDialog.close()
                }
            }
        }
    }

    // --- Diálogo: Renombrar episodio ---
    Dialog {
        id: renameEpisodeDialog
        title: qsTr("Renombrar episodio")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.NoButton
        width: 380
        palette.window: "#2a2d31"
        palette.windowText: "#ecf0f1"
        palette.text: "#ecf0f1"
        palette.base: "#1a1d21"
        palette.button: "#34383d"
        palette.buttonText: "#ecf0f1"
        palette.toolTipBase: "#1a1d21"
        palette.toolTipText: "#ecf0f1"

        property var episodeToRename: null

        contentItem: Column {
            spacing: 16
            padding: 16

            Label { text: qsTr("Nuevo nombre"); color: root.textPri }
            TextField {
                id: renameEpisodeField
                width: parent.width - 32
                color: root.textPri
                onAccepted: {
                    if (text.trim() !== "" && renameEpisodeDialog.episodeToRename) {
                        lib.renameEpisode(renameEpisodeDialog.episodeToRename, text.trim())
                        renameEpisodeDialog.close()
                    }
                }
            }

            Row {
                spacing: 8
                anchors.right: parent.right
                anchors.rightMargin: 16
                Button {
                    text: qsTr("Renombrar")
                    onClicked: {
                        if (renameEpisodeField.text.trim() !== "" && renameEpisodeDialog.episodeToRename) {
                            lib.renameEpisode(renameEpisodeDialog.episodeToRename, renameEpisodeField.text.trim())
                            renameEpisodeDialog.close()
                        }
                    }
                }
                Button {
                    text: qsTr("Cancelar")
                    onClicked: renameEpisodeDialog.close()
                }
            }
        }
    }
}
