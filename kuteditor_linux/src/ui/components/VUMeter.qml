import QtQuick
import QtQuick.Layouts

Rectangle {
    id: vuMeter
    property real leftLevel:  0.0
    property real rightLevel: 0.0

    color: "#1a1d21"
    border.color: "#3c4146"
    border.width: 1
    radius: 3

    // Marcas de dB a la derecha del medidor
    readonly property var dbMarks: [0, -3, -6, -12, -18, -24, -36, -48]

    function dbToY(db) {
        // Convertir dB a posición Y (0 dB = arriba, -48 dB = abajo)
        // level 1.0 = 0 dB, level ~0.004 = -48 dB
        const fraction = Math.max(0, (db + 48) / 48)
        return metersArea.height * (1.0 - fraction)
    }

    RowLayout {
        id: metersArea
        anchors.fill: parent
        anchors.margins: 2
        anchors.rightMargin: 22
        spacing: 2

        // Canal izquierdo
        Rectangle {
            Layout.fillHeight: true
            Layout.fillWidth: true
            color: "#111"
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: parent.height * Math.min(1.0, vuMeter.leftLevel)
                color: height > parent.height * 0.9 ? "#e74c3c"
                     : height > parent.height * 0.7 ? "#f39c12"
                     : "#2ecc71"
            }
        }
        // Canal derecho
        Rectangle {
            Layout.fillHeight: true
            Layout.fillWidth: true
            color: "#111"
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: parent.height * Math.min(1.0, vuMeter.rightLevel)
                color: height > parent.height * 0.9 ? "#e74c3c"
                     : height > parent.height * 0.7 ? "#f39c12"
                     : "#2ecc71"
            }
        }
    }

    // Escala de dB
    Repeater {
        model: vuMeter.dbMarks
        Item {
            anchors.right: parent.right
            anchors.rightMargin: 2
            y: vuMeter.dbToY(modelData) + metersArea.anchors.margins - 5
            width: 20
            height: 10

            Rectangle {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: 4
                height: 1
                color: "#666"
            }

            Text {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: modelData
                color: "#888"
                font.pixelSize: 7
            }
        }
    }
}
