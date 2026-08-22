import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Window {
    id: root
    title: qsTr("Exportar episodio")
    width: 560; height: 680
    minimumWidth: 480; minimumHeight: 500
    flags: Qt.Window
    color: "#22252a"

    function open() {
        _resetState()
        visible = true; raise()
    }
    function close() { visible = false }
    function reject() { if (!isExporting) close() }

    property int defaultSampleRate: 48000
    property int defaultBitDepth:   24
    property string lastExportPath: ""
    property var activeLocalPodcast: null
    property color panelBg:  "#2a2d31"
    property color panelBg2: "#34383d"
    property color textPri:  "#ecf0f1"
    property color textSec:  "#95a5a6"
    property color divider:  "#3c4146"
    property color accent:   "#e74c3c"

    property bool isExporting: false
    property string exportFilePath: ""
    property int exportProgressValue: 0
    property string exportStage: ""
    property bool exportDone: false
    property bool exportError: false
    property string exportErrorMsg: ""

    function _resetState() {
        isExporting = false; exportDone = false; exportError = false
        exportProgressValue = 0; exportStage = ""
        contentCol.visible = true; progressCol.visible = false
        elapsedTimer.seconds = 0
        const md = ProjectIO.metadata || {}
        metaTitle.text       = md.title       !== undefined ? md.title       : ""
        metaPodcaster.text   = (md.podcaster !== undefined && md.podcaster !== "") ? md.podcaster : (
            root.activeLocalPodcast ? (root.activeLocalPodcast.author || "") : ""
        )
        metaPodcast.text     = (md.podcast !== undefined && md.podcast !== "") ? md.podcast : (
            root.activeLocalPodcast ? (root.activeLocalPodcast.name || root.activeLocalPodcast.title || "") : ""
        )
        metaDescription.text = md.description !== undefined ? md.description : ""
        metaEpisode.value    = md.episode     !== undefined ? Number(md.episode) : 0
        metaSeason.value     = md.season      !== undefined ? Number(md.season)  : 0
        coverPreview.path    = md.coverPath   !== undefined ? md.coverPath   : ""
    }

    Connections {
        target: ExportManager
        function onExportProgress(p) {
            root.exportProgressValue = p
            if (p < 20) root.exportStage = qsTr("Mezclando pistas…")
            else if (p < 70) root.exportStage = qsTr("Procesando audio…")
            else if (p < 80) root.exportStage = qsTr("Normalizando audio…")
            else if (p < 100) root.exportStage = qsTr("Escribiendo archivo…")
            else root.exportStage = qsTr("¡Exportación completada!")
        }
        function onExportFinished(success, filePath, errorMessage) {
            root.exportDone = true; root.isExporting = false
            elapsedTimer.running = false
            if (success) {
                root.exportStage = qsTr("¡Exportación completada!")
                root.exportProgressValue = 100
            } else {
                root.exportError = true; root.exportErrorMsg = errorMessage
                root.exportStage = qsTr("Error al exportar")
            }
        }
    }

    FileDialog {
        id: fileDlg; fileMode: FileDialog.SaveFile
        currentFolder: {
            if (root.lastExportPath) {
                var dir = root.lastExportPath.replace(/\/[^\/]*$/, "")
                return "file://" + dir
            }
            return ""
        }
        selectedFile: root.lastExportPath ? ("file://" + root.lastExportPath) : ""
        onAccepted: {
            var path = selectedFile.toString().replace(/^file:\/\//, "")
            root.lastExportPath = path
            root._doExport(path)
        }
    }

    ScrollView {
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth

        Item {
            width: parent.width
            implicitHeight: Math.max(contentCol.visible ? contentCol.implicitHeight + 40
                                                        : progressCol.implicitHeight + 40, 400)

            // ════════════════════════════════════════════════════════
            //  FORMULARIO
            // ════════════════════════════════════════════════════════
            ColumnLayout {
                id: contentCol
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 20 }
                spacing: 10; visible: true
                readonly property int labelWidth: 110

                RowLayout { Layout.fillWidth: true
                    Label { text: qsTr("Formato:"); color: root.textPri; Layout.preferredWidth: contentCol.labelWidth }
                    ComboBox { id: formatCombo; Layout.fillWidth: true
                        model: [ {label:"MP3 (192k CBR)",value:"mp3"}, {label:"WAV (PCM)",value:"wav"},
                                 {label:"OGG Vorbis",value:"ogg"}, {label:"FLAC",value:"flac"}, {label:"M4A (AAC 192k)",value:"m4a"} ]
                        textRole: "label"; valueRole: "value"
                    }
                }
                RowLayout { Layout.fillWidth: true
                    Label { text: qsTr("Sample rate:"); color: root.textPri; Layout.preferredWidth: contentCol.labelWidth }
                    ComboBox { id: sampleRateCombo; Layout.fillWidth: true
                        model: [{label:"48000 Hz",value:48000},{label:"44100 Hz",value:44100}]
                        textRole:"label"; valueRole:"value"; currentIndex: root.defaultSampleRate===44100?1:0
                    }
                }
                RowLayout { Layout.fillWidth: true; visible: formatCombo.currentValue==="wav"
                    Label { text: qsTr("Bit depth:"); color: root.textPri; Layout.preferredWidth: contentCol.labelWidth }
                    ComboBox { id: bitDepthCombo; Layout.fillWidth: true
                        model: [{label:"24 bits",value:24},{label:"16 bits",value:16},{label:"32 bits float",value:32}]
                        textRole:"label"; valueRole:"value"
                    }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: root.divider }
                CheckBox { id: normalizeCheck; text: qsTr("Normalizar a LUFS"); checked: true; Layout.fillWidth: true }
                RowLayout { Layout.fillWidth: true; visible: normalizeCheck.checked
                    Label { text: qsTr("Objetivo:"); color: root.textPri; Layout.preferredWidth: contentCol.labelWidth }
                    ComboBox { id: lufsPreset; Layout.fillWidth: true
                        model: [{label:"-14 LUFS (Spotify)",value:-14},{label:"-16 LUFS (podcast)",value:-16},
                                {label:"-19 LUFS (Apple)",value:-19},{label:"-23 LUFS (EBU)",value:-23}]
                        textRole:"label"; valueRole:"value"; currentIndex: 1
                    }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: root.divider }
                Label { text: qsTr("METADATOS"); color: root.accent; font.pixelSize: 11; font.bold: true }
                GridLayout { Layout.fillWidth: true; columns: 2; columnSpacing: 8; rowSpacing: 6
                    Label { text: qsTr("Título:"); color: root.textPri; Layout.preferredWidth: contentCol.labelWidth }
                    TextField { id: metaTitle; Layout.fillWidth: true; color: root.textPri }
                    Label { text: qsTr("Podcaster:"); color: root.textPri; Layout.preferredWidth: contentCol.labelWidth }
                    TextField { id: metaPodcaster; Layout.fillWidth: true; color: root.textPri }
                    Label { text: qsTr("Podcast:"); color: root.textPri; Layout.preferredWidth: contentCol.labelWidth }
                    TextField { id: metaPodcast; Layout.fillWidth: true; color: root.textPri }
                    Label { text: qsTr("Temporada:"); color: root.textPri; Layout.preferredWidth: contentCol.labelWidth }
                    SpinBox { id: metaSeason; Layout.fillWidth: true; from: 0; to: 999; editable: true }
                    Label { text: qsTr("Episodio:"); color: root.textPri; Layout.preferredWidth: contentCol.labelWidth }
                    SpinBox { id: metaEpisode; Layout.fillWidth: true; from: 0; to: 9999; editable: true }
                    Label { text: qsTr("Descripción:"); color: root.textPri; Layout.preferredWidth: contentCol.labelWidth; Layout.alignment: Qt.AlignTop }
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 60
                        clip: true
                        TextArea {
                            id: metaDescription
                            color: root.textPri
                            wrapMode: TextArea.Wrap
                            background: Rectangle { color: "#1f2226"; radius: 4; border.color: root.divider }
                        }
                    }
                    Label { text: qsTr("Portada:"); color: root.textPri; Layout.preferredWidth: contentCol.labelWidth; Layout.alignment: Qt.AlignTop }
                    RowLayout { Layout.fillWidth: true; spacing: 8
                        Rectangle { id: coverPreview; implicitWidth: 64; implicitHeight: 64; color: "#1f2226"; radius: 4; border.color: root.divider
                            property string path: ""
                            Image { anchors.fill: parent; anchors.margins: 2; source: coverPreview.path?("file://"+coverPreview.path):""; visible: source.toString()!==""; fillMode: Image.PreserveAspectCrop; asynchronous: true; cache: false }
                            Label { anchors.centerIn: parent; visible: coverPreview.path===""; text: qsTr("sin\nportada"); color: root.textSec; font.pixelSize: 9; horizontalAlignment: Text.AlignHCenter }
                        }
                        ColumnLayout { spacing: 4
                            Button { text: qsTr("Elegir…"); onClicked: coverFileDlg.open() }
                            Button { text: qsTr("Quitar"); enabled: coverPreview.path!==""; onClicked: coverPreview.path="" }
                        }
                        Item { Layout.fillWidth: true }
                    }
                }
                FileDialog { id: coverFileDlg; title: qsTr("Portada"); fileMode: FileDialog.OpenFile
                    nameFilters: [qsTr("Imágenes (*.jpg *.jpeg *.png)")]
                    onAccepted: coverPreview.path = selectedFile.toString().replace(/^file:\/\//, "")
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: root.divider }
                RowLayout { Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    Button { text: qsTr("Cancelar"); onClicked: root.close() }
                    Button { text: qsTr("Exportar…"); highlighted: true
                        onClicked: {
                            var ext = formatCombo.currentValue
                            fileDlg.nameFilters = [ext.toUpperCase() + " (*." + ext + ")"]
                            fileDlg.defaultSuffix = ext
                            if (root.lastExportPath) {
                                var base = root.lastExportPath.replace(/\.[^.]+$/, "")
                                fileDlg.selectedFile = "file://" + base + "." + ext
                            }
                            fileDlg.open()
                        }
                    }
                }
            }

            // ════════════════════════════════════════════════════════
            //  PROGRESO
            // ════════════════════════════════════════════════════════
            ColumnLayout {
                id: progressCol
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 20 }
                spacing: 12; visible: false

                Label { text: qsTr("Archivo de salida:"); color: root.textSec; font.pixelSize: 11 }
                Label { text: root.exportFilePath; color: root.textPri; font.pixelSize: 11; font.family: "monospace"; elide: Text.ElideMiddle; Layout.fillWidth: true }
                Label {
                    text: (formatCombo.currentText || "") + " • " + (sampleRateCombo.currentValue || 48000) + " Hz"
                    color: root.textSec; font.pixelSize: 10
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: root.divider }
                Label { text: root.exportStage; color: root.exportDone?(root.exportError?"#e74c3c":"#2ecc71"):root.textPri; font.pixelSize: 13; font.bold: true }
                Rectangle { Layout.fillWidth: true; height: 24; radius: 4; color: "#1a1d21"; border.color: root.divider
                    Rectangle { anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.margins: 2
                        width: Math.max(0,(parent.width-4)*(root.exportProgressValue/100.0)); radius: 3
                        color: root.exportDone?(root.exportError?"#e74c3c":"#27ae60"):root.accent
                        Behavior on width { NumberAnimation { duration: 150 } }
                    }
                    Label { anchors.centerIn: parent; text: root.exportProgressValue+" %"; color: "#fff"; font.pixelSize: 11; font.bold: true }
                }
                Label { text: qsTr("Tiempo: %1:%2").arg(Math.floor(elapsedTimer.seconds/60)).arg(("0"+(elapsedTimer.seconds%60)).slice(-2)); color: root.textSec; font.pixelSize: 11 }
                Timer { id: elapsedTimer; property int seconds: 0; interval: 1000; repeat: true; onTriggered: seconds++ }
                Label { visible: root.exportError; text: root.exportErrorMsg; color: "#e74c3c"; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                Rectangle { Layout.fillWidth: true; height: 1; color: root.divider }
                RowLayout { Layout.fillWidth: true; Item { Layout.fillWidth: true }
                    Button { text: qsTr("Cerrar"); visible: root.exportDone; onClicked: root.close() }
                }
            }
        }
    }

    function _doExport(path) {
        var md = { title: metaTitle.text, podcaster: metaPodcaster.text, podcast: metaPodcast.text,
                   description: metaDescription.text, episode: metaEpisode.value, season: metaSeason.value, coverPath: coverPreview.path }
        ProjectIO.metadata = md
        var opts = { format: formatCombo.currentValue, sampleRate: sampleRateCombo.currentValue, bitDepth: bitDepthCombo.currentValue }
        if (normalizeCheck.checked) opts.normalizeLUFS = lufsPreset.currentValue
        opts.metadata = { title: md.title, artist: md.podcaster, album: md.podcast, comment: md.description, track: String(md.episode) }
        if (coverPreview.path) opts.metadata.coverPath = coverPreview.path

        exportFilePath = path; isExporting = true; exportDone = false; exportError = false
        exportProgressValue = 0; exportStage = qsTr("Iniciando…")
        contentCol.visible = false; progressCol.visible = true
        elapsedTimer.seconds = 0; elapsedTimer.running = true

        Qt.callLater(function() { ExportManager.exportWithOptions(path, opts) })
    }
}
