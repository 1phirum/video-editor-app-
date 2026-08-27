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
        title: "Tracks"
        SettingsRow {
            label: "Default video tracks"
            SettingsSpinBox {
                from: 1
                to: 16
                value: Number(root.settings.defaultVideoTracks || 1)
                onValueModified: root.settingChanged("defaultVideoTracks", value)
            }
        }
        SettingsRow {
            label: "Default audio tracks"
            SettingsSpinBox {
                from: 1
                to: 16
                value: Number(root.settings.defaultAudioTracks || 1)
                onValueModified: root.settingChanged("defaultAudioTracks", value)
            }
        }
        SettingsRow {
            label: "Track height"
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
        title: "Timeline Behavior"
        SettingsRow {
            label: "Auto-scroll during playback"
            CaptionSwitch {
                checked: root.settings.timelineAutoScroll !== false
                onToggled: root.settingChanged("timelineAutoScroll", checked)
            }
        }
        SettingsRow {
            label: "Snapping for new projects"
            CaptionSwitch {
                checked: root.settings.timelineSnapping !== false
                onToggled: root.settingChanged("timelineSnapping", checked)
            }
        }
    }
}
