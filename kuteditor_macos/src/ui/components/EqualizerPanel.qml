import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Column {
    id: eqPanel
    spacing: 8
    width: parent ? parent.width : 400

    property var fx: null
    property bool showHeader: true

    // Header + Toggle
    Row {
        spacing: 10
        visible: eqPanel.showHeader
        Label {
            text: "Ecualizador"
            color: "#fff"; font.bold: true; font.pixelSize: 14
            anchors.verticalCenter: parent.children[1].verticalCenter
        }
        Button {
            width: 48; height: 26
            text: fx && fx.enabled ? "on" : "off"
            onClicked: { if (fx) fx.setEnabled(!fx.enabled) }
            background: Rectangle {
                radius: 3
                color: fx && fx.enabled ? Qt.rgba(0.2, 0.6, 0.86, 0.35) : Qt.rgba(1,1,1,0.08)
                border.color: fx && fx.enabled ? "#3498db" : "#555"
            }
            contentItem: Label {
                anchors.centerIn: parent
                text: parent.text
                color: fx && fx.enabled ? "#3498db" : "#888"
                font.bold: true; font.pixelSize: 11
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
    }

    // === Curva ===
    Rectangle {
        width: parent.width; height: 180
        color: "#111418"; radius: 5
        border.color: "#3c4146"

        Canvas {
            id: trackEqCurve
            anchors.fill: parent; anchors.margins: 1

            property real _hpf: fx ? fx.hpfFreq : 0
            property real _lf: fx ? fx.lowFreq : 200
            property real _lg: fx ? fx.lowGainDb : 0
            property real _mf: fx ? fx.midFreq : 1000
            property real _mg: fx ? fx.midGainDb : 0
            property real _mq: fx ? fx.midQ : 1
            property real _hf: fx ? fx.highFreq : 5000
            property real _hg: fx ? fx.highGainDb : 0

            on_HpfChanged: requestPaint()
            on_LfChanged: requestPaint(); on_LgChanged: requestPaint()
            on_MfChanged: requestPaint(); on_MgChanged: requestPaint()
            on_MqChanged: requestPaint()
            on_HfChanged: requestPaint(); on_HgChanged: requestPaint()

            onPaint: {
                var ctx = getContext("2d"); var w = width, h = height
                ctx.clearRect(0, 0, w, h)
                ctx.fillStyle = "#111418"; ctx.fillRect(0, 0, w, h)
                var pL = 28, pR = 6, pT = 6, pB = 16
                var gw = w - pL - pR, gh = h - pT - pB
                var sr = 48000

                function f2x(f) { return pL + gw * (Math.log10(f) - Math.log10(20)) / (Math.log10(20000) - Math.log10(20)) }
                function d2y(d) { return pT + gh * (1 - (d + 15) / 30) }

                // Grid 0dB and others
                var dbLines = [-12, -9, -6, -3, 0, 3, 6, 9, 12]
                ctx.textAlign = "right"; ctx.textBaseline = "middle"
                for (var di = 0; di < dbLines.length; di++) {
                    var yy = d2y(dbLines[di])
                    ctx.beginPath()
                    ctx.moveTo(pL, yy)
                    ctx.lineTo(w - pR, yy)
                    ctx.strokeStyle = dbLines[di] === 0 ? "#555" : "#2a2d32"
                    ctx.lineWidth = dbLines[di] === 0 ? 1.5 : 1
                    ctx.stroke()
                    ctx.fillStyle = "#666"; ctx.font = "9px Menlo"
                    ctx.fillText(dbLines[di] + "", pL - 3, yy)
                }

                // Grid Frequencies
                var fLines = [100, 200, 500, 1000, 2000, 5000, 10000, 20000]
                ctx.textAlign = "center"; ctx.textBaseline = "top"
                for (var fi = 0; fi < fLines.length; fi++) {
                    var xx = f2x(fLines[fi])
                    ctx.beginPath()
                    ctx.moveTo(xx, pT)
                    ctx.lineTo(xx, pT + gh)
                    ctx.strokeStyle = "#2a2d32"
                    ctx.lineWidth = 1
                    ctx.stroke()
                    ctx.fillStyle = "#666"
                    ctx.font = "9px Menlo"
                    var label = fLines[fi] >= 1000 ? (fLines[fi] / 1000) + "k" : fLines[fi].toString()
                    ctx.fillText(label, xx, pT + gh + 2)
                }

                function bqMag(b0,b1,b2,a1,a2,freq) {
                    var om = 2*Math.PI*freq/sr, cw = Math.cos(om), sw = Math.sin(om)
                    var c2 = Math.cos(2*om), s2 = Math.sin(2*om)
                    var nr = b0+b1*cw+b2*c2, ni = -(b1*sw+b2*s2)
                    var dr = 1+a1*cw+a2*c2, di = -(a1*sw+a2*s2)
                    var n2 = nr*nr+ni*ni, d2m = dr*dr+di*di
                    return d2m < 1e-20 ? 0 : 10*Math.log10(n2/d2m)
                }
                function lsC(f0,g) {
                    var A=Math.pow(10,g/40),w0=2*Math.PI*f0/sr,c=Math.cos(w0),s=Math.sin(w0)
                    var al=s/(2*Math.sqrt(2)),sq=2*Math.sqrt(A)*al
                    var b0=A*((A+1)-(A-1)*c+sq),b1=2*A*((A-1)-(A+1)*c),b2=A*((A+1)-(A-1)*c-sq)
                    var a0=(A+1)+(A-1)*c+sq,a1n=-2*((A-1)+(A+1)*c),a2n=(A+1)+(A-1)*c-sq
                    return{b0:b0/a0,b1:b1/a0,b2:b2/a0,a1:a1n/a0,a2:a2n/a0}
                }
                function hsC(f0,g) {
                    var A=Math.pow(10,g/40),w0=2*Math.PI*f0/sr,c=Math.cos(w0),s=Math.sin(w0)
                    var al=s/(2*Math.sqrt(2)),sq=2*Math.sqrt(A)*al
                    var b0=A*((A+1)+(A-1)*c+sq),b1=-2*A*((A-1)+(A+1)*c),b2=A*((A+1)+(A-1)*c-sq)
                    var a0=(A+1)-(A-1)*c+sq,a1n=2*((A-1)-(A+1)*c),a2n=(A+1)-(A-1)*c-sq
                    return{b0:b0/a0,b1:b1/a0,b2:b2/a0,a1:a1n/a0,a2:a2n/a0}
                }
                function pkC(f0,g,Q) {
                    var A=Math.pow(10,g/40),w0=2*Math.PI*f0/sr,c=Math.cos(w0),al=Math.sin(w0)/(2*Q)
                    var b0=1+al*A,b1=-2*c,b2=1-al*A,a0=1+al/A,a1n=-2*c,a2n=1-al/A
                    return{b0:b0/a0,b1:b1/a0,b2:b2/a0,a1:a1n/a0,a2:a2n/a0}
                }
                function hpC(f0) {
                    if (f0 < 20) return {b0:1,b1:0,b2:0,a1:0,a2:0}
                    var w0=2*Math.PI*f0/sr,c=Math.cos(w0),al=Math.sin(w0)/(2*0.7071)
                    var b0=(1+c)/2,b1=-(1+c),b2=(1+c)/2,a0=1+al,a1n=-2*c,a2n=1-al
                    return{b0:b0/a0,b1:b1/a0,b2:b2/a0,a1:a1n/a0,a2:a2n/a0}
                }

                var hp = hpC(_hpf), lo = lsC(_lf, _lg), mi = pkC(_mf, _mg, _mq), hi = hsC(_hf, _hg)
                var nP = 150
                ctx.beginPath()
                for (var i = 0; i < nP; i++) {
                    var fr = i/(nP-1), freq = 20*Math.pow(1000, fr)
                    var tot = bqMag(hp.b0,hp.b1,hp.b2,hp.a1,hp.a2,freq)
                            + bqMag(lo.b0,lo.b1,lo.b2,lo.a1,lo.a2,freq)
                            + bqMag(mi.b0,mi.b1,mi.b2,mi.a1,mi.a2,freq)
                            + bqMag(hi.b0,hi.b1,hi.b2,hi.a1,hi.a2,freq)
                    var px = f2x(freq), py = d2y(Math.max(-15, Math.min(15, tot)))
                    i === 0 ? ctx.moveTo(px, py) : ctx.lineTo(px, py)
                }
                
                // Relleno bajo la curva (Hindenburg style: azul hacia abajo)
                ctx.lineTo(f2x(20000), d2y(-15))
                ctx.lineTo(f2x(20), d2y(-15))
                ctx.closePath()
                var fillGrad = ctx.createLinearGradient(0, d2y(15), 0, d2y(-15))
                fillGrad.addColorStop(0, "rgba(52, 152, 219, 0.4)")
                fillGrad.addColorStop(1, "rgba(52, 152, 219, 0.05)")
                ctx.fillStyle = fx && fx.enabled ? fillGrad : "rgba(150,150,150,0.06)"
                ctx.fill()

                ctx.beginPath()
                for (var j = 0; j < nP; j++) {
                    var fr2 = j/(nP-1), fq2 = 20*Math.pow(1000,fr2)
                    var t2 = bqMag(hp.b0,hp.b1,hp.b2,hp.a1,hp.a2,fq2)
                           + bqMag(lo.b0,lo.b1,lo.b2,lo.a1,lo.a2,fq2)
                           + bqMag(mi.b0,mi.b1,mi.b2,mi.a1,mi.a2,fq2)
                           + bqMag(hi.b0,hi.b1,hi.b2,hi.a1,hi.a2,fq2)
                    var px2 = f2x(fq2), py2 = d2y(Math.max(-15,Math.min(15,t2)))
                    j === 0 ? ctx.moveTo(px2,py2) : ctx.lineTo(px2,py2)
                }
                ctx.strokeStyle = fx && fx.enabled ? "#3498db" : "#888"
                ctx.lineWidth = 2; ctx.stroke()
            }
        }
    }

    Rectangle { width: parent.width; height: 1; color: "#3c4146" }

    // Dials Hindenburg-Style
    RowLayout {
        width: parent.width
        spacing: 15

        // HPF
        Column {
            Layout.alignment: Qt.AlignHCenter
            spacing: 4
            MetalKnob {
                size: 60
                from: 0
                to: 500
                value: fx ? fx.hpfFreq : 0
                defaultValue: 0
                label: "HPF"
                dotColor: "#e67e22"
                onMoved: (newValue) => { if (fx) { if (!fx.enabled) fx.setEnabled(true); fx.setHpfFreq(newValue) } }
            }
        }

        // Bass
        Column {
            Layout.alignment: Qt.AlignHCenter
            spacing: 4
            MetalKnob {
                size: 60
                from: -12
                to: 12
                value: fx ? fx.lowGainDb : 0
                label: "LOW"
                dotColor: "#e74c3c"
                onMoved: (newValue) => { if (fx) { if (!fx.enabled) fx.setEnabled(true); fx.setLowGainDb(newValue) } }
            }
        }

        // Mid
        Column {
            Layout.alignment: Qt.AlignHCenter
            spacing: 4
            MetalKnob {
                size: 60
                from: -12
                to: 12
                value: fx ? fx.midGainDb : 0
                label: "MID"
                dotColor: "#2ecc71"
                onMoved: (newValue) => { if (fx) { if (!fx.enabled) fx.setEnabled(true); fx.setMidGainDb(newValue) } }
            }
        }

        // High
        Column {
            Layout.alignment: Qt.AlignHCenter
            spacing: 4
            MetalKnob {
                size: 60
                from: -12
                to: 12
                value: fx ? fx.highGainDb : 0
                label: "HIGH"
                dotColor: "#3498db"
                onMoved: (newValue) => { if (fx) { if (!fx.enabled) fx.setEnabled(true); fx.setHighGainDb(newValue) } }
            }
        }
    }
}
