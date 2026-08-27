import QtQuick
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
        title: "Playback"
        SettingsRow {
            label: "Pause when app loses focus"
            CaptionSwitch {
                checked: root.settings.pauseOnFocusLoss !== false
                onToggled: root.settingChanged("pauseOnFocusLoss", checked)
            }
        }
        SettingsRow {
            label: "Loop sequence"
            CaptionSwitch {
                checked: root.settings.loopPlayback === true
                onToggled: root.settingChanged("loopPlayback", checked)
            }
        }
    }
}
