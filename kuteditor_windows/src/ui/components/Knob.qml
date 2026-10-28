import QtQuick
import QtQuick.Controls

/**
 * Knob: control rotativo tipo potenciómetro.
 *
 * Propiedades:
 *   value     - valor actual (entre from y to)
 *   from      - valor mínimo
 *   to        - valor máximo
 *   resetValue - valor al hacer doble clic (default: 0)
 *   size      - diámetro en px (default: 36)
 *   label     - texto mostrado debajo
 *   accentColor - color del arco activo
 *
 * Interacción:
 *   - Rueda del mouse: ajusta valor
 *   - Arrastrar verticalmente: ajusta valor
 *   - Doble clic: resetea a resetValue
 *
 * Señales:
 *   moved() - emitida cuando el usuario cambia el valor
 */
Item {
    id: knob
    width: size
    height: size + (labelText.visible ? 16 : 0)

    property real value: 0
    property real from: -1.0
    property real to: 1.0
    property real resetValue: 0.0
    property real stepSize: 0.01
    property int size: 36
    property string label: ""
    property color accentColor: "#3498db"
    property string valueText: ""  // override del texto central

    signal moved()

    // Ángulo: de 225° (min) a -45° (max) = rango de 270°
    readonly property real _startAngle: 225
    readonly property real _endAngle: -45
    readonly property real _range: to - from
    readonly property real _normalized: _range > 0 ? (value - from) / _range : 0
    readonly property real _angle: _startAngle + (_endAngle - _startAngle) * _normalized

    // --- Fondo del knob ---
    Rectangle {
        id: knobBody
        width: knob.size
        height: knob.size
        radius: knob.size / 2
        color: "#2a2d32"
        border.color: knobMA.containsMouse || knobMA.pressed ? knob.accentColor : "#4a4d52"
        border.width: 1.5

        // --- Arco de valor (canvas) ---
        Canvas {
            id: arcCanvas
            anchors.fill: parent
            onPaint: {
                const ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                const cx = width / 2
                const cy = height / 2
                const r = (Math.min(width, height) / 2) - 3

                // Arco de fondo (track)
                ctx.beginPath()
                ctx.arc(cx, cy, r, toRad(knob._startAngle), toRad(knob._endAngle), true)
                ctx.strokeStyle = "#1a1d21"
                ctx.lineWidth = 3
                ctx.lineCap = "round"
                ctx.stroke()

                // Arco activo
                if (knob._range > 0) {
                    ctx.beginPath()
                    ctx.arc(cx, cy, r, toRad(knob._startAngle), toRad(knob._angle), true)
                    ctx.strokeStyle = knob.accentColor
                    ctx.lineWidth = 3
                    ctx.lineCap = "round"
                    ctx.stroke()
                }

                // Indicador (línea desde centro hacia el borde)
                const indicatorLen = r - 4
                const innerR = 4
                const rad = toRad(knob._angle)
                ctx.beginPath()
                ctx.moveTo(cx + Math.cos(rad) * innerR, cy - Math.sin(rad) * innerR)
                ctx.lineTo(cx + Math.cos(rad) * indicatorLen, cy - Math.sin(rad) * indicatorLen)
                ctx.strokeStyle = "#ffffff"
                ctx.lineWidth = 2
                ctx.lineCap = "round"
                ctx.stroke()
            }

            function toRad(deg) { return deg * Math.PI / 180 }

            Connections {
                target: knob
                function onValueChanged() { arcCanvas.requestPaint() }
            }
            Component.onCompleted: requestPaint()
        }

        // --- Texto central (valor) ---
        Label {
            anchors.centerIn: parent
            text: knob.valueText !== "" ? knob.valueText : knob.value.toFixed(1)
            color: "#cccccc"
            font.pixelSize: Math.max(8, knob.size / 4.5)
            visible: knob.size >= 32
        }
    }

    // --- Label debajo ---
    Label {
        id: labelText
        anchors.top: knobBody.bottom
        anchors.topMargin: 2
        anchors.horizontalCenter: knobBody.horizontalCenter
        text: knob.label
        color: "#888"
        font.pixelSize: 9
        visible: knob.label !== ""
    }

    // --- Interacción ---
    MouseArea {
        id: knobMA
        anchors.fill: knobBody
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton

        property real dragStartY: 0
        property real dragStartValue: 0

        onPressed: (mouse) => {
            dragStartY = mouse.y
            dragStartValue = knob.value
        }

        onPositionChanged: (mouse) => {
            if (!pressed) return
            const dy = dragStartY - mouse.y  // arriba = aumentar
            const sensitivity = knob._range / (knob.size * 3)
            let newVal = dragStartValue + dy * sensitivity
            newVal = Math.max(knob.from, Math.min(knob.to, newVal))
            // Snap a stepSize
            newVal = Math.round(newVal / knob.stepSize) * knob.stepSize
            if (newVal !== knob.value) {
                knob.value = newVal
                knob.moved()
            }
        }

        onDoubleClicked: {
            knob.value = knob.resetValue
            knob.moved()
        }

        onWheel: (wheel) => {
            const delta = wheel.angleDelta.y > 0 ? knob.stepSize : -knob.stepSize
            let newVal = knob.value + delta * 5
            newVal = Math.max(knob.from, Math.min(knob.to, newVal))
            newVal = Math.round(newVal / knob.stepSize) * knob.stepSize
            if (newVal !== knob.value) {
                knob.value = newVal
                knob.moved()
            }
        }
    }
}
