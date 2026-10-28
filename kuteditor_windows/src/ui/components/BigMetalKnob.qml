import QtQuick
import QtQuick.Controls

Item {
    id: root
    width: size
    height: size

    property real value: 0.0
    property real from: 0.0
    property real to: 1.0
    property int size: 110
    property color activeColor: "#aaa"

    signal moved(real val)
    signal doubleClicked()

    Canvas {
        id: trackKnobCanvas
        anchors.fill: parent

        property real normalizedValue: (root.value - root.from) / (root.to - root.from)

        onNormalizedValueChanged: requestPaint()

        onPaint: {
            var ctx = getContext("2d")
            var w = width, h = height
            ctx.clearRect(0, 0, w, h)

            var cx = w / 2, cy = h / 2
            var outerR = Math.min(w, h) / 2 - 4
            var innerR = outerR - 10

            // Fondo oscuro exterior
            ctx.beginPath()
            ctx.arc(cx, cy, outerR, 0, Math.PI * 2)
            ctx.fillStyle = "#1a1c20"
            ctx.fill()
            ctx.strokeStyle = "#555"
            ctx.lineWidth = 1
            ctx.stroke()

            // Puntos indicadores
            var dotR = outerR + 7
            for (var i = 0; i <= 10; i++) {
                var frac = i / 10.0
                var ang = (225 - frac * 270) * Math.PI / 180
                ctx.beginPath()
                ctx.arc(cx + Math.cos(ang) * dotR, cy - Math.sin(ang) * dotR, 2, 0, Math.PI * 2)
                ctx.fillStyle = frac <= normalizedValue ? root.activeColor : "#444"
                ctx.fill()
            }

            // Degradado 3D del Knob
            var knobGrad = ctx.createRadialGradient(
                cx - innerR * 0.3, cy - innerR * 0.3, 2,
                cx, cy, innerR)
            knobGrad.addColorStop(0, "#d0d0d0")
            knobGrad.addColorStop(0.4, "#b0b0b0")
            knobGrad.addColorStop(0.7, "#888")
            knobGrad.addColorStop(1, "#666")

            ctx.beginPath()
            ctx.arc(cx, cy, innerR, 0, Math.PI * 2)
            ctx.fillStyle = knobGrad
            ctx.fill()
            ctx.strokeStyle = "#444"
            ctx.lineWidth = 1.5
            ctx.stroke()

            // Línea indicadora
            var markAng = (225 - normalizedValue * 270) * Math.PI / 180
            var markR1 = innerR * 0.4
            var markR2 = innerR * 0.85
            ctx.beginPath()
            ctx.moveTo(cx + Math.cos(markAng) * markR1, cy - Math.sin(markAng) * markR1)
            ctx.lineTo(cx + Math.cos(markAng) * markR2, cy - Math.sin(markAng) * markR2)
            ctx.strokeStyle = "#222"
            ctx.lineWidth = 2.5
            ctx.stroke()
        }

        MouseArea {
            anchors.fill: parent
            preventStealing: true
            property real startY: 0
            property real startVal: 0
            onPressed: (mouse) => {
                startY = mouse.y
                startVal = root.value
            }
            onPositionChanged: (mouse) => {
                if (!pressed) return
                var delta = (startY - mouse.y) / 150.0
                var range = root.to - root.from
                var nv = Math.max(root.from, Math.min(root.to, startVal + delta * range))
                if (nv !== root.value) {
                    root.value = nv
                    root.moved(nv)
                    trackKnobCanvas.requestPaint()
                }
            }
            onDoubleClicked: {
                root.doubleClicked()
            }
        }
    }
}
