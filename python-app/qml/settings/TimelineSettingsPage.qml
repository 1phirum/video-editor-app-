import QtQuick
import QtQuick.Layouts
import "../components/common"
import "../components/effects"
import "../components/export"
import "../components/lumetri"
import "../components/project"
import "../components/subtitles"
import "../components/timeline"

SettingsPage {
    id: root

    SettingsSection {
        title: qsTr("Tracks")
        SettingsRow {
            label: qsTr("Default video tracks")
            SettingsSpinBox {
                from: 1
                to: 16
                value: Number(root.settings.defaultVideoTracks || 1)
                onValueModified: root.settingChanged("defaultVideoTracks", value)
            }
        }
        // Audio lanes are content-driven (CapCut-style): a sequence has no
        // audio track until an audio clip or the timeline's A+ button creates
        // one, so there is no "default audio tracks" count to configure.
        SettingsRow {
            label: qsTr("Track height")
            LumetriSlider {
                Layout.fillWidth: true
                from: 48
                to: 96
                value: Number(root.settings.timelineTrackHeight || 68)
                suffix: "px"
                onValueCommitted: value => root.settingChanged(
                                      "timelineTrackHeight", Math.round(value))
            }
        }
    }

    SettingsSection {
        title: qsTr("Timeline Behavior")
        SettingsRow {
            label: qsTr("Auto-scroll during playback")
            CaptionSwitch {
                checked: root.settings.timelineAutoScroll !== false
                onToggled: root.settingChanged("timelineAutoScroll", checked)
            }
        }
        SettingsRow {
            label: qsTr("Snapping for new projects")
            CaptionSwitch {
                checked: root.settings.timelineSnapping !== false
                onToggled: root.settingChanged("timelineSnapping", checked)
            }
        }
    }
}
