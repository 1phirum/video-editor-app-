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
        title: "Output"
        SettingsRow {
            label: "Master volume"
            LumetriSlider {
                Layout.fillWidth: true
                from: 0
                to: 100
                value: root.settings.masterVolume === undefined
                       ? 100 : Number(root.settings.masterVolume)
                suffix: "%"
                onValueCommitted: value => root.settingChanged(
                                      "masterVolume", Math.round(value))
            }
        }
        SettingsRow {
            label: "Mute all audio"
            CaptionSwitch {
                checked: root.settings.muteAllAudio === true
                onToggled: root.settingChanged("muteAllAudio", checked)
            }
        }
    }

    SettingsSection {
        title: "Meters"
        SettingsRow {
            label: "Refresh interval"
            SettingsComboBox {
                model: ["60 ms", "110 ms", "200 ms", "350 ms"]
                currentIndex: {
                    var wanted = Number(root.settings.audioMeterRefreshMs || 110)
                    var values = [60, 110, 200, 350]
                    return Math.max(0, values.indexOf(wanted))
                }
                onActivated: root.settingChanged(
                                 "audioMeterRefreshMs", [60, 110, 200, 350][currentIndex])
            }
        }
    }
}
