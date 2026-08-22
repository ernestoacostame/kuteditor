import QtQuick
import QtQuick.Controls

/**
 * TruncateSilencePanel: Interfaz unificada de Truncar Silencio
 */
Column {
    id: silencePanel
    spacing: 12
    width: parent ? parent.width : 400

    property real thresholdDb: -30
    property real minSilenceMs: 500
    property real maxSilenceMs: 500

    signal applyRequested(real threshold, real minSil, real maxSil)

    Row {
        spacing:12
        Label { text: qsTr("Truncar silencio"); color:"#e8eaed"; font.bold:true; font.pixelSize:13; anchors.verticalCenter:parent.verticalCenter }
    }
    Rectangle { width:parent.width; height:1; color:"#2a2d32" }

    // Advertencia + Procesar
    Rectangle {
        width:parent.width; height:36; color:"#241e10"; radius:4; border.color:"#5a3e10"
        Row {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 8
            Label { text: qsTr("⚠  Elimina silencios del clip. Configura y pulsa Procesar."); color:"#d4a017"; font.pixelSize:10; wrapMode:Text.WordWrap; width:parent.width - procBtn.width - 16; anchors.verticalCenter:procBtn.verticalCenter }
            Button {
                id:procBtn; text: qsTr("Procesar"); width:80; height:24
                onClicked: silencePanel.applyRequested(silencePanel.thresholdDb, silencePanel.minSilenceMs, silencePanel.maxSilenceMs)
                background: Rectangle { color:parent.pressed?"#7a5a00":"#5a4200"; radius:3; border.color:"#d4a017"; border.width:1 }
                contentItem: Label { anchors.centerIn: parent; text:parent.text; color:"#f0c040"; font.pixelSize:10; font.bold:true; horizontalAlignment:Text.AlignHCenter; verticalAlignment:Text.AlignVCenter }
            }
        }
    }

    // Knobs horizontales
    Row {
        spacing: 8
        Repeater {
            model: [
                {prop:"thresholdDb",  label:"UMBRAL",    from:-80, to:0,    unit:" dB", color:"#95a5a6", val: silencePanel.thresholdDb},
                {prop:"minSilenceMs", label:"MÍN. SIL.", from:100, to:5000, unit:" ms", color:"#7f8c8d", val: silencePanel.minSilenceMs},
                {prop:"maxSilenceMs", label:"MÁX. SIL.", from:0,   to:2000, unit:" ms", color:"#626567", val: silencePanel.maxSilenceMs}
            ]
            Item {
                width: 82; height: 88
                property var kd: modelData
                Label {
                    anchors.top: parent.top
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: Number(kd.val).toFixed(0) + (kd.unit || "")
                    color: kd.color
                    font.pixelSize: 9; font.family: "Menlo"
                    horizontalAlignment: Text.AlignHCenter
                    width: parent.width
                }
                MetalKnob {
                    anchors.top: parent.top
                    anchors.topMargin: 14
                    anchors.horizontalCenter: parent.horizontalCenter
                    size: 52; from: kd.from; to: kd.to; dotColor: kd.color; label: ""
                    value: kd.val
                    onMoved: (v) => { silencePanel[kd.prop] = v }
                }
                Label {
                    anchors.bottom: parent.bottom
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: kd.label; color: "#555"
                    font.pixelSize: 8; font.bold: true; font.letterSpacing: 0.5
                    horizontalAlignment: Text.AlignHCenter
                    width: parent.width
                }
            }
        }
    }
}
