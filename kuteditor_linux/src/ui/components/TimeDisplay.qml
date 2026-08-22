import QtQuick
import QtQuick.Controls

Label {
    property double currentTime: 0.0
    property double totalTime:   0.0
    property int    displayMode: 0  // 0 = elapsed, 1 = remaining

    function formatTime(seconds) {
        const s = Math.max(0, seconds)
        const h = Math.floor(s / 3600)
        const m = Math.floor((s % 3600) / 60)
        const sec = Math.floor(s % 60)
        const dec = Math.floor((s * 10) % 10)
        if (h > 0) {
            return h + ":" + m.toString().padStart(2, '0') + ":"
                   + sec.toString().padStart(2, '0') + "." + dec
        }
        return m + ":" + sec.toString().padStart(2, '0') + "." + dec
    }

    text: formatTime(currentTime)

    font.family: "Monospace"
    font.pixelSize: 22
    font.bold: true
}
