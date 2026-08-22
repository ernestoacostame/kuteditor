import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import KutIcons 1.0

/**
 * OnlineLibrary.qml — Vista de la biblioteca remota (KutPod).
 *
 * Estados:
 *  1. No logueado: formulario URL/usuario/contraseña.
 *  2. Logueado sin podcast seleccionado: grid de podcasts del usuario.
 *  3. Podcast abierto: lista paginada de episodios con buscador.
 *
 * No descarga proyectos; los episodios remotos son solo lectura. La acción
 * importante es "Publicar desde el editor" (ver main.qml → PublishDialog).
 */
Rectangle {
    id: olib
    color: palette.window

    signal skipLibrary()
    signal backToLocal()

    property var currentPodcast: null
    property var episodesData: ({ items: [], page: 1, pages: 1, total: 0 })
    property string lastQuery: ""
    property string loginErrorText: ""
    property string episodesErrorText: ""

    function _resetEpisodes() {
        episodesData = { items: [], page: 1, pages: 1, total: 0 }
    }

    function formatDuration(secs) {
        if (!secs) return ""
        var h = Math.floor(secs / 3600)
        var m = Math.floor((secs % 3600) / 60)
        var s = Math.floor(secs % 60)
        if (h > 0) {
            return h + ":" + (m < 10 ? "0" : "") + m + ":" + (s < 10 ? "0" : "") + s
        }
        return m + ":" + (s < 10 ? "0" : "") + s
    }

    Connections {
        target: KutPod
        function onLoginSuccess(user) { 
            olibStack.replace(podcastsPage)
            KutPod.fetchPodcasts() 
        }
        function onLoginFailed(reason) { olib.loginErrorText = reason }
        function onPodcastsLoaded(items) { /* binding ya actualiza */ }
        function onEpisodesLoaded(podcastId, items, total, page, pages) {
            if (!olib.currentPodcast || podcastId !== String(olib.currentPodcast.id)) return
            olib.episodesData = { items: items, page: page, pages: pages, total: total }
        }
        function onEpisodesFailed(podcastId, reason) {
            olib.episodesErrorText = reason
        }
    }

    Component.onCompleted: {
        if (KutPod.loggedIn) KutPod.fetchPodcasts()
    }

    StackView {
        id: olibStack
        anchors.fill: parent
        initialItem: KutPod.loggedIn ? podcastsPage : loginPage
    }

    // ─── Página: Login ─────────────────────────────────────────────
    Component {
        id: loginPage
        Item {
            ColumnLayout {
                anchors.centerIn: parent
                width: 360
                spacing: 14

                Label {
                    text: qsTr("Conectar con KutPod")
                    color: root.textPri
                    font.pixelSize: 22
                    font.bold: true
                    Layout.alignment: Qt.AlignHCenter
                }
                Label {
                    text: qsTr("Inicia sesión en tu instancia para publicar episodios.")
                    color: root.textSec
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                }

                Item { Layout.preferredHeight: 8 }

                Label { text: qsTr("URL de la plataforma"); color: root.textPri }
                TextField {
                    id: urlField
                    Layout.fillWidth: true
                    placeholderText: "https://www.tupodcast.com"
                    placeholderTextColor: root.textSec
                    text: KutPod.baseUrl !== "" ? KutPod.baseUrl : "https://"
                    color: root.textPri
                }

                Label { text: qsTr("Usuario o email"); color: root.textPri }
                TextField {
                    id: userField
                    Layout.fillWidth: true
                    placeholderText: qsTr("tu_usuario")
                    placeholderTextColor: root.textSec
                    text: KutPod.currentUser
                    color: root.textPri
                }

                Label { text: qsTr("Contraseña"); color: root.textPri }
                TextField {
                    id: passField
                    Layout.fillWidth: true
                    echoMode: TextInput.Password
                    placeholderText: "••••••••"
                    placeholderTextColor: root.textSec
                    color: root.textPri
                    onAccepted: loginBtn.clicked()
                }

                Label {
                    id: loginError
                    text: olib.loginErrorText
                    Layout.fillWidth: true
                    color: "#e74c3c"
                    wrapMode: Text.WordWrap
                    visible: text !== ""
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Button {
                        text: qsTr("← Volver")
                        onClicked: olib.backToLocal()
                    }
                    Item { Layout.fillWidth: true }
                    BusyIndicator {
                        running: KutPod.busy
                        visible: KutPod.busy
                        implicitWidth: 24; implicitHeight: 24
                    }
                    Button {
                        id: loginBtn
                        text: qsTr("Iniciar sesión")
                        highlighted: true
                        enabled: !KutPod.busy
                              && urlField.text.length > 4
                              && userField.text.length > 0
                              && passField.text.length > 0
                        onClicked: {
                            olib.loginErrorText = ""
                            KutPod.login(urlField.text.trim(),
                                         userField.text.trim(),
                                         passField.text)
                        }
                    }
                }
            }
        }
    }

    // ─── Página: Grid de podcasts ──────────────────────────────────
    Component {
        id: podcastsPage
        Item {
            ScrollView {
                anchors.fill: parent
                anchors.margins: 20
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                Column {
                    width: olib.width - 72
                    spacing: 20

                    RowLayout {
                        width: parent.width
                        Label {
                            text: qsTr("Podcasts en KutPod")
                            color: root.textPri
                            font.pixelSize: 24
                            font.bold: true
                            Layout.fillWidth: true
                        }
                        Label {
                            text: KutPod.currentUser + "  ·  " + KutPod.baseUrl
                            color: root.textSec
                            font.pixelSize: 11
                        }
                        Button {
                            text: qsTr("Refrescar")
                            onClicked: KutPod.fetchPodcasts()
                            enabled: !KutPod.busy
                        }
                        Button {
                            text: qsTr("Cerrar sesión")
                            onClicked: { KutPod.logout(); olibStack.replace(loginPage) }
                        }
                    }

                    BusyIndicator {
                        running: KutPod.busy && KutPod.podcasts.length === 0
                        visible: running
                    }

                    Flow {
                        width: parent.width
                        spacing: 16
                        Repeater {
                            model: KutPod.podcasts
                            delegate: Rectangle {
                                required property var modelData
                                width: 200; height: 240
                                radius: 8
                                color: ph.hovered ? palette.midlight : palette.base
                                border.color: ph.hovered ? palette.highlight : palette.mid
                                border.width: 1
                                HoverHandler { id: ph }

                                Column {
                                    anchors.fill: parent
                                    anchors.margins: 10
                                    spacing: 8

                                    Rectangle {
                                        width: parent.width
                                        height: 140
                                        radius: 6
                                        color: palette.window
                                        clip: true
                                        Image {
                                            anchors.fill: parent
                                            source: modelData.cover || modelData.cover_url || ""
                                            fillMode: Image.PreserveAspectCrop
                                            visible: source != ""
                                        }
                                        Label {
                                            anchors.centerIn: parent
                                            text: "🌐"
                                            font.pixelSize: 40
                                            visible: !(modelData.cover || modelData.cover_url)
                                            color: root.textPri
                                        }
                                    }
                                    Label {
                                        text: modelData.name || modelData.title || ""
                                        color: root.textPri
                                        font.pixelSize: 14
                                        font.bold: true
                                        elide: Text.ElideRight
                                        width: parent.width
                                    }
                                    Label {
                                        text: modelData.author || ""
                                        color: root.textSec
                                        font.pixelSize: 11
                                        elide: Text.ElideRight
                                        width: parent.width
                                    }
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        olib.currentPodcast = modelData
                                        olib._resetEpisodes()
                                        KutPod.fetchEpisodes(String(modelData.id), 1, 10, "")
                                        olibStack.push(episodesPage)
                                    }
                                }
                            }
                        }
                    }

                    Label {
                        visible: !KutPod.busy && KutPod.podcasts.length === 0
                        text: qsTr("No tienes podcasts en esta instancia.\nCrea uno desde el panel web de KutPod.")
                        color: root.textSec
                        font.pixelSize: 14
                        wrapMode: Text.WordWrap
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        topPadding: 40
                    }
                }
            }
        }
    }

    // ─── Página: Episodios de un podcast (paginado + buscador) ─────
    Component {
        id: episodesPage
        Item {
            // Debounce search
            Timer {
                id: searchDebounce
                interval: 350; repeat: false
                onTriggered: {
                    olib.lastQuery = searchField.text.trim()
                    KutPod.fetchEpisodes(String(olib.currentPodcast.id), 1, 10, olib.lastQuery)
                }
            }

            ScrollView {
                anchors.fill: parent
                anchors.margins: 20
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                Column {
                    width: olib.width - 72
                    spacing: 16

                    RowLayout {
                        width: parent.width
                        Button {
                            text: "← " + qsTr("Podcasts")
                            onClicked: { olib.currentPodcast = null; olibStack.pop() }
                        }
                        Label {
                            text: olib.currentPodcast
                                ? (olib.currentPodcast.name || olib.currentPodcast.title || "")
                                : ""
                            color: root.textPri
                            font.pixelSize: 20
                            font.bold: true
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                    }

                    // Buscador
                    RowLayout {
                        width: parent.width
                        spacing: 8
                        TextField {
                            id: searchField
                            Layout.fillWidth: true
                            placeholderText: qsTr("Buscar episodios…")
                            color: root.textPri
                            placeholderTextColor: root.textSec
                            onTextChanged: searchDebounce.restart()
                        }
                        BusyIndicator {
                            running: KutPod.busy
                            visible: KutPod.busy
                            implicitWidth: 22; implicitHeight: 22
                        }
                        Label {
                            text: qsTr("%1 resultados").arg(olib.episodesData.total)
                            color: root.textSec
                            font.pixelSize: 11
                            visible: olib.episodesData.total > 0
                        }
                    }

                    Label {
                        id: episodesError
                        text: olib.episodesErrorText
                        color: "#e74c3c"
                        visible: text !== ""
                        wrapMode: Text.WordWrap
                        width: parent.width
                    }

                    // Lista
                    Column {
                        width: parent.width
                        spacing: 6
                        Repeater {
                            model: olib.episodesData.items
                            delegate: Rectangle {
                                required property var modelData
                                width: parent.width
                                height: 56
                                radius: 6
                                color: epHv.hovered ? palette.midlight : palette.base
                                border.color: epHv.hovered ? palette.highlight : palette.mid
                                border.width: 1
                                HoverHandler { id: epHv }
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 12
                                    anchors.rightMargin: 12
                                    spacing: 12
                                    Text {
                                        text: Icons.microphone
                                        color: root.textPri
                                        font.family: "Material Symbols Outlined"
                                        font.pixelSize: 22
                                        opacity: 0.85
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 1
                                        Label {
                                            text: modelData.title || ""
                                            color: root.textPri
                                            font.pixelSize: 13
                                            font.bold: true
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }
                                        Label {
                                            text: {
                                                var parts = []
                                                if (modelData.season)  parts.push("S" + modelData.season)
                                                if (modelData.episode) parts.push("E" + modelData.episode)
                                                if (modelData.published_at) parts.push(modelData.published_at.substring(0,10))
                                                if (modelData.status === "draft") parts.push(qsTr("borrador"))
                                                return parts.join(" · ")
                                            }
                                            color: root.textSec
                                            font.pixelSize: 10
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }
                                    }
                                    Label {
                                        text: olib.formatDuration(modelData.duration)
                                        color: root.textSec
                                        font.pixelSize: 11
                                        visible: text !== ""
                                    }
                                    Button {
                                        text: qsTr("Editar")
                                        flat: true
                                        onClicked: {
                                            publishWindowLoader.item.editMode = true
                                            publishWindowLoader.item.editEpisodeId = String(modelData.id)
                                            publishWindowLoader.item._selectedPodcastId = String(olib.currentPodcast.id)
                                            publishWindowLoader.item._selectedPodcastName = olib.currentPodcast.title || ""
                                            // Pre-fill form by hijacking the projectMeta context, or creating a temp payload
                                            publishWindowLoader.item._pendingPayload = {
                                                title: modelData.title || "",
                                                description: modelData.description || modelData.notes_md || "",
                                                author: modelData.author || "",
                                                episodeNumber: modelData.number || modelData.episode || 0,
                                                seasonNumber: modelData.season || 1,
                                            }
                                            // The publishWindow will read these if we expose them, or we can just force the UI
                                            // Actually, the PublishWindow uses _readField which reads from the text fields.
                                            // Let's pass the meta object.
                                            publishWindowLoader.item.open()
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Label {
                        visible: !KutPod.busy && olib.episodesData.items.length === 0
                        text: olib.lastQuery !== ""
                            ? qsTr("Sin resultados para «%1».").arg(olib.lastQuery)
                            : qsTr("Este podcast no tiene episodios todavía.")
                        color: root.textSec
                        wrapMode: Text.WordWrap
                        width: parent.width
                        topPadding: 20
                        horizontalAlignment: Text.AlignHCenter
                    }

                    // Paginación
                    RowLayout {
                        width: parent.width
                        visible: olib.episodesData.pages > 1
                        Button {
                            text: "‹ " + qsTr("Anterior")
                            enabled: olib.episodesData.page > 1 && !KutPod.busy
                            onClicked: KutPod.fetchEpisodes(
                                String(olib.currentPodcast.id),
                                olib.episodesData.page - 1, 10, olib.lastQuery)
                        }
                        Item { Layout.fillWidth: true }
                        Label {
                            text: qsTr("Página %1 de %2")
                                .arg(olib.episodesData.page)
                                .arg(olib.episodesData.pages)
                            color: root.textPri
                        }
                        Item { Layout.fillWidth: true }
                        Button {
                            text: qsTr("Siguiente") + " ›"
                            enabled: olib.episodesData.page < olib.episodesData.pages && !KutPod.busy
                            onClicked: KutPod.fetchEpisodes(
                                String(olib.currentPodcast.id),
                                olib.episodesData.page + 1, 10, olib.lastQuery)
                        }
                    }
                }
            }
        }
    }
}
