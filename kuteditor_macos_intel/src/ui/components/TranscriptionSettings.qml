import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

/**
 * TranscriptionSettings: ventana de configuración de transcripción local.
 * Permite seleccionar modelo, idioma, descargar/eliminar modelos,
 * y ver el estado de la feature.
 */
ApplicationWindow {
    id: transcriptionSettingsDialog
    title: qsTr("Transcripción Local (Whisper)")
    width: 560
    height: 580
    flags: Qt.Window
    color: "#22252a"

    palette.window: "#22252a"
    palette.windowText: "#ecf0f1"
    palette.text: "#ecf0f1"
    palette.base: "#2a2d32"
    palette.button: "#34383d"
    palette.buttonText: "#ecf0f1"
    palette.highlight: "#e74c3c"
    palette.highlightedText: "#ffffff"
    palette.toolTipBase: "#1a1d21"
    palette.toolTipText: "#ecf0f1"

    function open() { visible = true; raise(); refreshModels() }
    function close() { visible = false }

    property var modelsList: []

    function refreshModels() {
        modelsList = Transcription.modelsList
    }

    Connections {
        target: Transcription
        function onModelStatusChanged() { transcriptionSettingsDialog.refreshModels() }
        function onDownloadProgressChanged(prog) {
            downloadProgressBar.value = prog
        }
        function onDownloadFinished(name) {
            statusLabel.text = qsTr("Modelo '%1' descargado correctamente.").arg(name)
            statusLabel.color = "#2ecc71"
            Transcription.activeModel = name
        }
        function onDownloadError(msg) {
            statusLabel.text = msg
            statusLabel.color = "#e74c3c"
        }
        function onModelCheckResult(message, hasUpdates) {
            statusLabel.text = message
            statusLabel.color = hasUpdates ? "#f39c12" : "#2ecc71"
            checkUpdatesBtn.checking = false
        }
    }

    ScrollView {
        anchors.fill: parent
        anchors.margins: 16
        contentWidth: availableWidth

        Column {
            spacing: 16
            width: parent.width

            // ---- Encabezado ----
            Label {
                text: qsTr("Transcripción Local")
                color: "#ecf0f1"
                font.pixelSize: 18
                font.bold: true
            }

            Label {
                text: {
                    if (!Transcription.available) {
                        if (Qt.platform.os === "osx") {
                            return qsTr("⚠ whisper.cpp no encontrado. Instálalo con:\n" +
                                        "  brew install whisper.cpp\n" +
                                        "o compílalo desde fuente con -DGGML_METAL=ON")
                        } else {
                            return qsTr("⚠ whisper.cpp no encontrado. Instálalo con:\n" +
                                        "  pacman -S whisper.cpp\n" +
                                        "o compílalo desde fuente con -DGGML_VULKAN=ON")
                        }
                    }
                    var msg = qsTr("Motor whisper.cpp disponible.")
                    msg += "\n" + qsTr("GPU: %1 (%2)").arg(Transcription.gpuName).arg(Transcription.gpuBackend)
                    if (!Transcription.hasGpu)
                        msg += "\n" + qsTr("⚠ Sin aceleración GPU. La transcripción será más lenta.")
                    return msg
                }
                color: {
                    if (!Transcription.available) return "#e74c3c"
                    if (!Transcription.hasGpu)    return "#f39c12"
                    return "#2ecc71"
                }
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                width: parent.width
            }

            // ---- Selector de idioma ----
            RowLayout {
                width: parent.width
                spacing: 10

                Label {
                    text: qsTr("Idioma:")
                    color: "#ecf0f1"
                    font.pixelSize: 13
                }

                ComboBox {
                    id: langCombo
                    Layout.preferredWidth: 200
                    model: [
                        { text: qsTr("Auto-detectar"), value: "auto" },
                        { text: qsTr("Español"),       value: "es" },
                        { text: qsTr("Inglés"),        value: "en" },
                        { text: qsTr("Portugués"),     value: "pt" },
                        { text: qsTr("Francés"),       value: "fr" },
                        { text: qsTr("Alemán"),        value: "de" },
                        { text: qsTr("Italiano"),      value: "it" },
                        { text: qsTr("Japonés"),       value: "ja" },
                        { text: qsTr("Chino"),         value: "zh" },
                    ]
                    textRole: "text"
                    valueRole: "value"

                    Component.onCompleted: {
                        for (let i = 0; i < model.length; i++) {
                            if (model[i].value === Transcription.language) {
                                currentIndex = i; break
                            }
                        }
                    }

                    onActivated: Transcription.language = currentValue
                }
            }

            // ---- Separador ----
            Rectangle {
                width: parent.width
                height: 1
                color: "#3c4146"
            }

            // ---- Modelos disponibles ----
            Label {
                text: qsTr("Modelos disponibles")
                color: "#ecf0f1"
                font.pixelSize: 14
                font.bold: true
            }

            Label {
                text: qsTr("Los modelos se descargan desde Hugging Face y se almacenan en:\n%1")
                    .arg(Transcription.modelsDir)
                color: "#95a5a6"
                font.pixelSize: 10
                wrapMode: Text.WordWrap
                width: parent.width
            }

            Column {
                width: parent.width
                spacing: 8

                Repeater {
                    model: transcriptionSettingsDialog.modelsList
                    delegate: Rectangle {
                        id: cardRect
                        required property var modelData
                        required property int index

                        width: parent.width
                        implicitHeight: cardContent.implicitHeight + 20
                        radius: 6
                        color: modelData.active
                            ? Qt.rgba(0.2, 0.6, 0.9, 0.16)
                            : (cardHoverArea.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(1, 1, 1, 0.04))
                        border.color: modelData.active
                            ? "#3498db"
                            : (cardHoverArea.containsMouse ? "#555" : "#3c4146")
                        border.width: modelData.active ? 1.5 : 0.8

                        MouseArea {
                            id: cardHoverArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: (modelData.downloaded && !modelData.active) ? Qt.PointingHandCursor : Qt.ArrowCursor
                            onClicked: {
                                if (modelData.downloaded && !modelData.active) {
                                    Transcription.activeModel = modelData.name
                                }
                            }
                        }

                        RowLayout {
                            id: cardContent
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.margins: 12
                            spacing: 12

                            Column {
                                Layout.fillWidth: true
                                spacing: 3

                                Label {
                                    text: modelData.displayName
                                    color: "#ecf0f1"
                                    font.pixelSize: 14
                                    font.bold: true
                                }
                                Label {
                                    text: modelData.description + " (~" + modelData.sizeMB + " MB)"
                                    color: "#95a5a6"
                                    font.pixelSize: 11
                                    wrapMode: Text.WordWrap
                                    width: parent.width
                                    elide: Text.ElideRight
                                    maximumLineCount: 2
                                }
                            }

                            Column {
                                spacing: 4
                                Layout.alignment: Qt.AlignVCenter
                                Layout.preferredWidth: 100

                                // Botón principal: descargar / activar
                                Button {
                                    id: actionBtn
                                    implicitWidth: 90
                                    implicitHeight: 28
                                    text: {
                                        if (modelData.downloaded && modelData.active)
                                            return qsTr("Activo ✓")
                                        if (modelData.downloaded)
                                            return qsTr("Usar")
                                        return qsTr("Descargar")
                                    }
                                    enabled: !Transcription.isDownloading() &&
                                             !(modelData.downloaded && modelData.active)

                                    onClicked: {
                                        if (modelData.downloaded) {
                                            Transcription.activeModel = modelData.name
                                        } else {
                                            Transcription.downloadModel(modelData.name)
                                            statusLabel.text = qsTr("Descargando %1…")
                                                .arg(modelData.displayName)
                                            statusLabel.color = "#f39c12"
                                        }
                                    }

                                    background: Rectangle {
                                        radius: 4
                                        color: {
                                            if (modelData.downloaded && modelData.active)
                                                return Qt.rgba(0.2, 0.8, 0.4, 0.15)
                                            if (modelData.downloaded)
                                                return Qt.rgba(0.2, 0.6, 0.9, actionBtn.hovered ? 0.35 : 0.2)
                                            return Qt.rgba(1, 1, 1, actionBtn.hovered ? 0.16 : 0.08)
                                        }
                                        border.color: {
                                            if (modelData.downloaded && modelData.active)
                                                return "#2ecc71"
                                            if (modelData.downloaded)
                                                return actionBtn.hovered ? "#5dade2" : "#3498db"
                                            return actionBtn.hovered ? "#888" : "#555"
                                        }
                                        border.width: 1
                                    }
                                    contentItem: Label {
                                        anchors.centerIn: parent
                                        text: parent.text
                                        color: parent.enabled ? "#ecf0f1" : "#666"
                                        font.pixelSize: 11
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        acceptedButtons: Qt.NoButton
                                        cursorShape: parent.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                    }
                                }

                                // Botón eliminar (solo si está descargado y no activo)
                                Button {
                                    id: deleteBtn
                                    implicitWidth: 90
                                    implicitHeight: 22
                                    text: qsTr("Eliminar")
                                    visible: modelData.downloaded && !modelData.active
                                    onClicked: Transcription.deleteModel(modelData.name)
                                    background: Rectangle {
                                        radius: 3
                                        color: deleteBtn.hovered ? Qt.rgba(0.75, 0.22, 0.17, 0.15) : "transparent"
                                        border.color: deleteBtn.hovered ? "#e74c3c" : "#c0392b"
                                        border.width: 1
                                    }
                                    contentItem: Label {
                                        anchors.centerIn: parent
                                        text: parent.text
                                        color: deleteBtn.hovered ? "#e74c3c" : "#c0392b"
                                        font.pixelSize: 10
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        acceptedButtons: Qt.NoButton
                                        cursorShape: Qt.PointingHandCursor
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ---- Botón de verificar actualizaciones ----
            RowLayout {
                width: parent.width
                spacing: 10

                Item { Layout.fillWidth: true }

                Button {
                    id: checkUpdatesBtn
                    property bool checking: false
                    text: checking ? qsTr("Verificando…") : qsTr("Buscar actualizaciones")
                    enabled: !checking && !Transcription.isDownloading()
                    implicitWidth: contentItem.implicitWidth + 24
                    implicitHeight: 32
                    onClicked: {
                        checking = true
                        statusLabel.text = qsTr("Verificando modelos descargados…")
                        statusLabel.color = "#f39c12"
                        Transcription.checkModelUpdates()
                    }
                    background: Rectangle {
                        radius: 4
                        color: Qt.rgba(1, 1, 1, parent.hovered ? 0.12 : 0.06)
                        border.color: "#3498db"
                        border.width: 1
                    }
                    contentItem: Label {
                        anchors.centerIn: parent
                        text: parent.text
                        color: parent.enabled ? "#ecf0f1" : "#666"
                        font.pixelSize: 11
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }

            // ---- Barra de progreso de descarga ----
            Column {
                width: parent.width
                spacing: 4
                visible: Transcription.isDownloading()

                Label {
                    text: qsTr("Descargando modelo…")
                    color: "#f39c12"
                    font.pixelSize: 11
                }

                ProgressBar {
                    id: downloadProgressBar
                    width: parent.width
                    from: 0.0
                    to: 1.0
                    value: Transcription.downloadProgress()
                }
            }

            // ---- Estado / mensajes ----
            Label {
                id: statusLabel
                text: ""
                color: "#95a5a6"
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                width: parent.width
            }

            // ---- Info de directorio ----
            Rectangle {
                width: parent.width
                height: 1
                color: "#3c4146"
            }

            Label {
                text: qsTr("Los modelos Whisper procesan todo el audio localmente. Ningún dato de audio sale de tu máquina.")
                color: "#7f8c8d"
                font.pixelSize: 10
                font.italic: true
                wrapMode: Text.WordWrap
                width: parent.width
            }
        }
    }
}
