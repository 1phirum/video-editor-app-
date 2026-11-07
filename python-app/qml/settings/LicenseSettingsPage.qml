// qmllint disable
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CutPro 1.0
import "../theme"

SettingsPage {
    id: root

    // Snapshot of Backend.licenseInfo(), refreshed when the page loads and
    // whenever the license changes. expiryMsecs drives the live countdown.
    property var info: ({})
    property double expiryMsecs: 0
    property bool perpetual: true
    property double nowMs: Date.now()

    function refresh() {
        info = Backend.licenseInfo()
        perpetual = info.perpetual === true
        expiryMsecs = Number(info.expiryMsecs || 0)
        nowMs = Date.now()
    }

    // Milliseconds left before the license lapses (0 when perpetual/expired).
    readonly property double remainingMs:
        perpetual ? -1 : Math.max(0, expiryMsecs - nowMs)

    function pad(n) { return (n < 10 ? "0" : "") + n }
    function countdownText() {
        if (perpetual)
            return qsTr("Never expires")
        if (remainingMs <= 0)
            return qsTr("Expired")
        var total = Math.floor(remainingMs / 1000)
        var days = Math.floor(total / 86400)
        var hours = Math.floor((total % 86400) / 3600)
        var mins = Math.floor((total % 3600) / 60)
        var secs = total % 60
        return days + "d  " + pad(hours) + ":" + pad(mins) + ":" + pad(secs)
    }

    Component.onCompleted: refresh()
    Connections {
        target: Backend
        function onLicensedChanged() { root.refresh() }
    }

    // Ticks the countdown once a second (no backend call - just re-reads the clock).
    Timer {
        interval: 1000
        running: !root.perpetual && root.remainingMs > 0
        repeat: true
        onTriggered: root.nowMs = Date.now()
    }

    SettingsSection {
        title: qsTr("License")

        SettingsRow {
            label: qsTr("Status")
            RowLayout {
                spacing: 8
                Rectangle {
                    Layout.alignment: Qt.AlignVCenter
                    width: 9; height: 9; radius: 4.5
                    color: !root.info.licensed ? Theme.textMuted
                           : (!root.perpetual && root.remainingMs <= 0) ? Theme.danger
                           : "#35c15a"
                }
                Text {
                    text: !root.info.licensed ? qsTr("Not activated")
                          : (!root.perpetual && root.remainingMs <= 0) ? qsTr("Expired")
                          : qsTr("Active")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fsSm
                    font.weight: Font.DemiBold
                }
            }
        }

        SettingsRow {
            label: qsTr("License type")
            Text {
                text: root.perpetual ? qsTr("Perpetual") : qsTr("Expires %1").arg(root.info.expiryDate || "")
                color: Theme.textPrimary
                font.pixelSize: Theme.fsSm
            }
        }

        SettingsRow {
            label: qsTr("Time remaining")
            visible: !root.perpetual
            Text {
                text: root.countdownText()
                color: root.remainingMs > 0 ? Theme.textPrimary : Theme.danger
                font.pixelSize: Theme.fsLg
                font.family: "monospace"
                font.weight: Font.DemiBold
            }
        }

        SettingsRow {
            label: qsTr("Machine ID")
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text {
                    Layout.fillWidth: true
                    text: root.info.machineId || ""
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fsSm
                    font.family: "monospace"
                    elide: Text.ElideRight
                }
                Button {
                    id: copyBtn
                    text: qsTr("Copy")
                    implicitWidth: 78
                    implicitHeight: 28
                    hoverEnabled: true
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    onClicked: {
                        clip.text = root.info.machineId || ""
                        clip.selectAll()
                        clip.copy()
                        copyBtn.text = qsTr("Copied")
                        copyReset.restart()
                    }
                    contentItem: Text {
                        text: copyBtn.text
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fsSm
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: Theme.radiusSm
                        color: copyBtn.down ? Theme.hover
                               : copyBtn.hovered ? Theme.bgCardHeader : Theme.bgPrimary
                        border.color: Theme.border
                    }
                    TextEdit { id: clip; visible: false; width: 0; height: 0 }
                    Timer { id: copyReset; interval: 1500; onTriggered: copyBtn.text = qsTr("Copy") }
                }
            }
        }
    }
}
