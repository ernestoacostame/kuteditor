import QtQuick

/**
 * Peak meter vertical con escala dB y hold del pico.
 * Sin dependencias de Kirigami: los colores son parámetros.
 */
Item {
    id: peakMeter

    // Nivel actual normalizado [0, 1]. 1.0 = 0 dB.
    property real level: 0.0
    property real peakHold: 0.0

    property int channelCount: 2

    // Paleta
    property color bgColor:      "#111418"
    property color borderColor:  "#3c4146"
    property color gridColor:    "#3c4146"
    property color textColor:    "#ecf0f1"
    property color peakColor:    "#ff6b6b"

    // Timer de hold del pico
    Timer {
        id: peakHoldTimer
        interval: 1500
        running: false
        onTriggered: peakMeter.peakHold = 0.0
    }

    onLevelChanged: {
        if (level > peakHold) {
            peakHold = level
            peakHoldTimer.restart()
        }
        canvas.requestPaint()
    }

    onWidthChanged:  canvas.requestPaint()
    onHeightChanged: canvas.requestPaint()

    Canvas {
        id: canvas
        anchors.fill: parent
        renderStrategy: Canvas.Cooperative

        onPaint: {
            const ctx = getContext("2d")
            if (!ctx) return
            ctx.reset()

            const w = canvas.width
            const h = canvas.height
            if (w <= 0 || h <= 0) return

            // Fondo
            ctx.fillStyle = peakMeter.bgColor
            ctx.fillRect(0, 0, w, h)

            // Escala dB (-60 .. 0) a lo largo de toda la altura
            ctx.strokeStyle = peakMeter.gridColor
            ctx.lineWidth = 1
            ctx.font = "10px monospace"
            ctx.fillStyle = peakMeter.textColor
            ctx.textBaseline = "middle"

            const dbLevels = [0, -6, -12, -20, -30, -40, -50, -60]
            for (let i = 0; i < dbLevels.length; i++) {
                const db = dbLevels[i]
                const norm = 1 - (db + 60) / 60
                const y = Math.round(norm * h) + 0.5
                ctx.beginPath()
                ctx.moveTo(0, y); ctx.lineTo(w, y); ctx.stroke()
                ctx.fillText(db + " dB", 4, y - 6)
            }

            // Barras de nivel, una por canal
            const chW = w / peakMeter.channelCount
            for (let c = 0; c < peakMeter.channelCount; c++) {
                const x = c * chW + 2
                const bw = chW - 4

                // Gradiente verde → amarillo → rojo
                const grad = ctx.createLinearGradient(0, h, 0, 0)
                grad.addColorStop(0.00, "#2ecc71")
                grad.addColorStop(0.60, "#2ecc71")
                grad.addColorStop(0.80, "#f1c40f")
                grad.addColorStop(0.95, "#e74c3c")
                grad.addColorStop(1.00, "#c0392b")

                const lvl = Math.min(1.0, Math.max(0.0, peakMeter.level))
                const barH = lvl * h
                ctx.fillStyle = grad
                ctx.fillRect(x, h - barH, bw, barH)

                // Línea de pico
                if (peakMeter.peakHold > 0) {
                    const py = h - (peakMeter.peakHold * h)
                    ctx.strokeStyle = peakMeter.peakColor
                    ctx.lineWidth = 2
                    ctx.beginPath()
                    ctx.moveTo(x, py); ctx.lineTo(x + bw, py); ctx.stroke()
                }
            }

            // Borde
            ctx.strokeStyle = peakMeter.borderColor
            ctx.lineWidth = 1
            ctx.strokeRect(0.5, 0.5, w - 1, h - 1)
        }
    }
}
