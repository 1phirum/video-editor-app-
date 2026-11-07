pragma ComponentBehavior: Bound
//qmllint disable
import QtQuick
import QtQuick.Layouts
import "../components/common"
import "../components/effects"
import "../components/export"
import "../components/lumetri"
import "../components/project"
import "../components/subtitles"
import "../components/timeline"
import "../theme"

SettingsPage {
    id: root
    property var accentChoices: ["#4b8ff5", "#29a3a3", "#55a868",
                                 "#d6a23d", "#d06464", "#b36ad8"]

    SettingsSection {
        title: qsTr("Interface")
        SettingsRow {
            label: qsTr("Brightness")
            LumetriSlider {
                Layout.fillWidth: true
                from: 20
                to: 100
                value: Number(root.settings.appearanceBrightness || 50)
                suffix: "%"
                // LumetriSlider publishes through valueCommitted, not the value
                // property, so listen there - otherwise a drag snaps back and the
                // brightness looks stuck wherever it last landed.
                onValueCommitted: v => {
                    if (Math.round(v) !== Math.round(Number(root.settings.appearanceBrightness || 50)))
                        root.settingChanged("appearanceBrightness", Math.round(v))
                }
            }
        }
        SettingsRow {
            label: qsTr("Highlight color")
            Repeater {
                model: root.accentChoices
                delegate: Rectangle {
                    id: swatch
                    required property string modelData
                    width: 30
                    height: 24
                    radius: 4
                    color: modelData
                    border.width: root.settings.accentColor === modelData ? 2 : 1
                    border.color: root.settings.accentColor === modelData
                                        ? "white" : Theme.border
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        onTapped: root.settingChanged("accentColor", swatch.modelData)
                    }
                }
            }
        }
    }
}
