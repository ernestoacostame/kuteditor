import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: transcriptionPanel

    required property var appRoot
    
    // Binding al track activo
    property int trackIndex: appRoot.selectedTrack
    
    property bool hasData: false
    property bool transcribing: false
    property real progress: 0.0
    property var segments: []

    color: appRoot.panelBg // Estilo oscuro

    // Solo mostramos el panel si la pista seleccionada tiene transcripción o está en progreso
    visible: appRoot.isManuscriptVisible && trackIndex >= 0 && (hasData || transcribing)

    property string searchQuery: ""

    function copyAllText() {
        let fullText = ""
        for (let i=0; i<segments.length; i++) {
            fullText += segments[i].text + " "
        }
        console.log("[Manuscript] Copiando texto:", fullText.length, "caracteres")
        Transcription.copyToClipboard(fullText.trim())
    }
    
    Connections {
        target: Transcription
        function onTranscriptionStateChanged(idx) {
            if (idx !== transcriptionPanel.trackIndex) return
            transcriptionPanel.refresh()
        }
        function onTranscriptionFinished(idx) {
            if (idx !== transcriptionPanel.trackIndex) return
            transcriptionPanel.refresh()
        }
        function onTranscriptionProgressChanged(idx, prog) {
            if (idx !== transcriptionPanel.trackIndex) return
            transcriptionPanel.transcribing = true
            transcriptionPanel.progress = prog
        }
    }
    
    onTrackIndexChanged: refresh()

    function refresh() {
        if (trackIndex < 0) {
            hasData = false
            transcribing = false
            segments = []
            return
        }
        transcribing = Transcription.isTranscribing(trackIndex)
        hasData = Transcription.hasTranscription(trackIndex)
        progress = Transcription.transcriptionProgress(trackIndex)
        if (hasData) {
            segments = Transcription.transcriptionSegments(trackIndex)
        } else {
            segments = []
        }
    }

    Component.onCompleted: refresh()

    // Header
    Rectangle {
        id: header
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 32
        color: appRoot.panelBg2

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 8
            spacing: 12
            
            Label {
                text: "Manuscript"
                font.bold: true
                color: appRoot.accent
                Layout.alignment: Qt.AlignVCenter
            }
            
            Label {
                text: trackIndex >= 0 ? qsTr("- Pista %1").arg(trackIndex + 1) : ""
                color: appRoot.textSec
                font.pixelSize: 11
                Layout.alignment: Qt.AlignVCenter
            }

            Item { Layout.fillWidth: true }

            // Buscador
            Rectangle {
                Layout.preferredWidth: 200
                Layout.preferredHeight: 24
                color: "#111418"; radius: 4; border.color: appRoot.divider
                visible: transcriptionPanel.hasData && !transcriptionPanel.transcribing

                RowLayout {
                    anchors.fill: parent; anchors.leftMargin: 6; anchors.rightMargin: 6
                    Text { 
                        text: "\ue8b6" // search icon
                        font.family: "Material Symbols Outlined"; font.pixelSize: 16; color: appRoot.textSec
                    }
                    TextField {
                        id: searchField
                        placeholderText: qsTr("Buscar...")
                        Layout.fillWidth: true; background: null; font.pixelSize: 12; color: appRoot.textPri
                        padding: 0; leftPadding: 0
                        onTextChanged: {
                            transcriptionPanel.searchQuery = text.toLowerCase()
                            console.log("[Manuscript] Buscando:", transcriptionPanel.searchQuery)
                        }
                    }
                    ToolButton {
                        width: 16; height: 16; padding: 0; visible: searchField.text !== ""
                        onClicked: { searchField.text = ""; transcriptionPanel.searchQuery = "" }
                        contentItem: Text { anchors.centerIn: parent; text: "\ue5cd"; font.family: "Material Symbols Outlined"; font.pixelSize: 14; color: appRoot.textSec; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        background: null
                    }
                }
            }

            // Botón Copiar
            ToolButton {
                Layout.preferredWidth: 28; Layout.preferredHeight: 28
                visible: transcriptionPanel.hasData && !transcriptionPanel.transcribing
                onClicked: transcriptionPanel.copyAllText()
                ToolTip.text: qsTr("Copiar todo el texto")
                ToolTip.visible: hovered; ToolTip.delay: 500
                contentItem: Text { anchors.centerIn: parent; text: "\ue14d"; font.family: "Material Symbols Outlined"; font.pixelSize: 20; color: parent.hovered ? appRoot.textPri : appRoot.textSec; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                background: null
            }

            // Botón Cerrar
            ToolButton {
                Layout.preferredWidth: 28; Layout.preferredHeight: 28
                onClicked: appRoot.isManuscriptVisible = false
                ToolTip.text: qsTr("Cerrar Manuscrito (Ctrl+M)")
                ToolTip.visible: hovered; ToolTip.delay: 500
                contentItem: Text { anchors.centerIn: parent; text: "\ue5cd"; font.family: "Material Symbols Outlined"; font.pixelSize: 20; color: parent.hovered ? appRoot.accent : appRoot.textSec; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                background: null
            }
        }
        
        Rectangle {
            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
            height: 1; color: appRoot.divider
        }
    }

    // ProgressBar
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: header.bottom
        height: 2
        color: "transparent"
        visible: transcriptionPanel.transcribing

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: parent.width * transcriptionPanel.progress
            color: appRoot.accent
            Behavior on width { NumberAnimation { duration: 200 } }
        }
    }

    // Loader/Mensaje
    Item {
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        visible: transcriptionPanel.transcribing

        Column {
            anchors.centerIn: parent
            spacing: 16

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 12
                BusyIndicator {
                    width: 24; height: 24
                    running: transcriptionPanel.transcribing
                }
                Label {
                    text: qsTr("Transcribiendo… %1%").arg(Math.round(transcriptionPanel.progress * 100))
                    color: appRoot.textPri
                    font.pixelSize: 14
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            Button {
                id: cancelBtn
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Cancelar")
                flat: true
                onClicked: Transcription.cancelTranscription(transcriptionPanel.trackIndex)
                
                contentItem: Label {
                    anchors.centerIn: parent
                    text: cancelBtn.text
                    color: cancelBtn.hovered ? appRoot.accent : appRoot.textSec
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    implicitWidth: 100
                    implicitHeight: 32
                    border.color: cancelBtn.hovered ? appRoot.accent : appRoot.divider
                    color: cancelBtn.pressed ? Qt.rgba(1,1,1,0.05) : "transparent"
                    radius: 4
                }
            }
        }
    }

    // ScrollView de texto (estilo Hindenburg)
    ScrollView {
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 14
        visible: transcriptionPanel.hasData && !transcriptionPanel.transcribing
        clip: true
        ScrollBar.vertical.policy: ScrollBar.AsNeeded

        Flickable {
            contentWidth: width
            contentHeight: flow.height + 40

            Flow {
                id: flow
                width: Math.min(parent.width, 800)
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 6

                Repeater {
                    model: transcriptionPanel.segments
                    delegate: Rectangle {
                        id: segRect
                        required property var modelData
                        required property int index

                        readonly property real segStartSec: modelData.startSec || 0
                        readonly property real segEndSec:   modelData.endSec   || 0
                        readonly property string segText:   modelData.text || ""

                        readonly property bool isActive:
                            AudioEngine.currentTime >= segStartSec &&
                            AudioEngine.currentTime <= segEndSec

                        readonly property bool matchesSearch: 
                            transcriptionPanel.searchQuery.length > 0 && 
                            segText.toLowerCase().indexOf(transcriptionPanel.searchQuery) !== -1

                        width: segLabel.implicitWidth + 12
                        height: segLabel.implicitHeight + 8
                        radius: 4
                        
                        // Fondo: rojo si activo, amarillo si coincide con búsqueda, transparente sino
                        color: isActive ? appRoot.accent : 
                               (matchesSearch ? "#f1c40f" : 
                               (segMA.containsMouse ? Qt.rgba(1,1,1,0.06) : "transparent"))
                        
                        border.color: matchesSearch ? "#f39c12" : "transparent"
                        border.width: matchesSearch ? 1 : 0

                        Label {
                            id: segLabel
                            anchors.centerIn: parent
                            text: segRect.segText
                            color: isActive ? "#ffffff" : (matchesSearch ? "#000000" : appRoot.textPri)
                            font.pixelSize: 15
                            font.bold: matchesSearch
                        }

                        MouseArea {
                            id: segMA
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: AudioEngine.seekTime(segRect.segStartSec)
                        }
                    }
                }
            }
        }
    }
}
