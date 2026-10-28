import QtQuick
import QtQuick.Controls

/**
 * MetalKnob: A metallic, highly fluid knob matching the Hindenburg style.
 */
Item {
    id: knob
    width: size
    height: size + 24
    
    property real value: 0
    property real from: -12.0
    property real to: 12.0
    property int size: 80
    property string label: ""
    property color dotColor: "red"
    // Valor al que vuelve con doble clic. Si no se especifica,
    // usa el centro del rango (comportamiento genérico razonable).
    property real defaultValue: (from + to) / 2.0
    property bool _hasExplicitDefault: false

    // Internal dragged value to avoid destroying declarative bindings
    property real _internalValue: value
    property bool _dragging: false

    onValueChanged: {
        if (!_dragging) {
            _internalValue = value
        }
    }

    on_InternalValueChanged: {
        if (arcCanvas) {
            arcCanvas.requestPaint()
        }
    }

    signal moved(real newValue)

    readonly property real _startAngle: 225
    readonly property real _endAngle: -45
    readonly property real _range: to - from
    readonly property real _normalized: _range > 0 ? (_internalValue - from) / _range : 0
    readonly property real _angle: _startAngle + (_endAngle - _startAngle) * _normalized

    // Dot above the knob
    Rectangle {
        width: 4
        height: 4
        radius: 2
        color: knob.dotColor
        anchors.horizontalCenter: parent.horizontalCenter
        y: 4
        
        // Glow effect
        Rectangle {
            anchors.centerIn: parent
            width: 8
            height: 8
            radius: 4
            color: knob.dotColor
            opacity: 0.3
        }
    }

    // Metal body
    Rectangle {
        id: knobBody
        width: knob.size
        height: knob.size
        radius: knob.size / 2
        anchors.horizontalCenter: parent.horizontalCenter
        y: 14
        color: "#1a1d21"
        
        // Base shadow
        Rectangle {
            anchors.centerIn: parent
            width: parent.width + 4
            height: parent.height + 4
            radius: width / 2
            color: "transparent"
            border.color: "#000000"
            border.width: 2
            opacity: 0.5
            z: -1
        }

        Canvas {
            id: arcCanvas
            anchors.fill: parent
            onPaint: {
                const ctx = getContext("2d")
                const w = width, h = height
                ctx.clearRect(0, 0, w, h)
                const cx = w / 2
                const cy = h / 2
                const r = (Math.min(w, h) / 2) - 2

                // Metallic gradient
                var grad = ctx.createLinearGradient(0, 0, w, h)
                grad.addColorStop(0, "#e0e0e0")
                grad.addColorStop(0.3, "#a0a0a0")
                grad.addColorStop(0.5, "#808080")
                grad.addColorStop(0.7, "#a0a0a0")
                grad.addColorStop(1, "#505050")

                ctx.beginPath()
                ctx.arc(cx, cy, r, 0, Math.PI * 2)
                ctx.fillStyle = grad
                ctx.fill()
                
                // Outer ring
                ctx.strokeStyle = "#333"
                ctx.lineWidth = 1
                ctx.stroke()

                // Indicator line
                const indicatorLen = r - 6
                const innerR = 4
                const rad = knob._angle * Math.PI / 180
                ctx.beginPath()
                ctx.moveTo(cx + Math.cos(rad) * innerR, cy - Math.sin(rad) * innerR)
                ctx.lineTo(cx + Math.cos(rad) * indicatorLen, cy - Math.sin(rad) * indicatorLen)
                ctx.strokeStyle = "#000000"
                ctx.lineWidth = 3
                ctx.lineCap = "round"
                ctx.stroke()
            }
            Component.onCompleted: requestPaint()
        }
    }

    // Label
    Label {
        anchors.top: knobBody.bottom
        anchors.topMargin: 8
        anchors.horizontalCenter: knobBody.horizontalCenter
        text: knob.label
        color: "#888"
        font.pixelSize: 11
        font.bold: true
    }

    MouseArea {
        anchors.fill: knobBody
        hoverEnabled: true
        preventStealing: true

        property real dragStartY: 0
        property real dragStartValue: 0

        onPressed: (mouse) => {
            knob._dragging = true
            dragStartY = mouse.y
            dragStartValue = knob._internalValue
        }

        onPositionChanged: (mouse) => {
            if (!pressed) return
            const dy = dragStartY - mouse.y
            // 150 pixels for full range
            const sensitivity = knob._range / 150.0
            let newVal = dragStartValue + dy * sensitivity
            newVal = Math.max(knob.from, Math.min(knob.to, newVal))
            
            if (newVal !== knob._internalValue) {
                knob._internalValue = newVal
                knob.moved(newVal)
            }
        }

        onReleased: {
            knob._dragging = false
            // Snap to 0 on double click later, or just let it be smooth
        }

        onDoubleClicked: {
            var resetVal = knob.defaultValue
            knob._internalValue = resetVal
            knob.moved(resetVal)
            arcCanvas.requestPaint()
            // Flash visual breve para confirmar el reset
            resetFlash.restart()
        }
    }

    // Flash de confirmación de reset
    Rectangle {
        id: resetOverlay
        anchors.fill: knobBody
        anchors.topMargin: 14
        radius: knob.size / 2
        color: "white"
        opacity: 0
        NumberAnimation {
            id: resetFlash
            target: resetOverlay
            property: "opacity"
            from: 0.35
            to: 0
            duration: 350
            easing.type: Easing.OutCubic
        }
    }
}
