import QtQuick
import QtQuick.Controls
import Qt.labs.platform as Platform

/**
 * Ruler: regla horizontal sobre las pistas (estilo Reaper / Hindenburg).
 *
 * Responsabilidades:
 *  - Mostrar marcas de tiempo alineadas con la línea temporal de las pistas.
 *  - Permitir arrastrar para crear una selección temporal (appRoot.selStart/selEnd).
 *  - Click simple → mueve el cursor (playhead) a esa posición.
 *  - Muestra el playhead como triangulito en la parte inferior.
 *
 * El tiempo total mostrado y el zoom se leen de appRoot (estado global).
 */
Rectangle {
    id: root
    height: 28
    color: "#1f2226"

    required property var appRoot
    property real leftOffsetPx: 340   // inicio de la zona de clips

    // Click en cualquier área libre de la regla deselecciona las pistas
    MouseArea {
        anchors.fill: parent
        z: -1
        onClicked: {
            appRoot.clearTrackSelection()
        }
    }

    // Divisor inferior
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: "#3c4146"
    }

    // Margen izquierdo (alineado con la cabecera de pistas): coincide con
    // el espacio ocupado por el panel de controles.
    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: root.leftOffsetPx
        color: "transparent"

        Label {
            anchors.centerIn: parent
            text: qsTr("Selección")
            color: "#7a8389"
            font.pixelSize: 10
            font.italic: true
        }
    }

    // Área real de la regla (zona alineada con clipArea de cada pista)
    Item {
        id: rulerArea
        anchors.left: parent.left
        anchors.leftMargin: root.leftOffsetPx
        anchors.right: parent.right
        // Coincidir exactamente con el rightMargin de clipArea (6 px) para
        // que ambos tengan el mismo ancho efectivo. Si difiere, al aumentar
        // el zoom la diferencia se multiplica y el cursor y la flecha se
        // desalinean proporcionalmente.
        anchors.rightMargin: 6
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        clip: true

        // Ancho virtual según zoom (igual que clipArea en cada track)
        readonly property real displayDuration: appRoot.timelineDisplayDuration
        readonly property real innerWidth: width * appRoot.timelineZoom
        readonly property real innerX: -appRoot.timelineScrollX

        // Selección (overlay)
        Rectangle {
            visible: appRoot.hasSelection
            color: Qt.rgba(0.91, 0.3, 0.24, 0.28)
            border.color: "#e74c3c"
            border.width: 1
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            x: appRoot.secToViewX(appRoot.selStart)
            width: {
                const x1 = appRoot.secToViewX(appRoot.selStart)
                const x2 = appRoot.secToViewX(appRoot.selEnd)
                return Math.max(1, x2 - x1)
            }
        }

        // Marcas de tiempo: intervalo adaptable al zoom
        Item {
            id: marks
            anchors.fill: parent

            // Segundos entre marcas mayores, dependiendo de cuántos píxeles hay por segundo
            readonly property real pxPerSec: {
                const spp = appRoot.secPerPixel
                if (spp <= 0) return 0
                return 1.0 / spp
            }
            // Elegir intervalo que deje al menos 60 px entre marcas
            readonly property real majorInterval: {
                const px = pxPerSec
                if (px <= 0) return 30
                const candidates = [1, 2, 5, 10, 15, 30, 60, 120, 300, 600]
                for (const c of candidates) {
                    if (c * px >= 60) return c
                }
                return 600
            }

            Repeater {
                model: {
                    const total = rulerArea.displayDuration
                    const n = Math.ceil(total / marks.majorInterval) + 1
                    return n
                }
                delegate: Item {
                    required property int index
                    property real tSec: index * marks.majorInterval
                    x: appRoot.secToViewX(tSec)
                    width: 1
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom

                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: 1
                        height: 8
                        color: "#5a6368"
                    }
                    Label {
                        anchors.left: parent.left
                        anchors.leftMargin: 3
                        anchors.top: parent.top
                        anchors.topMargin: 3
                        text: {
                            const t = parent.tSec
                            const m = Math.floor(t / 60)
                            const s = Math.floor(t % 60)
                            return m + ":" + (s < 10 ? "0" + s : s)
                        }
                        color: "#a0a8ac"
                        font.pixelSize: 10
                        font.family: "monospace"
                    }
                }
            }
        }

        // Input: click = seek, drag = selección temporal
        MouseArea {
            id: mouseZone
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            cursorShape: Qt.IBeamCursor
            preventStealing: true
            hoverEnabled: true

            property real dragStartSec: -1
            property bool dragging: false

            function xToSec(mx) {
                return appRoot.viewXToSec(mx)
            }

            onPressed: (mouse) => {
                if (mouse.button !== Qt.LeftButton) return
                const sec = xToSec(mouse.x)
                if ((mouse.modifiers & Qt.ShiftModifier) && appRoot.hasSelection) {
                    // Shift+click: expandir la selección existente
                    const distToStart = Math.abs(sec - appRoot.selStart)
                    const distToEnd = Math.abs(sec - appRoot.selEnd)
                    if (distToStart < distToEnd) {
                        dragStartSec = appRoot.selEnd
                        appRoot.setSelection(sec, appRoot.selEnd)
                    } else {
                        dragStartSec = appRoot.selStart
                        appRoot.setSelection(appRoot.selStart, sec)
                    }
                    dragging = true
                } else if ((mouse.modifiers & Qt.ShiftModifier) && !appRoot.hasSelection) {
                    // Shift+click sin selección: crear desde el cursor hasta el click
                    dragStartSec = AudioEngine.currentTime
                    appRoot.setSelection(dragStartSec, sec)
                    dragging = true
                } else {
                    dragStartSec = sec
                    dragging = false
                    appRoot.clearSelection()
                    appRoot.clearTrackSelection()
                    appRoot.clearClipSelection()
                }
            }
            onPositionChanged: (mouse) => {
                if (!pressed || dragStartSec < 0) return
                const cur = xToSec(mouse.x)
                if (!dragging && Math.abs(cur - dragStartSec) > 0.05) dragging = true
                if (dragging) appRoot.setSelection(dragStartSec, cur)
            }
            onReleased: (mouse) => {
                if (mouse.button === Qt.RightButton) {
                    rulerContextMenu.clickSec = xToSec(mouse.x)
                    rulerContextMenu.open()
                    return
                }
                if (mouse.button !== Qt.LeftButton) return
                if (!dragging) {
                    // Click sin drag: mover cursor
                    let targetSec = dragStartSec
                    // Magnetic snap: search within 15 pixels
                    const thresholdSec = 15 * appRoot.secPerPixel
                    targetSec = TrackModel.findNearestClipEdge(targetSec, thresholdSec)
                    AudioEngine.seekTime(targetSec)
                    // Seek = reactivar auto-scroll
                    appRoot.isUserInteracting = false
                }
                dragStartSec = -1
                dragging = false
            }
        }

        Platform.Menu {
            id: rulerContextMenu
            property double clickSec: 0

            Platform.MenuItem {
                text: qsTr("Añadir capítulo aquí")
                onTriggered: {
                    // Snap al borde de clip más cercano (15px)
                    var sec = rulerContextMenu.clickSec
                    var threshSec = 15 * appRoot.secPerPixel
                    sec = ChapterModel.snapToClipEdgeSec(sec, threshSec)
                    UndoManager.addChapterAtSec(sec)
                }
            }
            Platform.MenuItem {
                text: qsTr("Añadir capítulo en cursor")
                onTriggered: {
                    UndoManager.addChapterAtSec(AudioEngine.currentTime)
                }
            }
            Platform.MenuSeparator {}
            Platform.MenuItem {
                text: qsTr("Editor de capítulos…")
                onTriggered: appRoot.openChapterEditor()
            }
        }
    }
}
