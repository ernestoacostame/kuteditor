import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import KutIcons 1.0

/**
 * PublishDialog.qml — Diálogo de publicación a KutPod.
 *
 * Flujo:
 *  1. Si no logueado → muestra mini-login.
 *  2. Si logueado → permite escoger podcast destino + revisar metadata.
 *  3. Botón "Publicar" ejecuta export-a-temp + upload con barra de progreso.
 *
 * Datos pre-rellenados desde ProjectIO.metadata y ChapterModel.
 */
ApplicationWindow {
    id: pubWin
    title: pubWin.editMode ? qsTr("Editar Episodio") : qsTr("Publicar en KutPod")
    modality: Qt.WindowModal
    flags: Qt.Dialog | Qt.WindowTitleHint | Qt.WindowCloseButtonHint
    visible: false
    
    property bool editMode: false
    property string editEpisodeId: ""
    property var activeLocalPodcast: null
    width: 640
    height: 780
    minimumHeight: 780
    
    

    // Configuración de exportación (heredada de main.qml)
    property int prefSampleRate: 48000
    property real normalizeLUFS: -16.0
    property string transcriptPath: ""

    // Estado interno
    property string _selectedPodcastId: ""
    property string _selectedPodcastName: ""
    property string _tmpExportPath: ""
    property int    _progress: 0
    property string _statusMsg: ""
    property string _errorMsg: ""
    property string _episodeUrl: ""
    property bool   _publishing: false
    // Episodio/temporada detectados desde KutPod (último ep + 1)
    property int _kutpodNextEpisode: -1
    property int _kutpodSeason: -1

    function resetState() {
        _selectedPodcastId = ""
        _selectedPodcastName = ""
        _tmpExportPath = ""
        _progress = 0
        _statusMsg = ""
        _errorMsg = ""
        _episodeUrl = ""
        _publishing = false
        _kutpodNextEpisode = -1
        _kutpodSeason = -1
    }

    function _normalizeName(str) {
        if (!str) return ""
        return String(str)
            .toLowerCase()
            .normalize("NFD")
            .replace(/[\u0300-\u036f]/g, "")
            .replace(/[^a-z0-9\s-]/g, "")
            .replace(/-/g, " ")
            .replace(/\s+/g, " ")
            .trim()
    }

    signal publishWindowOpened()

    function _writeField(objectName, value) {
        const form = pubStack.currentItem
        if (!form) return
        function find(node) {
            if (!node) return null
            if (node.objectName === objectName) return node
            const c = node.children || []
            for (var i = 0; i < c.length; i++) {
                const f = find(c[i])
                if (f) return f
            }
            return null
        }
        const t = find(form)
        if (t) t.text = value
    }

    function _writeNumberField(objectName, value) {
        const form = pubStack.currentItem
        if (!form) return
        function find(node) {
            if (!node) return null
            if (node.objectName === objectName) return node
            const c = node.children || []
            for (var i = 0; i < c.length; i++) {
                const f = find(c[i])
                if (f) return f
            }
            return null
        }
        const t = find(form)
        if (t) t.value = value
    }

    function _writeCheckField(objectName, value) {
        const form = pubStack.currentItem
        if (!form) return
        function find(node) {
            if (!node) return null
            if (node.objectName === objectName) return node
            const c = node.children || []
            for (var i = 0; i < c.length; i++) {
                const f = find(c[i])
                if (f) return f
            }
            return null
        }
        const t = find(form)
        if (t) t.checked = value
    }

    function _writeComboValueField(objectName, value) {
        const form = pubStack.currentItem
        if (!form) return
        function find(node) {
            if (!node) return null
            if (node.objectName === objectName) return node
            const c = node.children || []
            for (var i = 0; i < c.length; i++) {
                const f = find(c[i])
                if (f) return f
            }
            return null
        }
        const t = find(form)
        if (t) {
            var index = -1
            for (var i = 0; i < t.model.length; i++) {
                if (t.model[i].value === value) {
                    index = i
                    break
                }
            }
            if (index !== -1) {
                t.currentIndex = index
            }
        }
    }

    function resetForm() {
        var md = {}
        if (pubWin.editMode && pubWin._pendingPayload) {
            md = {
                title: pubWin._pendingPayload.title || "",
                description: pubWin._pendingPayload.description || "",
                episode: pubWin._pendingPayload.episodeNumber || 0,
                season: pubWin._pendingPayload.seasonNumber || 1,
                author: pubWin._pendingPayload.author || "",
                publishAt: pubWin._pendingPayload.publishAt || "",
                episodeType: pubWin._pendingPayload.episodeType || "full"
            }
        } else {
            md = pubWin.projectMeta()
            if (ProjectIO.metadata && ProjectIO.metadata.episodeType) {
                md.episodeType = ProjectIO.metadata.episodeType
            } else {
                md.episodeType = "full"
            }
        }

        var name = ""
        var projMd = ProjectIO.metadata
        if (projMd && projMd.podcast) {
            name = projMd.podcast
        } else if (ProjectIO.currentDisplayName) {
            var parts = ProjectIO.currentDisplayName.split("/")
            if (parts.length > 1) name = parts[0].trim()
        }
        if (!name && pubWin.activeLocalPodcast) {
            name = pubWin.activeLocalPodcast.name || pubWin.activeLocalPodcast.title || ""
        }
        
        _writeField("podcastField", name)
        _writeField("titleField", md.title)
        _writeField("descField", md.description)
        _writeNumberField("epField", md.episode)
        _writeNumberField("seasonField", md.season)
        _writeComboValueField("epTypeCombo", md.episodeType)
        _writeField("authorField", md.author)
        
        var hasPublishAt = (md.publishAt && md.publishAt !== "")
        _writeCheckField("scheduleCheck", hasPublishAt)
        if (hasPublishAt) {
            _writeField("scheduleField", md.publishAt)
        } else {
            var d = new Date()
            d.setHours(d.getHours() + 1)
            _writeField("scheduleField", Qt.formatDateTime(d, "yyyy-MM-dd hh:mm"))
        }

        if (!pubWin.editMode) {
            pubWin.transcriptPath = ""
        }
    }

    // Timer de reintento: si tras abrir la ventana _selectedPodcastId sigue
    // vacío (porque fetchPodcasts no había respondido aún), volver a intentar.
    Timer {
        id: _resolveRetryTimer
        interval: 500; repeat: true; running: false
        property int _attempts: 0
        onTriggered: {
            if (pubWin._selectedPodcastId !== "" || _attempts >= 10) {
                running = false
                _attempts = 0
                return
            }
            _attempts++
            console.log("[PublishWindow] Reintento resolve #" + _attempts)
            pubWin._resolveFromProject()
        }
    }

    function open() {
        _tmpExportPath = ""
        _progress = 0
        _statusMsg = ""
        _errorMsg = ""
        _episodeUrl = ""
        _publishing = false
        _kutpodNextEpisode = -1
        _kutpodSeason = -1
        _selectedPodcastId = ""
        _selectedPodcastName = ""

        if (pubStack.depth > 1) {
            pubStack.pop(null, StackView.Immediate)
        }
        if (!pubStack.currentItem || pubStack.currentItem.objectName !== "publishFormPage") {
            pubStack.replace(pubStack.currentItem, formPage, StackView.Immediate)
        }
        show()
        raise()

        resetForm()

        if (KutPod.loggedIn) {
            KutPod.fetchPodcasts()
            _resolveFromProject()
            // Iniciar reintento por si los podcasts aún no están cargados
            _resolveRetryTimer._attempts = 0
            _resolveRetryTimer.running = true
        }
        publishWindowOpened()
    }

    function _resolveFromProject() {
        var md = ProjectIO.metadata
        var name = ""
        if (md && md.podcast) {
            name = md.podcast
        } else if (ProjectIO.currentDisplayName) {
            var parts = ProjectIO.currentDisplayName.split("/")
            if (parts.length > 1) name = parts[0].trim()
        }
        if (!name && pubWin.activeLocalPodcast) {
            name = pubWin.activeLocalPodcast.name || pubWin.activeLocalPodcast.title || ""
        }
        console.log("[PublishWindow] _resolveFromProject: name='" + name + "'")
        if (!name) {
            console.log("[PublishWindow] No se encontró nombre de podcast en metadata/displayName/activeLocal")
            return
        }
        var normName = _normalizeName(name)
        var pods = KutPod.podcasts
        console.log("[PublishWindow] Podcasts disponibles: " + (pods ? pods.length : 0))
        if (!pods || pods.length === 0) return
        for (var i = 0; i < pods.length; i++) {
            var p = pods[i]
            var normPName = _normalizeName(p.name || p.title)
            var normSlug = _normalizeName(p.slug)
            console.log("[PublishWindow]   Comparando '" + name + "' / norm='" + normName + "' con podcast: name='" + p.name + "' slug='" + p.slug + "' normPName='" + normPName + "' normSlug='" + normSlug + "'")
            if (normPName === normName || normSlug === normName) {
                _selectedPodcastId   = String(p.id)
                _selectedPodcastName = p.name || p.title || ""
                console.log("[PublishWindow] ✓ Match exacto: id=" + _selectedPodcastId + " name='" + _selectedPodcastName + "'")
                _fetchLastEpisodeFromKutPod()
                _writeField("podcastField", _selectedPodcastName)
                _resolveRetryTimer.running = false
                return
            }
        }
        for (var j = 0; j < pods.length; j++) {
            var p2 = pods[j]
            var normPName2 = _normalizeName(p2.name || p2.title)
            var normSlug2 = _normalizeName(p2.slug)
            if (normPName2.indexOf(normName) !== -1 || normName.indexOf(normPName2) !== -1 ||
                normSlug2.indexOf(normName) !== -1 || normName.indexOf(normSlug2) !== -1) {
                _selectedPodcastId   = String(p2.id)
                _selectedPodcastName = p2.name || p2.title || ""
                console.log("[PublishWindow] ✓ Match parcial: id=" + _selectedPodcastId + " name='" + _selectedPodcastName + "'")
                _fetchLastEpisodeFromKutPod()
                _writeField("podcastField", _selectedPodcastName)
                _resolveRetryTimer.running = false
                return
            }
        }
        console.log("[PublishWindow] ✗ No se encontró match para '" + name + "'")
    }

    // ── Consultar último episodio desde KutPod ─────────────────────
    function _fetchLastEpisodeFromKutPod() {
        if (!KutPod.loggedIn || _selectedPodcastId === "") return
        // Pedir page=1, per_page=1 para obtener solo el último episodio
        KutPod.fetchEpisodes(_selectedPodcastId, 1, 1, "")
    }

    // ── Pre-rellenar metadata desde el proyecto ──────────────────────
    function projectMeta() {
        const m = ProjectIO.metadata || {}
        return {
            title:       m.title       || "",
            description: m.description || "",
            episode:     pubWin._kutpodNextEpisode >= 0
                             ? pubWin._kutpodNextEpisode
                             : (m.episode || 0),
            season:      pubWin._kutpodSeason >= 0
                             ? pubWin._kutpodSeason
                             : (m.season || 1),
            author:      m.podcaster   || "",
            publishAt:   m.publishAt   || ""
        }
    }

    // Re-resolve podcast when the server responds with the podcasts list
    Connections {
        target: KutPod
        function onPodcastsChanged() {
            if (pubWin.visible) {
                pubWin._resolveFromProject()
            }
        }
        function onEpisodesLoaded(podcastId, items, total, page, pages) {
            // Solo procesar si es la respuesta de nuestro podcast
            if (!pubWin.visible) return
            if (podcastId !== pubWin._selectedPodcastId) return
            if (pubWin.editMode) return  // En edición no auto-detectamos

            // Buscar el número máximo de episodio y la temporada
            var maxEp = 0
            var season = 1
            for (var i = 0; i < items.length; i++) {
                var ep = items[i]
                var epNum = Number(ep.number || ep.episode || 0)
                var epSeason = Number(ep.season || 1)
                if (epNum > maxEp) {
                    maxEp = epNum
                    season = epSeason
                }
            }
            // Si total > items.length, el maxEp del server puede ser mayor.
            // Usamos total como fallback si no hay episode numbers.
            if (maxEp === 0 && total > 0) maxEp = total

            pubWin._kutpodNextEpisode = maxEp + 1
            pubWin._kutpodSeason = season

            // Actualizar los SpinBox si el formulario está visible
            var form = pubStack.currentItem
            if (form) {
                pubWin._updateSpinBox(form, "epField", maxEp + 1)
                pubWin._updateSpinBox(form, "seasonField", season)
            }
        }
    }

    function _updateSpinBox(root, objectName, value) {
        function find(node) {
            if (!node) return null
            if (node.objectName === objectName) return node
            var c = node.children || []
            for (var i = 0; i < c.length; i++) {
                var f = find(c[i])
                if (f) return f
            }
            return null
        }
        var sb = find(root)
        if (sb) sb.value = value
    }

    StackView {
        id: pubStack
        anchors.fill: parent
        initialItem: KutPod.loggedIn ? formPage : loginMini
    }

    // ── Mini-login ──────────────────────────────────────────────────
    Component {
        id: loginMini
        Item {
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 24
                spacing: 12

                Label {
                    text: qsTr("Conecta con KutPod para publicar")
                    font.pixelSize: 16; font.bold: true
                    color: palette.text
                }
                Label { text: qsTr("URL"); color: palette.text }
                TextField { id: lu; Layout.fillWidth: true
                    placeholderText: "https://www.tupodcast.com"
                    text: KutPod.baseUrl || "https://" }
                Label { text: qsTr("Usuario"); color: palette.text }
                TextField { id: lus; Layout.fillWidth: true; text: KutPod.currentUser }
                Label { text: qsTr("Contraseña"); color: palette.text }
                TextField { id: lp; Layout.fillWidth: true; echoMode: TextInput.Password }
                Label {
                    id: miniErr; color: "#e74c3c"; wrapMode: Text.WordWrap
                    Layout.fillWidth: true; visible: text !== ""
                }
                Item { Layout.fillHeight: true }
                RowLayout {
                    Layout.fillWidth: true
                    Button { text: qsTr("Cancelar"); onClicked: pubWin.close() }
                    Item { Layout.fillWidth: true }
                    BusyIndicator { running: KutPod.busy; visible: KutPod.busy }
                    Button {
                        text: qsTr("Iniciar sesión")
                        highlighted: true
                        enabled: !KutPod.busy && lu.text.length > 4
                              && lus.text.length > 0 && lp.text.length > 0
                        onClicked: {
                            miniErr.text = ""
                            KutPod.login(lu.text.trim(), lus.text.trim(), lp.text)
                        }
                    }
                }
                Connections {
                    target: KutPod
                    function onLoginSuccess(user) {
                        KutPod.fetchPodcasts()
                        pubStack.replace(pubStack.currentItem, formPage, StackView.Immediate)
                        pubWin.resetForm()
                        pubWin._resolveFromProject()
                    }
                    function onLoginFailed(reason) { miniErr.text = reason }
                }
            }
        }
    }

    // ── Formulario de publicación ───────────────────────────────────
    Component {
        id: formPage
        Item {
            objectName: "publishFormPage"
            property var meta: {
                if (pubWin.editMode && pubWin._pendingPayload) {
                    return {
                        title: pubWin._pendingPayload.title || "",
                        description: pubWin._pendingPayload.description || "",
                        episode: pubWin._pendingPayload.episodeNumber || 0,
                        season: pubWin._pendingPayload.seasonNumber || 1,
                        author: pubWin._pendingPayload.author || "",
                        publishAt: pubWin._pendingPayload.publishAt || ""
                    }
                }
                return pubWin.projectMeta()
            }

            ScrollView {
                anchors.fill: parent
                anchors.margins: 18
                clip: true
                contentWidth: availableWidth

                ColumnLayout {
                    id: pubCol
                    width: parent.width
                    spacing: 12

                    // Selector de podcast — usa el nombre del metadata del proyecto
                    // (mismo enfoque que ExportDialog: lee ProjectIO.metadata.podcast)
                    Label { text: qsTr("Podcast destino"); color: palette.text; font.bold: true }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        TextField {
                            id: podcastField
                            objectName: "podcastField"
                            Layout.fillWidth: true
                            readOnly: true
                            color: palette.text
                            text: {
                                // Leer directamente del metadata del proyecto, igual que Export
                                var md = ProjectIO.metadata
                                if (md && md.podcast) return md.podcast
                                // Fallback: nombre del displayName
                                if (ProjectIO.currentDisplayName) {
                                    var parts = ProjectIO.currentDisplayName.split("/")
                                    if (parts.length > 1) return parts[0].trim()
                                }
                                // Fallback: podcast seleccionado en la biblioteca local
                                if (pubWin.activeLocalPodcast) {
                                    return pubWin.activeLocalPodcast.name || pubWin.activeLocalPodcast.title || ""
                                }
                                return ""
                            }
                        }
                        Button {
                            text: qsTr("Cambiar…")
                            onClicked: podcastPopup.open()
                        }
                    }

                    // Popup para cambiar podcast manualmente
                    Popup {
                        id: podcastPopup
                        modal: true
                        anchors.centerIn: parent
                        width: 400
                        height: Math.min(300, podcastListView.contentHeight + 60)
                        padding: 12
                        ColumnLayout {
                            anchors.fill: parent
                            Label { text: qsTr("Seleccionar podcast"); font.bold: true; color: palette.text }
                            ListView {
                                id: podcastListView
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                model: KutPod.podcasts
                                delegate: ItemDelegate {
                                    width: podcastListView.width
                                    text: modelData.name || modelData.title || ""
                                    highlighted: String(modelData.id) === pubWin._selectedPodcastId
                                    onClicked: {
                                        pubWin._selectedPodcastId   = String(modelData.id)
                                        pubWin._selectedPodcastName = modelData.name || modelData.title || ""
                                        podcastField.text = pubWin._selectedPodcastName
                                        podcastPopup.close()
                                        pubWin._fetchLastEpisodeFromKutPod()
                                    }
                                }
                            }
                        }
                    }

                    // Resolver el podcast ID al abrir la ventana
                    Connections {
                        target: pubWin
                        function onVisibleChanged() {
                            if (pubWin.visible) pubCol.resolveId()
                        }
                    }
                    Connections {
                        target: KutPod
                        function onPodcastsChanged() { pubCol.resolveId() }
                    }
                    Timer {
                        id: resolveTimer
                        interval: 200; repeat: true
                        running: pubWin.visible && pubWin._selectedPodcastId === ""
                        onTriggered: pubCol.resolveId()
                    }
                    function resolveId() {
                        var name = podcastField.text.trim()
                        if (!name) return
                        var normName = pubWin._normalizeName(name)
                        var pods = KutPod.podcasts
                        if (!pods || pods.length === 0) return
                        for (var i = 0; i < pods.length; i++) {
                            var p = pods[i]
                            var normPName = pubWin._normalizeName(p.name || p.title)
                            var normSlug = pubWin._normalizeName(p.slug)
                            if (normPName === normName || normSlug === normName) {
                                pubWin._selectedPodcastId   = String(p.id)
                                pubWin._selectedPodcastName = p.name || p.title || ""
                                return
                            }
                        }
                        for (var j = 0; j < pods.length; j++) {
                            var p2 = pods[j]
                            var normPName2 = pubWin._normalizeName(p2.name || p2.title)
                            var normSlug2 = pubWin._normalizeName(p2.slug)
                            if (normPName2.indexOf(normName) !== -1 || normName.indexOf(normPName2) !== -1 ||
                                normSlug2.indexOf(normName) !== -1 || normName.indexOf(normSlug2) !== -1) {
                                pubWin._selectedPodcastId   = String(p2.id)
                                pubWin._selectedPodcastName = p2.name || p2.title || ""
                                return
                            }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: palette.mid }

                    // Título
                    Label { text: qsTr("Título"); color: palette.text }
                    TextField {
                        id: titleField
                        objectName: "titleField"
                        Layout.fillWidth: true
                        text: meta.title
                    }

                    // Descripción
                    Label { text: qsTr("Descripción / notas"); color: palette.text }
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 100
                        background: Rectangle {
                            color: "#1f2226"
                            radius: 4
                            border.color: "#3d4146"
                        }
                        TextArea {
                            id: descField
                            objectName: "descField"
                            text: meta.description
                            wrapMode: TextArea.Wrap
                            color: palette.text
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 4
                        columnSpacing: 12
                        rowSpacing: 6

                        Label { text: qsTr("Episodio"); color: palette.text }
                        SpinBox {
                            id: epField
                            objectName: "epField"
                            Layout.fillWidth: true
                            from: 0; to: 99999
                            value: meta.episode
                            editable: true
                            enabled: true
                        }
                        Label { text: qsTr("Temporada"); color: palette.text }
                        SpinBox {
                            id: seasonField
                            objectName: "seasonField"
                            Layout.fillWidth: true
                            from: 0; to: 99
                            value: meta.season
                            editable: true
                            enabled: true
                        }
                    }

                    Label { text: qsTr("Tipo de episodio"); color: palette.text }
                    ComboBox {
                        id: epTypeCombo
                        objectName: "epTypeCombo"
                        Layout.fillWidth: true
                        textRole: "text"
                        valueRole: "value"
                        model: [
                            { text: qsTr("Full (Completo)"), value: "full" },
                            { text: qsTr("Trailer"), value: "trailer" },
                            { text: qsTr("Bonus (Extra)"), value: "bonus" }
                        ]
                        Component.onCompleted: {
                            var targetType = "full"
                            if (pubWin.editMode && pubWin._pendingPayload && pubWin._pendingPayload.episodeType) {
                                targetType = pubWin._pendingPayload.episodeType
                            } else if (ProjectIO.metadata && ProjectIO.metadata.episodeType) {
                                targetType = ProjectIO.metadata.episodeType
                            }
                            for (var i = 0; i < model.length; i++) {
                                if (model[i].value === targetType) {
                                    currentIndex = i
                                    break
                                }
                            }
                        }
                    }

                    Label { text: qsTr("Autor / podcaster"); color: palette.text }
                    TextField {
                        id: authorField
                        objectName: "authorField"
                        Layout.fillWidth: true
                        text: meta.author
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: palette.mid }

                    CheckBox {
                        id: scheduleCheck
                        objectName: "scheduleCheck"
                        text: qsTr("Programar publicación")
                        checked: meta.publishAt && meta.publishAt !== ""
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: scheduleCheck.checked
                        spacing: 12

                        Label {
                            text: qsTr("Fecha y hora")
                            color: palette.text
                        }
                        TextField {
                            id: scheduleField
                            objectName: "scheduleField"
                            Layout.fillWidth: true
                            placeholderText: "YYYY-MM-DD HH:MM"
                            text: meta.publishAt ? meta.publishAt : (function() {
                                var d = new Date();
                                d.setHours(d.getHours() + 1);
                                // Mostrar en formato estándar legible
                                return Qt.formatDateTime(d, "yyyy-MM-dd hh:mm");
                            })()
                        }
                        // Mostrar la zona horaria detectada del sistema
                        Label {
                            text: {
                                var d = new Date()
                                var offset = -d.getTimezoneOffset()
                                var sign = offset >= 0 ? "+" : "-"
                                var h = Math.floor(Math.abs(offset) / 60)
                                var m = Math.abs(offset) % 60
                                return "UTC" + sign + (h < 10 ? "0" : "") + h + ":" + (m < 10 ? "0" : "") + m
                            }
                            color: palette.placeholderText
                            font.pixelSize: 11
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: palette.mid }

                    // Resumen de extras
                    Label {
                        text: qsTr("Adjuntos detectados")
                        color: palette.text; font.bold: true
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        RowLayout {
                            Text {
                                text: ChapterModel.count > 0 ? "✓" : "—"
                                color: ChapterModel.count > 0 ? "#2ecc71" : palette.placeholderText
                                font.bold: true; font.pixelSize: 14
                            }
                            Label {
                                color: palette.text
                                text: ChapterModel.count > 0
                                    ? qsTr("%1 capítulos (con imágenes locales se suben al servidor)").arg(ChapterModel.count)
                                    : qsTr("Sin capítulos")
                            }
                        }
                        RowLayout {
                            Text {
                                text: pubWin.transcriptPath !== "" ? "✓" : "—"
                                color: pubWin.transcriptPath !== "" ? "#2ecc71" : palette.placeholderText
                                font.bold: true; font.pixelSize: 14
                            }
                            Label {
                                color: palette.text
                                text: pubWin.transcriptPath !== ""
                                    ? qsTr("Transcripción adjunta: %1").arg(FileHelper.fileName(pubWin.transcriptPath))
                                    : qsTr("Sin transcripción")
                            }
                        }
                    }
                    Button {
                        text: pubWin.transcriptPath === ""
                            ? qsTr("Adjuntar transcripción…")
                            : qsTr("Cambiar transcripción…")
                        onClicked: transcriptPicker.open()
                    }
                    FileDialog {
                        id: transcriptPicker
                        nameFilters: ["Subtítulos (*.srt *.vtt)"]
                        onAccepted: {
                            var p = selectedFile.toString()
                            if (p.startsWith("file://")) p = p.substring(7)
                            pubWin.transcriptPath = p
                        }
                    }
                }
            }
        }
    }

    // ── Pantalla de progreso ────────────────────────────────────────
    Component {
        id: progressPage
        Item {
            ColumnLayout {
                anchors.centerIn: parent
                width: 460
                spacing: 14

                Label {
                    text: pubWin._publishing
                        ? qsTr("Publicando episodio…")
                        : (pubWin._errorMsg !== ""
                            ? qsTr("Error al publicar")
                            : qsTr("Publicado correctamente"))
                    color: pubWin._errorMsg !== "" ? "#e74c3c" : palette.text
                    font.pixelSize: 18; font.bold: true
                    Layout.alignment: Qt.AlignHCenter
                }
                Label {
                    text: pubWin._statusMsg
                    color: palette.placeholderText
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                }
                ProgressBar {
                    Layout.fillWidth: true
                    from: 0; to: 100
                    value: pubWin._progress
                    visible: pubWin._publishing
                }
                Label {
                    visible: pubWin._errorMsg !== ""
                    text: pubWin._errorMsg
                    color: "#e74c3c"
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Label {
                    visible: pubWin._episodeUrl !== "" && !pubWin._publishing
                    text: qsTr("Disponible en: %1").arg(pubWin._episodeUrl)
                    color: palette.text
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                }
                Item { Layout.preferredHeight: 8 }
                Button {
                    Layout.alignment: Qt.AlignHCenter
                    text: pubWin._publishing ? qsTr("Cancelar") : qsTr("Cerrar")
                    onClicked: pubWin.close()
                }
            }
        }
    }

    // ── Footer con acción principal (solo en formPage) ───────────────
    footer: ToolBar {
        id: pubFooter
        implicitHeight: 56
        visible: pubStack.currentItem && pubStack.currentItem.objectName === "publishFormPage"
              && !pubWin._publishing
              && pubWin._errorMsg === ""
              && pubWin._episodeUrl === ""
              && KutPod.loggedIn
        
        RowLayout {
            anchors.fill: parent
            anchors.margins: 8
            
            Item { Layout.fillWidth: true } // spacer
            
            Button {
                text: qsTr("Cancelar")
                onClicked: pubWin.close()
            }
            Button {
                text: pubWin.editMode ? qsTr("Guardar Cambios") : qsTr("Publicar")
                highlighted: true
                enabled: true
                onClicked: pubWin._startPublish()
            }
        }
    }

    // ── Acción: exportar a temp y subir ─────────────────────────────
    // Datos del formulario capturados antes de lanzar la exportación,
    // para usarlos cuando exportFinished llegue.
    property var _pendingPayload: null

    function _startPublish() {
        const form = pubStack.currentItem
        if (!form) return

        // Resolución tardía: si _selectedPodcastId sigue vacío,
        // intentar resolver ahora desde el nombre del campo.
        if (_selectedPodcastId === "") {
            _resolveFromProject()
        }
        if (_selectedPodcastId === "") {
            // Último intento: buscar por nombre en el campo de podcast
            var pName = _readField("podcastField").trim()
            var normPName = _normalizeName(pName)
            var pods = KutPod.podcasts
            if (pods && pName) {
                for (var i = 0; i < pods.length; i++) {
                    var p = pods[i]
                    var normN = _normalizeName(p.name || p.title)
                    var normS = _normalizeName(p.slug)
                    if (normN === normPName || normS === normPName ||
                        normN.indexOf(normPName) !== -1 || normPName.indexOf(normN) !== -1 ||
                        normS.indexOf(normPName) !== -1 || normPName.indexOf(normS) !== -1) {
                        _selectedPodcastId = String(p.id)
                        _selectedPodcastName = p.name || p.title || ""
                        break
                    }
                }
            }
        }
        if (_selectedPodcastId === "") {
            _errorMsg = qsTr("Selecciona un podcast destino primero.")
            pubStack.replace(progressPage)
            return
        }
        const titleVal   = _readField("titleField")
        const descVal    = _readField("descField")
        const epVal      = _readNumberField("epField")
        const seasonVal  = _readNumberField("seasonField")
        const epTypeVal  = _readComboValueField("epTypeCombo")
        const authorVal  = _readField("authorField")
        const scheduleRaw = _readCheckField("scheduleCheck") ? _readField("scheduleField") : ""
        // Normalizar a ISO 8601 con offset de timezone del sistema.
        // Así el servidor sabe exactamente qué hora local quiso el usuario.
        // Ejemplo: "2026-05-21 00:30" → "2026-05-21T00:30:00-05:00"
        var scheduleVal = ""
        if (scheduleRaw.trim() !== "") {
            var s = scheduleRaw.trim().replace(" ", "T")
            if (s.length === 16) s += ":00"  // añadir segundos si faltan
            // Adjuntar offset de timezone del sistema
            var now = new Date()
            var offsetMin = -now.getTimezoneOffset()  // JS devuelve inverso
            var sign = offsetMin >= 0 ? "+" : "-"
            var oh = Math.floor(Math.abs(offsetMin) / 60)
            var om = Math.abs(offsetMin) % 60
            var tz = sign + (oh < 10 ? "0" : "") + oh + ":" + (om < 10 ? "0" : "") + om
            scheduleVal = s + tz
        }

        if (!titleVal || titleVal.trim() === "") {
            _errorMsg = qsTr("Falta el título del episodio.")
            pubStack.replace(progressPage)
            return
        }

        _publishing = true
        _progress = 0
        _statusMsg = qsTr("Exportando MP3 con capítulos…")
        pubStack.replace(progressPage)

        // Ruta de export temporal
        const home = FileHelper.homeDir()
        const tmpDir = home + "/.cache/kut/publish"
        FileHelper.ensureDir(tmpDir)
        const safeTitle = titleVal.replace(/[\/\\:*?"<>|]/g, "_")
        _tmpExportPath = tmpDir + "/" + safeTitle + ".mp3"

        // Metadata para el export (igual que ExportDialog)
        const metadata = {
            title:       titleVal,
            artist:      authorVal,
            album:       _selectedPodcastName,
            comment:     descVal,
            track:       epVal,
            disc:        seasonVal,
            podcaster:   authorVal,
            description: descVal,
            episode:     epVal,
            season:      seasonVal
        }

        const opts = {
            format:        "mp3",
            sampleRate:    pubWin.prefSampleRate,
            normalizeLUFS: pubWin.normalizeLUFS,
            metadata:      metadata
        }

        // Capítulos con paths absolutos para img
        const chapters = []
        const chaptersJson = ChapterModel.toJson()
        for (let i = 0; i < chaptersJson.length; i++) {
            const c = chaptersJson[i]
            const localImg = ChapterModel.chapterArtwork(i)
            if (localImg && localImg !== "")
                c.img = localImg
            chapters.push(c)
        }

        // Guardar el payload para usarlo en onExportFinished
        _pendingPayload = {
            title:          titleVal,
            description:    descVal,
            author:         authorVal,
            episodeNumber:  epVal,
            seasonNumber:   seasonVal,
            episodeType:    epTypeVal,
            publishAt:      scheduleVal,
            audioPath:      _tmpExportPath,
            coverPath:      ProjectIO.metadata && ProjectIO.metadata.coverPath
                              ? ProjectIO.metadata.coverPath : "",
            transcriptPath: pubWin.transcriptPath,
            chapters:       chapters
        }

        if (pubWin.editMode && TrackModel.count === 0) {
            // Edit mode without audio changes
            pubWin._pendingPayload.audioPath = ""
            pubWin._statusMsg = qsTr("Guardando metadatos en KutPod…")
            KutPod.updateEpisode(pubWin._selectedPodcastId, pubWin.editEpisodeId, pubWin._pendingPayload)
            pubWin._pendingPayload = null
        } else {
            const ok = ExportManager.exportWithOptions(_tmpExportPath, opts)
            if (!ok) {
                _publishing = false
                _pendingPayload = null
                _errorMsg = qsTr("No se pudo exportar: %1").arg(ExportManager.lastError())
                return
            }
        }
    }

    function _readField(objectName) {
        // Recorre el árbol del formPage para encontrar el TextField/TextArea por objectName
        const form = pubStack.currentItem
        if (!form) return ""
        // Buscar recursivamente
        function find(node) {
            if (!node) return null
            if (node.objectName === objectName) return node
            // Si no tiene objectName, intentar por id directamente con findChild no existe.
            // Recurrir a children + visibleChildren.
            const c = node.children || []
            for (let i = 0; i < c.length; i++) {
                const f = find(c[i])
                if (f) return f
            }
            return null
        }
        const t = find(form)
        return t ? t.text : ""
    }

    function _readComboValueField(objectName) {
        const form = pubStack.currentItem
        if (!form) return "full"
        function find(node) {
            if (!node) return null
            if (node.objectName === objectName) return node
            const c = node.children || []
            for (let i = 0; i < c.length; i++) {
                const f = find(c[i])
                if (f) return f
            }
            return null
        }
        const t = find(form)
        return t ? t.currentValue : "full"
    }

    function _readNumberField(objectName) {
        const form = pubStack.currentItem
        if (!form) return 0
        function find(node) {
            if (!node) return null
            if (node.objectName === objectName) return node
            const c = node.children || []
            for (let i = 0; i < c.length; i++) {
                const f = find(c[i])
                if (f) return f
            }
            return null
        }
        const t = find(form)
        return t ? t.value : 0
    }

    function _readCheckField(objectName) {
        const form = pubStack.currentItem
        if (!form) return false
        function find(node) {
            if (!node) return null
            if (node.objectName === objectName) return node
            const c = node.children || []
            for (let i = 0; i < c.length; i++) {
                const f = find(c[i])
                if (f) return f
            }
            return null
        }
        const t = find(form)
        return t ? t.checked : false
    }

    // ── Conexiones del ExportManager (esperar fin de export) ─────────
    Connections {
        target: ExportManager
        function onExportFinished(success, filePath, errorMessage) {
            if (!pubWin._publishing || !pubWin._pendingPayload) return
            if (!success) {
                pubWin._publishing = false
                pubWin._errorMsg = qsTr("Error al exportar: %1").arg(errorMessage)
                pubWin._pendingPayload = null
                return
            }
            pubWin._statusMsg = qsTr("Subiendo a KutPod…")
            if (pubWin.editMode) {
                KutPod.updateEpisode(pubWin._selectedPodcastId, pubWin.editEpisodeId, pubWin._pendingPayload)
            } else {
                KutPod.publishEpisode(pubWin._selectedPodcastId, pubWin._pendingPayload)
            }
            pubWin._pendingPayload = null
        }
    }

    // ── Conexiones del cliente ──────────────────────────────────────
    Connections {
        target: KutPod
        function onPublishProgress(percent) { pubWin._progress = percent }
        function onPublishFinished(success, url, err) {
            if (pubWin.editMode) return;
            pubWin._publishing = false
            if (success) {
                pubWin._episodeUrl = url
                pubWin._statusMsg  = qsTr("Episodio publicado.")
            } else {
                pubWin._errorMsg = err
            }
        }
        function onEpisodeUpdateFinished(success, url, err) {
            if (!pubWin.editMode) return;
            pubWin._publishing = false
            if (success) {
                pubWin._episodeUrl = url
                pubWin._statusMsg  = qsTr("Episodio actualizado.")
            } else {
                pubWin._errorMsg = err
            }
        }
    }
}
