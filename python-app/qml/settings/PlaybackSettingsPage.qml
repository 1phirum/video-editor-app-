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
        title: qsTr("Playback")
        // Playback no longer stops when the window loses focus: clicking another
        // app while a sequence plays is not a request to pause, so there is no
        // "pause on focus loss" switch to offer any more.
        SettingsRow {
            label: qsTr("Loop sequence")
            CaptionSwitch {
                checked: root.settings.loopPlayback === true
                onToggled: root.settingChanged("loopPlayback", checked)
            }
        }
    }
}
