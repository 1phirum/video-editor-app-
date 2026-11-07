import QtQuick
import "../common"
import "../effects"
import "../export"
import "../lumetri"
import "../project"
import "../subtitles"

// Throttles timeline auto-follow and keeps the playhead inside a stable viewport
// zone. Repeated playback ticks collapse into one scroll update instead of
// queueing an unbounded number of Qt.callLater callbacks.
Item {
    id: root
    visible: false

    required property Flickable target
    required property real playheadContentX
    required property real headerWidth
    property bool followEnabled: true
    property bool blocked: false

    function requestFollow() {
        if (followEnabled && !blocked)
            followTimer.restart()
    }

    function followNow() {
        if (!followEnabled || blocked || !target
                || target.contentWidth <= target.width)
            return

        var viewportX = playheadContentX - target.contentX
        var leftGuard = headerWidth + 36
        var rightGuard = Math.max(leftGuard + 40, target.width - 96)
        var maximum = Math.max(0, target.contentWidth - target.width)

        if (viewportX < leftGuard) {
            target.contentX = Math.max(
                        0, Math.min(maximum,
                                    playheadContentX - leftGuard))
        } else if (viewportX > rightGuard) {
            // Keep the playhead around 70% across the lane. This scrolls in
            // useful chunks and avoids a one-pixel correction on every frame.
            var preferredX = headerWidth
                    + Math.max(80, (target.width - headerWidth) * 0.70)
            target.contentX = Math.max(
                        0, Math.min(maximum,
                                    playheadContentX - preferredX))
        }
    }

    Timer {
        id: followTimer
        interval: 24
        repeat: false
        onTriggered: root.followNow()
    }
}
