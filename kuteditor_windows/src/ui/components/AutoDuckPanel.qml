import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

/**
 * AutoDuckPanel: Interfaz unificada de Auto Duck
 */
Column {
    id: duckPanel
    spacing: 12
    width: parent ? parent.width : 400

    property var fx: null

    // === Header ===
    Row {
        spacing: 12
        Rectangle {
            width: 48; height: 22; radius: 4
            color: fx && fx.enabled ? Qt.rgba(0.9,0.6,0.1,0.22) : Qt.rgba(1,1,1,0.06)
            border.color: fx && fx.enabled ? "#e67e22" : "#484e56"
            Label { anchors.centerIn:parent; text:fx && fx.enabled ? "ON":"OFF"; color:fx && fx.enabled ? "#e67e22":"#666"; font.bold:true; font.pixelSize:10; font.letterSpacing:1 }
            MouseArea { anchors.fill:parent; cursorShape:Qt.PointingHandCursor; onClicked:{ if(fx) fx.setEnabled(!fx.enabled) } }
        }
        Label { text:"Auto Duck"; color:"#e8eaed"; font.bold:true; font.pixelSize:13; anchors.verticalCenter:parent.children[0].verticalCenter }
    }
    Rectangle { width:parent.width; height:1; color:"#2a2d32" }

    // === Tip + Auto-detectar ===
    Rectangle {
        width:parent.width; height:36; color:"#1a2030"; radius:4; border.color:"#2a3850"
        Row {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 8
            Label { text:"💡 Asigna pistas manualmente o usa Auto-detectar."; color:"#5d8fd4"; font.pixelSize:9; wrapMode:Text.WordWrap; width:parent.width - autoBtn.width - 16; anchors.verticalCenter:autoBtn.verticalCenter }
            Button {
                id: autoBtn; text:"Auto"; width:50; height:22
                onClicked: {
                    if(!fx) return
                    // Asume que la pista actual es control, y la siguiente es target (si es de pista)
                    // Si es Master, no tiene trackIndex, usamos valores fijos o dejamos al usuario.
                }
                background: Rectangle { color:parent.pressed?"#1a4a7a":"#1e3a5f"; radius:3; border.color:"#2980b9" }
                contentItem: Label { anchors.centerIn: parent; text:parent.text; color:"#5dade2"; font.pixelSize:9; font.bold:true; horizontalAlignment:Text.AlignHCenter; verticalAlignment:Text.AlignVCenter }
            }
        }
    }

    // === Selectores de pista ===
    Row {
        spacing:8; width:parent.width
        Column { width:(parent.width-8)/2; spacing:3
            Label { text:"VOZ (Control)"; color:"#777"; font.pixelSize:8; font.bold:true; font.letterSpacing:0.5 }
            ComboBox {
                width:parent.width; height:26
                // TrackModel is globally available
                model: { var a=["— Ninguna —"]; for(var i=0;i<TrackModel.count;i++){var d=TrackModel.getTrackData(i); a.push("P"+(i+1)+": "+(d.name||"Sin nombre"))} return a }
                currentIndex: { if(!fx) return 0; var v=fx.controlTrack; return (v>=0)?v+1:0 }
                onActivated: { if(fx) fx.setControlTrack(currentIndex-1) }
                background: Rectangle { color:"#1e2228"; radius:3; border.color:"#3a4050" }
                contentItem: Label { anchors.fill: parent; rightPadding: 24; elide: Text.ElideRight; leftPadding:6; text:parent.displayText; color:"#ccc"; font.pixelSize:10; verticalAlignment:Text.AlignVCenter }
            }
        }
        Column { width:(parent.width-8)/2; spacing:3
            Label { text:"MÚSICA (Objetivo)"; color:"#777"; font.pixelSize:8; font.bold:true; font.letterSpacing:0.5 }
            ComboBox {
                width:parent.width; height:26
                model: { var a=["— Ninguna —"]; for(var i=0;i<TrackModel.count;i++){var d=TrackModel.getTrackData(i); a.push("P"+(i+1)+": "+(d.name||"Sin nombre"))} return a }
                currentIndex: { if(!fx) return 0; var v=fx.targetTrack; return (v>=0)?v+1:0 }
                onActivated: { if(fx) fx.setTargetTrack(currentIndex-1) }
                background: Rectangle { color:"#1e2228"; radius:3; border.color:"#3a4050" }
                contentItem: Label { anchors.fill: parent; rightPadding: 24; elide: Text.ElideRight; leftPadding:6; text:parent.displayText; color:"#ccc"; font.pixelSize:10; verticalAlignment:Text.AlignVCenter }
            }
        }
    }
    Rectangle { width:parent.width; height:1; color:"#2a2d32" }

    // === Knobs horizontales ===
    Row {
        spacing: 8
        Repeater {
            model: [
                {prop:"thresholdDb",    label:"UMBRAL",  from:-60, to:0,    unit:" dB", color:"#e67e22"},
                {prop:"duckAmountDb",   label:"AMOUNT",  from:-40, to:0,    unit:" dB", color:"#d35400"},
                {prop:"innerFadeDownMs",label:"FADE ↓",  from:1,   to:500,  unit:" ms", color:"#a04000"},
                {prop:"innerFadeUpMs",  label:"FADE ↑",  from:10,  to:3000, unit:" ms", color:"#7e5109"}
            ]
            Item {
                width: 82; height: 88
                property var kd: modelData
                Label {
                    anchors.top: parent.top
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: {
                        if (!fx) return "—"
                        var v = fx[kd.prop]
                        return v !== undefined ? Number(v).toFixed(0) + (kd.unit || "") : "—"
                    }
                    color: kd.color
                    font.pixelSize: 9; font.family: "monospace"
                    horizontalAlignment: Text.AlignHCenter
                    width: parent.width
                }
                MetalKnob {
                    anchors.top: parent.top
                    anchors.topMargin: 14
                    anchors.horizontalCenter: parent.horizontalCenter
                    size: 52; from: kd.from; to: kd.to; dotColor: kd.color; label: ""
                    value: {
                        if (!fx) return 0
                        var v = fx[kd.prop]
                        return v !== undefined ? Number(v) : 0
                    }
                    onMoved: (v) => {
                        if (fx) {
                            var s = "set" + kd.prop.charAt(0).toUpperCase() + kd.prop.slice(1)
                            if (typeof fx[s] === "function") fx[s](v)
                        }
                    }
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
