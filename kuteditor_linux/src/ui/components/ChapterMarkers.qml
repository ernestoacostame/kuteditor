import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.platform as Platform

/**
 * ChapterMarkers: overlay que dibuja marcadores de capítulos sobre la
 * línea de tiempo. Se posiciona sobre el área de clips.
 *
 * Cada marcador es una línea vertical con triángulo + etiqueta en la parte
 * superior. Los marcadores son arrastrables y se imanan (snap) a los bordes
 * de los clips de audio.
 */
Item {
    id: markers
    clip: true

    required property var appRoot
    required property var chapterModel

    readonly property var markerColors: ["#0ea5e9", "#8b5cf6", "#e67e22", "#2ecc71", "#e74c3c", "#f1c40f", "#1abc9c", "#9b59b6"]

    // ── Marcadores ──────────────────────────────────────────────────
    Repeater {
        model: chapterModel

        delegate: Item {
            id: marker
            property double sec: model.startSec
            property int chapterIdx: model.index
            property string chapterTitle: model.title || ""

            x: appRoot.secToViewX(sec)
            width: 1
            height: markers.height
            visible: x >= -100 && x <= markers.width + 100

            // ── Línea vertical (punteada) ───────────────────────────
            Rectangle {
                id: markerLine
                width: 2
                height: parent.height
                color: markers.markerColors[marker.chapterIdx % markers.markerColors.length]
                opacity: 0.75
                x: -1
            }

            // ── Triángulo + etiqueta en la parte superior ───────────
            Rectangle {
                id: markerHead
                x: -1
                y: 28
                width: Math.max(labelText.implicitWidth + 16, 40)
                height: 18
                radius: 3
                color: dragArea.isDragging 
                       ? Qt.darker(markers.markerColors[marker.chapterIdx % markers.markerColors.length], 1.2) 
                       : markers.markerColors[marker.chapterIdx % markers.markerColors.length]
                border.color: Qt.lighter(color, 1.2)
                border.width: dragArea.containsMouse ? 1 : 0

                // Triángulo apuntando hacia abajo
                Canvas {
                    id: markerTriangle
                    anchors.top: parent.bottom
                    anchors.horizontalCenter: parent.left
                    anchors.horizontalCenterOffset: 1
                    width: 10; height: 6
                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)
                        ctx.fillStyle = parent.color
                        ctx.beginPath()
                        ctx.moveTo(0, 0)
                        ctx.lineTo(width, 0)
                        ctx.lineTo(width / 2, height)
                        ctx.closePath()
                        ctx.fill()
                    }
                    Connections {
                        target: markerHead
                        function onColorChanged() { markerTriangle.requestPaint() }
                    }
                }

                Label {
                    id: labelText
                    anchors.centerIn: parent
                    text: marker.chapterTitle
                    color: "#fff"
                    font.pixelSize: 10
                    font.bold: true
                    elide: Text.ElideRight
                    maximumLineCount: 1
                    width: Math.min(implicitWidth, 140)
                }

                MouseArea {
                    id: dragArea
                    anchors.fill: parent
                    anchors.margins: -4  // zona de toque expandida
                    hoverEnabled: true
                    cursorShape: isDragging ? Qt.ClosedHandCursor : Qt.PointingHandCursor
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    drag.target: null  // controlamos el drag manualmente
                    property real dragStartX: 0
                    property bool isDragging: false

                    onPressed: (mouse) => {
                        if (mouse.button === Qt.RightButton) {
                            chapterContextMenu.chapterIdx = marker.chapterIdx
                            chapterContextMenu.open()
                            return
                        }
                        dragStartX = mouse.x
                        isDragging = true
                    }

                    onPositionChanged: (mouse) => {
                        if (!pressed || !isDragging) return
                        var globalX = marker.x + (mouse.x - dragStartX)
                        var newSec = appRoot.viewXToSec(globalX)
                        if (newSec < 0) newSec = 0

                        // Snap a bordes de clips (threshold: 15px)
                        var threshSec = 15 * appRoot.secPerPixel
                        newSec = chapterModel.snapToClipEdgeSec(newSec, threshSec)

                        UndoManager.moveChapterToSec(marker.chapterIdx, newSec)
                    }

                    onReleased: {
                        isDragging = false
                    }

                    onDoubleClicked: {
                        renameField.text = marker.chapterTitle
                        renamePopup.chapterIdx = marker.chapterIdx
                        renamePopup.open()
                        renameField.forceActiveFocus()
                        renameField.selectAll()
                    }

                    ToolTip {
                        visible: dragArea.containsMouse && !dragArea.isDragging
                        text: {
                            var s = marker.sec
                            var m = Math.floor(s / 60)
                            var ss = Math.floor(s % 60)
                            return marker.chapterTitle + "\n"
                                 + m + ":" + ("0" + ss).slice(-2)
                        }
                        delay: 500
                    }
                }
            }
        }
    }

    // ── Menú contextual de capítulo ─────────────────────────────────
    Platform.Menu {
        id: chapterContextMenu
        property int chapterIdx: -1

        Platform.MenuItem {
            text: qsTr("Renombrar capítulo")
            onTriggered: {
                renameField.text = chapterModel.chapterTitle(chapterContextMenu.chapterIdx)
                renamePopup.chapterIdx = chapterContextMenu.chapterIdx
                renamePopup.open()
                renameField.forceActiveFocus()
                renameField.selectAll()
            }
        }
        Platform.MenuItem {
            text: qsTr("Ir al capítulo")
            onTriggered: {
                AudioEngine.seekTime(chapterModel.chapterStartSec(chapterContextMenu.chapterIdx))
            }
        }
        Platform.MenuSeparator {}
        Platform.MenuItem {
            text: qsTr("Eliminar capítulo")
            onTriggered: UndoManager.removeChapter(chapterContextMenu.chapterIdx)
        }
    }

    // ── Popup de renombrar ──────────────────────────────────────────
    Popup {
        id: renamePopup
        modal: true
        property int chapterIdx: -1
        width: 260; height: 80
        x: (markers.width - width) / 2
        y: 30

        background: Rectangle { color: "#2a2d32"; radius: 6; border.color: "#555" }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 8

            TextField {
                id: renameField
                Layout.fillWidth: true
                color: "#ecf0f1"
                font.pixelSize: 13
                onAccepted: {
                    UndoManager.renameChapter(renamePopup.chapterIdx, text)
                    renamePopup.close()
                }
                background: Rectangle { color: "#1f2226"; radius: 4; border.color: "#555" }
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button {
                    text: qsTr("Cancelar")
                    onClicked: renamePopup.close()
                }
                Button {
                    text: qsTr("Guardar")
                    highlighted: true
                    onClicked: {
                        UndoManager.renameChapter(renamePopup.chapterIdx, renameField.text)
                        renamePopup.close()
                    }
                }
            }
        }
    }
}
