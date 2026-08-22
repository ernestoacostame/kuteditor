import QtQuick
import QtQuick.Controls

/**
 * NoiseGatePanel: Interfaz unificada de Noise Gate (estilo Hindenburg)
 * Utiliza un único ReductionDial grande para controlar el umbral (Threshold).
 */
Column {
    id: gatePanel
    spacing: 12
    width: parent ? parent.width : 300

    property var fx: null

    // === Header + Toggle ===
    Row {
        spacing: 10
        Label {
            text: qsTr("Puerta de Ruido")
            color: "#fff"; font.bold: true; font.pixelSize: 14
            anchors.verticalCenter: parent.children[1].verticalCenter
        }
        Button {
            width: 48; height: 26
            text: fx && fx.enabled ? "on" : "off"
            onClicked: {
                if (fx) fx.setEnabled(!fx.enabled)
            }
            background: Rectangle {
                radius: 3
                color: fx && fx.enabled ? Qt.rgba(0.9, 0.49, 0.13, 0.3) : Qt.rgba(1,1,1,0.08)
                border.color: fx && fx.enabled ? "#e67e22" : "#555"
            }
            contentItem: Label {
                anchors.centerIn: parent
                text: parent.text
                color: fx && fx.enabled ? "#e67e22" : "#888"
                font.bold: true; font.pixelSize: 11
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
    }

    // === METER DE REDUCCIÓN ===
    Rectangle {
        width: parent.width; height: 16
        color: "#111"; radius: 3; border.color: "#2a2d32"
        visible: fx !== null

        Rectangle {
            anchors { left: parent.left; top: parent.top; bottom: parent.bottom; margins: 2 }
            width: fx ? Math.min(parent.width - 4, (parent.width - 4) * Math.abs(fx.gainReduction) / 40.0) : 0
            color: "#e67e22"; radius: 2
            Behavior on width { NumberAnimation { duration: 60 } }
        }
        Label {
            anchors.centerIn: parent
            text: fx && fx.gainReduction < -0.1 ? "CERRADO" : "ABIERTO"
            color: fx && fx.gainReduction < -0.1 ? "#fff" : "#555"
            font.pixelSize: 9; font.family: "monospace"; font.bold: true
        }
    }

    // === DIAL CENTRAL (THRESHOLD) ===
    Item {
        width: parent.width
        height: 160

        BigMetalKnob {
            id: gateDial
            size: 110
            from: -80
            to: 0
            value: fx ? fx.thresholdDb : -40
            activeColor: "#e67e22"
            anchors.centerIn: parent
            onMoved: (val) => {
                if (fx) {
                    if (!fx.enabled) fx.setEnabled(true)
                    fx.setThresholdDb(val)
                }
            }

            Label {
                text: gateDial.value.toFixed(1) + " dB"
                anchors.top: parent.bottom
                anchors.topMargin: 2
                anchors.horizontalCenter: parent.horizontalCenter
                color: "#e67e22"
                font.pixelSize: 12
                font.bold: true
            }
        }
    }
}
