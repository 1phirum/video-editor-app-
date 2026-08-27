import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CutPro 1.0
import "../../theme"
import "../common"
import "../effects"
import "../export"
import "../lumetri"
import "../project"
import "../timeline"

Rectangle {
    id: root
    color: Theme.bgPanel

    readonly property int labelWidth: 86

    Flickable {
        anchors.fill: parent
        anchors.margins: 12
        clip: true
        contentWidth: width
        contentHeight: controls.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        ColumnLayout {
            id: controls
            width: parent.width
            spacing: 10

            Text {
                text: "Subtitle blur region"
                color: Theme.textPrimary
                font.pixelSize: Theme.fsMd
                font.weight: Font.DemiBold
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text {
                    text: "Effect"
                    color: Theme.textMuted
                    font.pixelSize: Theme.fsSm
                    Layout.minimumWidth: root.labelWidth
                    Layout.maximumWidth: root.labelWidth
                }
                CaptionSwitch {
                    text: "Enable blur"
                    checked: Backend.captionBlurEnabled
                    onToggled: Backend.captionBlurEnabled = checked
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text {
                    text: "Tracking"
                    color: Theme.textMuted
                    font.pixelSize: Theme.fsSm
                    Layout.minimumWidth: root.labelWidth
                    Layout.maximumWidth: root.labelWidth
                }
                CaptionSwitch {
                    text: "Subtitle bounds"
                    checked: Backend.captionBlurTrackingEnabled
                    onToggled: Backend.captionBlurTrackingEnabled = checked
                }
                Item { Layout.fillWidth: true }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text {
                    text: "Strength"
                    color: Theme.textMuted
                    font.pixelSize: Theme.fsSm
                    Layout.minimumWidth: root.labelWidth
                    Layout.maximumWidth: root.labelWidth
                }
                CaptionSpinBox {
                    Layout.preferredWidth: 118
                    from: 1
                    to: 64
                    value: Backend.captionBlurStrength
                    onValueModified: Backend.captionBlurStrength = value
                }
                Text {
                    text: "px"
                    color: Theme.textMuted
                    font.pixelSize: Theme.fsXs
                }
                Item { Layout.fillWidth: true }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text {
                    text: "Padding"
                    color: Theme.textMuted
                    font.pixelSize: Theme.fsSm
                    Layout.minimumWidth: root.labelWidth
                    Layout.maximumWidth: root.labelWidth
                }
                CaptionSpinBox {
                    Layout.preferredWidth: 118
                    from: 0
                    to: 64
                    value: Backend.captionBlurPadding
                    onValueModified: Backend.captionBlurPadding = value
                }
                Text {
                    text: "px"
                    color: Theme.textMuted
                    font.pixelSize: Theme.fsXs
                }
                Item { Layout.fillWidth: true }
            }

            Text {
                visible: !Backend.hasSubtitleClips
                text: "No subtitle track"
                color: Theme.textMuted
                font.pixelSize: Theme.fsSm
            }

            Item { Layout.fillHeight: true; Layout.minimumHeight: 16 }
        }
    }
}
