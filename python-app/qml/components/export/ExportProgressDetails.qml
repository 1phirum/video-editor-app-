import QtQuick
import CutPro 1.0
import "../../theme"

Text {
    color: Theme.textMuted
    font.pixelSize: Theme.fsXs
    wrapMode: Text.WordWrap
    visible: Backend.exportInProgress

    function remainingTime(seconds) {
        if (seconds < 0)
            return qsTr("Calculating time remaining…")
        var hours = Math.floor(seconds / 3600)
        var minutes = Math.floor((seconds % 3600) / 60)
        var secs = Math.floor(seconds % 60)
        if (hours > 0)
            return qsTr("%1h %2m remaining").arg(hours).arg(minutes)
        if (minutes > 0)
            return qsTr("%1m %2s remaining").arg(minutes).arg(secs)
        return qsTr("%1s remaining").arg(secs)
    }

    text: {
        var parts = []
        if (Backend.exportEncoder !== "")
            parts.push(Backend.exportEncoder)
        parts.push(qsTr("%1m %2s elapsed").arg(Math.floor(Backend.exportElapsedSeconds / 60)).arg(Backend.exportElapsedSeconds % 60))
        if (Backend.exportBytesPerSecond > 0)
            parts.push((Backend.exportBytesPerSecond / 1000000).toFixed(1) + " MB/s "
                       + (Backend.exportFinalizing ? qsTr("final output written") : qsTr("data written")))
        if (Backend.exportFramesPerSecond > 0)
            parts.push(Backend.exportFramesPerSecond.toFixed(1) + " fps")
        if (Backend.exportSpeed > 0)
            parts.push(Backend.exportSpeed.toFixed(2) + "×")
        parts.push(remainingTime(Backend.exportRemainingSeconds))
        if (!Backend.exportRemainingIncludesFinalization)
            parts.push(qsTr("plus finalization"))
        if (Backend.exportEncoderFallback !== "")
            parts.push(Backend.exportEncoderFallback)
        return parts.join("  ·  ")
    }
}
