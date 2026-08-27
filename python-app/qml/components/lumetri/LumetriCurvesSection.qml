//qmllint disable
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../theme"
import "../common"
import "../effects"
import "../export"
import "../project"
import "../subtitles"
import "../timeline"

LumetriSection {
    id: root
    required property var panel
    title: "Curves"
    checkable: true
    sectionEnabled: panel.clipValue("curvesEnabled", true)
    onSectionToggled: enabled => panel.setClip("curvesEnabled", enabled)
    expanded: true
    property int rgbChannel: 0
    readonly property var channelNames: ["Master", "Red", "Green", "Blue"]
    readonly property var channelPointKeys: ["masterCurvePoints", "redCurvePoints", "greenCurvePoints", "blueCurvePoints"]
    readonly property var channelColors: [Theme.textPrimary, "#ef6767", "#62c77b", "#668de8"]
    readonly property var hueNames: ["Hue Vs Hue", "Hue Vs Sat", "Hue Vs Luma", "Luma Vs Sat", "Sat Vs Sat"]
    readonly property var huePointKeys: ["hueVsHuePoints", "hueVsSatPoints", "hueVsLumaPoints", "lumaVsSatPoints", "satVsSatPoints"]
    readonly property var hueEnabledKeys: ["hueVsHueEnabled", "hueVsSatEnabled", "hueVsLumaEnabled", "lumaVsSatEnabled", "satVsSatEnabled"]

    function resetCurves() {
        root.panel.setClip("curvePreset", "None")
        var keys = channelPointKeys.concat(huePointKeys)
        for (var i = 0; i < keys.length; ++i)
            root.panel.setClip(keys[i], [])
    }

    LumetriCurveGroup {
        title: "RGB Curves"
        expanded: true
        groupEnabled: root.panel.clipValue("rgbCurvesEnabled", true)
        hueBackground: false
        flatIdentity: false
        curveColor: root.channelColors[root.rgbChannel]
        points: root.panel.clipValue(root.channelPointKeys[root.rgbChannel], [])
        onEnabledToggled: enabled => root.panel.setClip("rgbCurvesEnabled", enabled)
        onPointsCommitted: points => root.panel.setClip(
                               root.channelPointKeys[root.rgbChannel], points)
        editorHeader: Component {
            RowLayout {
                spacing: 12
                Repeater {
                    model: root.channelNames
                    delegate: Rectangle {
                        id: channelButton
                        required property int index
                        required property string modelData
                        width: 20
                        height: 20
                        radius: 10
                        color: root.channelColors[index]
                        border.width: root.rgbChannel === index ? 2 : 0
                        border.color: Theme.accent
                        ToolTip.visible: channelHover.hovered
                        ToolTip.text: modelData
                        HoverHandler {
                            id: channelHover
                            cursorShape: Qt.PointingHandCursor
                        }
                        TapHandler {
                            onTapped: root.rgbChannel = channelButton.index
                        }
                    }
                }
                Item { Layout.fillWidth: true }
            }
        }
    }
    LumetriSettingRow {
        label: "Curve preset"
        WhisperComboBox {
            property var choices: ["None", "Increase Contrast", "Lift Shadows"]
            model: choices
            currentIndex: Math.max(0, choices.indexOf(root.panel.clipValue("curvePreset", "None")))
            Layout.fillWidth: true
            onActivated: root.panel.setClip("curvePreset", currentText)
        }
    }
    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }
    Text {
        text: "Hue/Saturation Curves"
        color: Theme.textMuted
        font.pixelSize: Theme.fsXs
    }
    Repeater {
        model: root.hueNames
        delegate: LumetriCurveGroup {
            id: hueGroup
            required property int index
            required property string modelData
            title: modelData
            expanded: index === 0
            groupEnabled: root.panel.clipValue(root.hueEnabledKeys[index], true)
            points: root.panel.clipValue(root.huePointKeys[index], [])
            onEnabledToggled: enabled => root.panel.setClip(
                                  root.hueEnabledKeys[index], enabled)
            onPointsCommitted: points => root.panel.setClip(
                                   root.huePointKeys[index], points)
        }
    }
    RowLayout {
        Layout.fillWidth: true
        Item { Layout.fillWidth: true }
        LumetriActionButton {
            text: "Reset curves"
            onClicked: root.resetCurves()
        }
    }
}
