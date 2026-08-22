import QtQuick
import QtQuick.Controls

/**
 * SpectralDisplay: Visualizador de espectro de frecuencia logarithmic (20Hz - 20kHz).
 * Muestra tres curvas: entrada (verde), salida (naranja) y ruido (gris).
 */
Item {
    id: display
    width: 400
    height: 200

    property var inputMags: []
    property var outputMags: []
    property var noiseMags: []
    property bool active: true

    Rectangle {
        anchors.fill: parent
        color: "#1a1d21"
        radius: 4
        border.color: "#3c4146"
        border.width: 1
    }

    // Grid y etiquetas de frecuencia
    Canvas {
        id: gridCanvas
        anchors.fill: parent
        opacity: 0.3
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            ctx.strokeStyle = "#4a4d52"
            ctx.lineWidth = 1
            ctx.setLineDash([2, 4])

            // Líneas horizontales (-20, -40, -60, -80 dB)
            for (let i = 1; i < 5; i++) {
                let y = (height / 5) * i
                ctx.beginPath()
                ctx.moveTo(0, y)
                ctx.lineTo(width, y)
                ctx.stroke()
            }

            // Líneas verticales (Logarítmicas: 100, 200, 500, 1k, 2k, 5k, 10k)
            const freqs = [50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000]
            freqs.forEach(f => {
                let x = freqToX(f)
                ctx.beginPath()
                ctx.moveTo(x, 0)
                ctx.lineTo(x, height)
                ctx.stroke()
            })
        }

        function freqToX(f) {
            const minF = 20
            const maxF = 22050
            return width * (Math.log10(f / minF) / Math.log10(maxF / minF))
        }
    }

    // Etiquetas de frecuencia
    Row {
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 2
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 0
        Repeater {
            model: [50, 100, 200, 500, "1k", "2k", "5k", "10k", "20k"]
            delegate: Text {
                text: modelData
                color: "#666"
                font.pixelSize: 9
                width: parent.width / 9
                horizontalAlignment: Text.AlignHCenter
                x: freqToX(parseFreq(modelData)) - width/2

                function parseFreq(s) {
                    if (typeof s === "number") return s
                    if (s.endsWith("k")) return parseFloat(s) * 1000
                    return parseFloat(s)
                }
                function freqToX(f) {
                    const minF = 20
                    const maxF = 22050
                    return display.width * (Math.log10(f / minF) / Math.log10(maxF / minF))
                }
            }
        }
    }

    // Curvas de datos
    Canvas {
        id: dataCanvas
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            if (!display.active) return

            // 1. Dibujar ruido (Gris)
            drawCurve(ctx, display.noiseMags, "#555555", 1.5, false)
            // 2. Dibujar entrada (Verde)
            drawCurve(ctx, display.inputMags, "#2ecc71", 1.5, true)
            // 3. Dibujar salida (Naranja)
            drawCurve(ctx, display.outputMags, "#e67e22", 1.5, false)
        }

        function drawCurve(ctx, mags, color, lineWidth, fill) {
            if (!mags || mags.length < 2) return
            
            ctx.beginPath()
            ctx.strokeStyle = color
            ctx.lineWidth = lineWidth
            ctx.lineJoin = "round"

            const n = mags.length
            const minF = 20
            const maxF = 22050
            
            for (let i = 0; i < n; i++) {
                // Mapear bin a frecuencia
                // FFT_SIZE = 2048, bins = 1025. Subsampled by 4 = 257 bins.
                const bin = i * 4
                const freq = (bin * 48000) / 2048
                if (freq < minF) continue
                if (freq > maxF) break

                const x = width * (Math.log10(freq / minF) / Math.log10(maxF / minF))
                const mag = mags[i]
                // Convertir mag a dB para el eje Y (normalizado 0 a -100 dB)
                const db = 20 * Math.log10(Math.max(mag, 1e-6))
                const y = Math.min(height, Math.max(0, height * (db / -80)))

                if (i === 0) ctx.moveTo(x, y)
                else ctx.lineTo(x, y)
            }
            ctx.stroke()

            if (fill) {
                ctx.lineTo(width, height)
                ctx.lineTo(0, height)
                ctx.fillStyle = color + "22" // semi-transparente
                ctx.fill()
            }
        }

        Timer {
            interval: 50
            running: display.visible && display.active
            repeat: true
            onTriggered: dataCanvas.requestPaint()
        }
    }
}
