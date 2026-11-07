//qmllint disable
import QtQuick
import QtQuick.Layouts
import "../common"
import "../export"
import "../lumetri"
import "../project"
import "../subtitles"
import "../timeline"

Item {
    id: root
    property var parameterData: ({})
    property var currentValue
    signal valueCommitted(var value)

    visible: parameterData.type !== "mask"

    Layout.fillWidth: true
    implicitHeight: parameterData.type === "bool" ? 30 : 28

    LumetriSlider {
        anchors.fill: parent
        visible: root.parameterData.type === "number"
        label: String(root.parameterData.name || "")
        from: Number(root.parameterData.minimum || 0)
        to: Number(root.parameterData.maximum || 100)
        decimals: Number(root.parameterData.decimals || 0)
        suffix: String(root.parameterData.unit || "")
        value: Number(root.currentValue === undefined
                      ? root.parameterData.default : root.currentValue)
        onValueCommitted: value => root.valueCommitted(value)
    }

    CaptionSwitch {
        anchors.fill: parent
        visible: root.parameterData.type === "bool"
        text: String(root.parameterData.name || "")
        checked: Boolean(root.currentValue === undefined
                         ? root.parameterData.default : root.currentValue)
        onToggled: root.valueCommitted(checked)
    }
}
