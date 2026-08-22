import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

/**
 * ChapterEditor: ventana externa para gestionar capítulos cómodamente.
 * Permite renombrar, eliminar, reordenar, asignar artwork y links.
 */
Window {
    id: root
    title: qsTr("Capítulos")
    width: 560; height: 500
    minimumWidth: 440; minimumHeight: 350
    flags: Qt.Window
    color: "#22252a"

    function open() { visible = true; raise() }

    // ── Encabezado ──────────────────────────────────────────────────
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            color: "#2a2d32"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16; anchors.rightMargin: 16
                spacing: 8

                Label {
                    text: qsTr("Capítulos")
                    color: "#ecf0f1"
                    font.pixelSize: 16; font.bold: true
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: ChapterModel.count + qsTr(" capítulos")
                    color: "#95a5a6"
                    font.pixelSize: 11
                }
                Button {
                    text: qsTr("+ Añadir en cursor")
                    onClicked: UndoManager.addChapterAtSec(AudioEngine.currentTime)
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: "#3c4146" }

        // ── Lista de capítulos ──────────────────────────────────────
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            ListView {
                id: chapterList
                model: ChapterModel
                spacing: 1

                delegate: Rectangle {
                    id: chapterDelegate
                    width: chapterList.width
                    height: delegateContent.implicitHeight + 16
                    color: ListView.isCurrentItem ? "#35393e" : (index % 2 === 0 ? "#26292e" : "#2a2d32")

                    required property int index
                    required property double startSec
                    required property string title
                    required property string artwork
                    required property var link

                    RowLayout {
                        id: delegateContent
                        anchors { fill: parent; margins: 8 }
                        spacing: 10

                        // ── Línea y punto de línea de tiempo ──────────
                        Item {
                            Layout.preferredWidth: 24
                            Layout.fillHeight: true

                            // Línea de conexión vertical
                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.top: chapterDelegate.index === 0 ? parent.verticalCenter : parent.top
                                anchors.bottom: chapterDelegate.index === (ChapterModel.count - 1) ? parent.verticalCenter : parent.bottom
                                width: 2
                                color: "#4f555e"
                            }

                            // Círculo del color del marcador
                            Rectangle {
                                anchors.centerIn: parent
                                width: 12; height: 12
                                radius: 6
                                color: {
                                    var markerColors = ["#0ea5e9", "#8b5cf6", "#e67e22", "#2ecc71", "#e74c3c", "#f1c40f", "#1abc9c", "#9b59b6"]
                                    return markerColors[chapterDelegate.index % markerColors.length]
                                }
                                border.color: "#22252a"
                                border.width: 1
                            }
                        }

                        // ── Thumbnail artwork ───────────────────────
                        Rectangle {
                            Layout.preferredWidth: 48
                            Layout.preferredHeight: 48
                            radius: 4
                            color: "#1f2226"
                            border.color: "#3c4146"

                            Image {
                                anchors.fill: parent; anchors.margins: 2
                                source: chapterDelegate.artwork ? ("file://" + chapterDelegate.artwork) : ""
                                visible: source.toString() !== ""
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                            }
                            Label {
                                anchors.centerIn: parent
                                visible: !chapterDelegate.artwork
                                text: "🖼"
                                font.pixelSize: 16
                                color: "#555"
                            }

                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    artworkDlg.chapterIdx = chapterDelegate.index
                                    artworkDlg.open()
                                }
                                ToolTip.visible: containsMouse
                                ToolTip.text: qsTr("Cambiar imagen del capítulo")
                            }
                        }

                        // ── Info principal ───────────────────────────
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3

                            RowLayout {
                                spacing: 6
                                Label {
                                    text: {
                                        var s = chapterDelegate.startSec
                                        var m = Math.floor(s / 60)
                                        var ss = Math.floor(s % 60)
                                        return m + ":" + ("0" + ss).slice(-2)
                                    }
                                    color: {
                                        var markerColors = ["#0ea5e9", "#8b5cf6", "#e67e22", "#2ecc71", "#e74c3c", "#f1c40f", "#1abc9c", "#9b59b6"]
                                        return markerColors[chapterDelegate.index % markerColors.length]
                                    }
                                    font.pixelSize: 12
                                    font.family: "Menlo"
                                    font.bold: true
                                    Layout.preferredWidth: 50
                                }
                                TextField {
                                    id: titleField
                                    Layout.fillWidth: true
                                    text: chapterDelegate.title
                                    color: "#ecf0f1"
                                    font.pixelSize: 13
                                    background: Rectangle {
                                        color: titleField.activeFocus ? "#1f2226" : "transparent"
                                        radius: 3
                                        border.color: titleField.activeFocus ? "#e67e22" : "transparent"
                                    }
                                    onEditingFinished: {
                                        UndoManager.renameChapter(chapterDelegate.index, text)
                                    }
                                }
                            }

                            RowLayout {
                                spacing: 6
                                Label {
                                    text: qsTr("Link:")
                                    color: "#666"
                                    font.pixelSize: 10
                                }
                                TextField {
                                    id: linkField
                                    Layout.fillWidth: true
                                    text: chapterDelegate.link ? chapterDelegate.link.toString() : ""
                                    placeholderText: qsTr("https://ejemplo.com (opcional)")
                                    color: "#95a5a6"
                                    placeholderTextColor: "#556270"
                                    palette.text: "#95a5a6"
                                    palette.placeholderText: "#556270"
                                    font.pixelSize: 10
                                    background: Rectangle {
                                        color: linkField.activeFocus ? "#1f2226" : "transparent"
                                        radius: 3
                                        border.color: linkField.activeFocus ? "#3498db" : "transparent"
                                    }
                                    onEditingFinished: {
                                        ChapterModel.setChapterLink(chapterDelegate.index, text)
                                    }
                                }
                            }
                        }

                        // ── Botones de acción ───────────────────────
                        ColumnLayout {
                            spacing: 4

                            Button {
                                id: playBtn
                                implicitWidth: 32; implicitHeight: 32; flat: true
                                ToolTip.visible: playBtn.hovered
                                ToolTip.text: qsTr("Ir al capítulo")
                                onClicked: AudioEngine.seekTime(chapterDelegate.startSec)
                                contentItem: Text {
                                    anchors.centerIn: parent
                                    text: "play_arrow"
                                    font.family: "Material Symbols Outlined"
                                    font.pixelSize: 20
                                    color: "#ecf0f1"
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                                background: Rectangle {
                                    radius: 4
                                    color: playBtn.hovered ? Qt.rgba(1,1,1,0.08) : "transparent"
                                }
                            }
                            Button {
                                id: deleteBtn
                                implicitWidth: 32; implicitHeight: 32; flat: true
                                ToolTip.visible: deleteBtn.hovered
                                ToolTip.text: qsTr("Eliminar")
                                onClicked: UndoManager.removeChapter(chapterDelegate.index)
                                contentItem: Text {
                                    anchors.centerIn: parent
                                    text: "delete"
                                    font.family: "Material Symbols Outlined"
                                    font.pixelSize: 20
                                    color: "#e74c3c"
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                                background: Rectangle {
                                    radius: 4
                                    color: deleteBtn.hovered ? Qt.rgba(1,1,1,0.08) : "transparent"
                                }
                            }
                        }
                    }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: "#3c4146" }

        // ── Barra inferior ──────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            color: "#2a2d32"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12; anchors.rightMargin: 12
                spacing: 8

                Button {
                    text: qsTr("Eliminar todos")
                    enabled: ChapterModel.count > 0
                    onClicked: UndoManager.clearChapters()
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: qsTr("Exportar JSON")
                    enabled: ChapterModel.count > 0
                    onClicked: exportJsonDlg.open()
                }
            }
        }
    }

    QtObject {
        id: artworkDlg
        property int chapterIdx: -1
        function open() {
            const path = FileHelper.getOpenFileName(
                qsTr("Imagen del capítulo"),
                FileHelper.homeDir() + "/Pictures",
                "Im\u00e1genes (*.jpg *.jpeg *.png *.webp)"
            )
            if (path !== "") {
                ChapterModel.setChapterArtwork(chapterIdx, path)
            }
        }
    }

    QtObject {
        id: exportJsonDlg
        function open() {
            const path = FileHelper.getSaveFileName(
                qsTr("Exportar capítulos JSON"),
                FileHelper.homeDir() + "/untitled.json",
                "JSON (*.json)",
                "json"
            )
            if (path !== "") {
                var chapters = ChapterModel.toJson()
                var wrapper = { "version": "1.2.0", "chapters": chapters }
                var jsonStr = JSON.stringify(wrapper, null, 2)
                FileHelper.writeTextFile(path, jsonStr)
            }
        }
    }
}
