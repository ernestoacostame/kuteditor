import QtQuick
import QtQuick.Controls

/**
 * ReductionDial: Dial rotativo estilo Hindenburg para reducción de ruido.
 */
Item {
    id: dial
    width: size
    height: size + 20

    property real value: 12
    property real from: 0
    property real to: 40
    property int size: 100
    property string label: "Reduction"

    signal moved()

    readonly property real _normalized: (value - from) / (to - from)
    readonly property real _rotation: -135 + (270 * _normalized)

    // Cuerpo exterior (anillo de puntos)
    Item {
        width: parent.width
        height: dial.size
        
        // Puntos indicadores
        Repeater {
            model: 13
            delegate: Rectangle {
                readonly property real angle: -135 + (270 * (index / 12))
                x: parent.width/2 + (dial.size/2 - 6) * Math.sin(angle * Math.PI / 180) - width/2
                y: parent.height/2 - (dial.size/2 - 6) * Math.cos(angle * Math.PI / 180) - height/2
                width: 4; height: 4; radius: 2
                color: (index / 12) <= dial._normalized ? "#2ecc71" : "#444"
                
                Behavior on color { ColorAnimation { duration: 200 } }
            }
        }

        // El Knob central
        Rectangle {
            id: knobBody
            width: dial.size * 0.7
            height: width
            radius: width / 2
            anchors.centerIn: parent
            
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#555" }
                GradientStop { position: 1.0; color: "#222" }
            }
            
            border.color: "#111"
            border.width: 2

            // Sombra interna/brillo para efecto metálico
            Rectangle {
                anchors.fill: parent
                anchors.margins: 2
                radius: width/2
                color: "transparent"
                border.color: "#ffffff11"
                border.width: 1
            }

            // Puntero
            Rectangle {
                width: 3; height: parent.height / 2 - 6
                color: "#111"
                radius: 1.5
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.verticalCenter
                transformOrigin: Item.Bottom
                rotation: dial._rotation
                
                // Efecto de relieve en el puntero
                Rectangle {
                    width: 1; height: parent.height
                    color: "#ffffff22"
                    anchors.left: parent.left
                }
            }
        }
    }

    Label {
        text: dial.label
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        color: "#aaa"
        font.pixelSize: 12
    }

    MouseArea {
        anchors.fill: parent
        property real dragStartY: 0
        property real dragStartValue: 0

        onPressed: (mouse) => {
            dragStartY = mouse.y
            dragStartValue = dial.value
        }

        onPositionChanged: (mouse) => {
            if (!pressed) return
            const dy = dragStartY - mouse.y
            const sensitivity = (dial.to - dial.from) / 150
            let newVal = dragStartValue + dy * sensitivity
            newVal = Math.max(dial.from, Math.min(dial.to, newVal))
            if (newVal !== dial.value) {
                dial.value = newVal
                dial.moved()
            }
        }

        onWheel: (wheel) => {
            let newVal = dial.value + (wheel.angleDelta.y > 0 ? 1 : -1)
            newVal = Math.max(dial.from, Math.min(dial.to, newVal))
            if (newVal !== dial.value) {
                dial.value = newVal
                dial.moved()
            }
        }
    }
}
