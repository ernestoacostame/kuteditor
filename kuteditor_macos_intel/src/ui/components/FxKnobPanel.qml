import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

/**
 * FxKnobPanel — Panel horizontal de knobs para efectos de audio.
 *
 * Propiedades:
 *   fx          - QObject del efecto (del fxChain o AudioEngine.xxx)
 *   fxName      - Nombre legible del efecto
 *   knobs       - Lista de knobs: [{prop, label, from, to, unit, color, decimals, isBool}]
 *   accentColor - Color del badge ON/OFF
 *   showHeader  - Mostrar el header con nombre + toggle (default: true)
 *
 * Layout: knobs dispuestos horizontalmente en una fila Flow.
 * Cada knob tiene su label debajo y el valor dinámico encima.
 */
Item {
    id: fxPanel
    width: parent ? parent.width : 460
    height: mainCol.implicitHeight + 20

    property var    fx:          null
    property string fxName:      ""
    property var    knobs:       []
    property color  accentColor: "#2ecc71"
    property bool   showHeader:  true

    // ── Señal para efectos que necesitan un "Aplicar" manual ────────────────
    signal applyRequested()

    Column {
        id: mainCol
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
        spacing: 14

        // ── Header ──────────────────────────────────────────────────────────
        Row {
            visible: fxPanel.showHeader
            spacing: 12

            // Badge ON/OFF
            Rectangle {
                id: onOffBadge
                width: 48; height: 22; radius: 4
                color: fx && fx.enabled
                    ? Qt.rgba(fxPanel.accentColor.r, fxPanel.accentColor.g, fxPanel.accentColor.b, 0.22)
                    : Qt.rgba(1, 1, 1, 0.06)
                border.color: fx && fx.enabled ? fxPanel.accentColor : "#484e56"
                border.width: 1

                Label {
                    anchors.centerIn: parent
                    text: fx && fx.enabled ? "ON" : "OFF"
                    color: fx && fx.enabled ? fxPanel.accentColor : "#666"
                    font.bold: true; font.pixelSize: 10
                    font.letterSpacing: 1
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: { if (fx) fx.setEnabled(!fx.enabled) }
                }
            }

            Label {
                text: fxPanel.fxName
                color: "#e8eaed"
                font.bold: true
                font.pixelSize: 13
                anchors.verticalCenter: parent.children[0].verticalCenter
            }
        }

        // ── Fila horizontal de knobs ─────────────────────────────────────────
        Row {
            id: knobsRow
            spacing: 4
            visible: fxPanel.knobs.some(function(k) { return !k.isBool })

            Repeater {
                model: fxPanel.knobs.filter(function(k) { return !k.isBool })

                Item {
                    width: 84
                    height: 94

                    property var kd: modelData

                    // Valor actual encima del knob
                    Label {
                        id: valueLabel
                        anchors.top: parent.top
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: {
                            if (!fx) return "—"
                            var v = fx[kd.prop]
                            if (v === undefined || v === null) return "—"
                            var dec = kd.decimals !== undefined ? kd.decimals : 1
                            return Number(v).toFixed(dec) + (kd.unit || "")
                        }
                        color: kd.color || "#aaa"
                        font.pixelSize: 9
                        font.family: "Menlo"
                        horizontalAlignment: Text.AlignHCenter
                        width: parent.width
                    }

                    // Knob
                    MetalKnob {
                        id: theKnob
                        anchors.top: valueLabel.bottom
                        anchors.topMargin: 2
                        anchors.horizontalCenter: parent.horizontalCenter
                        size: 56
                        from: kd.from !== undefined ? kd.from : 0
                        to:   kd.to   !== undefined ? kd.to   : 1
                        // defaultValue: usa el explícito del config, si no el metapunto del rango
                        defaultValue: kd.defaultValue !== undefined ? kd.defaultValue
                                    : ((kd.from !== undefined && kd.to !== undefined) ? (kd.from + kd.to) / 2.0 : 0)
                        dotColor: kd.color || "#3498db"
                        label: ""   // el valor lo mostramos nosotros arriba
                        value: {
                            if (!fx) return 0
                            var v = fx[kd.prop]
                            return (v !== undefined && v !== null) ? Number(v) : 0
                        }
                        onMoved: (newValue) => {
                            if (!fx) return
                            // Auto-activar el efecto si el usuario toca un knob
                            if (!fx.enabled) fx.setEnabled(true)
                            var setter = "set" + kd.prop.charAt(0).toUpperCase() + kd.prop.slice(1)
                            if (typeof fx[setter] === "function") fx[setter](newValue)
                        }
                    }

                    // Label del parámetro debajo del knob
                    Label {
                        anchors.top: theKnob.bottom
                        anchors.topMargin: 4
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: kd.label || ""
                        color: "#666"
                        font.pixelSize: 8
                        font.bold: true
                        font.letterSpacing: 0.8
                        horizontalAlignment: Text.AlignHCenter
                        width: parent.width
                    }
                }
            }
        }

        // ── Switches booleanos (debajo si los hay) ───────────────────────────
        Column {
            spacing: 6
            visible: fxPanel.knobs.some(function(k) { return !!k.isBool })
            width: parent.width

            Repeater {
                model: fxPanel.knobs.filter(function(k) { return !!k.isBool })

                Row {
                    spacing: 10; width: mainCol.width
                    property var kd: modelData

                    Switch {
                        id: boolSwitch
                        checked: {
                            if (!fx || kd.prop === undefined) return false
                            var v = fx[kd.prop]
                            return v !== undefined ? !!v : false
                        }
                        onToggled: {
                            if (!fx) return
                            var setter = "set" + kd.prop.charAt(0).toUpperCase() + kd.prop.slice(1)
                            if (typeof fx[setter] === "function") fx[setter](checked)
                        }
                        implicitHeight: 24
                    }
                    Label {
                        text: kd.label || ""
                        color: boolSwitch.checked ? "#e8eaed" : "#777"
                        font.pixelSize: 11
                        anchors.verticalCenter: boolSwitch.verticalCenter
                    }
                }
            }
        }

        // ── GR Meter (efectos dinámicos) ──────────────────────────────────────
        Rectangle {
            visible: fx && typeof fx.gainReduction !== "undefined" && fx.gainReduction < -0.1
            width: parent.width; height: 16
            color: "#111"; radius: 3; border.color: "#2a2d32"

            Rectangle {
                anchors { left: parent.left; top: parent.top; bottom: parent.bottom; margins: 2 }
                width: fx ? Math.min(parent.width - 4,
                    (parent.width - 4) * Math.abs(fx.gainReduction) / 20) : 0
                color: "#e74c3c"; radius: 2
                Behavior on width { NumberAnimation { duration: 60 } }
            }
            Label {
                anchors.centerIn: parent
                text: fx ? "GR  " + fx.gainReduction.toFixed(1) + " dB" : ""
                color: "#888"; font.pixelSize: 8; font.family: "Menlo"
            }
        }
    }
}
