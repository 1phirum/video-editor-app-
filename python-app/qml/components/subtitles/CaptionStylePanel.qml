// qmllint disable
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
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

    readonly property int labelWidth: 76

    property var fonts: ["Khmer OS System", "Khmer OS Siemreap",
                          "Khmer OS Battambang", "Roboto", "Open Sans", "Lato"]
    property var positions: ["Top", "Center", "Bottom", "Custom"]
    property var alignments: ["Left", "Center", "Right"]

    ColorDialog {
        id: textColorDialog
        title: "Caption text color"
        onAccepted: Backend.captionTextColor = selectedColor.toString()
    }
    ColorDialog {
        id: backgroundColorDialog
        title: "Caption background color"
        onAccepted: Backend.captionBackgroundColor = selectedColor.toString()
    }

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
                text: "Caption appearance"
                color: Theme.textPrimary
                font.pixelSize: Theme.fsMd
                font.weight: Font.DemiBold
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text {
                    text: "Font"
                    color: Theme.textMuted
                    font.pixelSize: Theme.fsSm
                    Layout.minimumWidth: root.labelWidth
                    Layout.maximumWidth: root.labelWidth
                }
                WhisperComboBox {
                    id: fontSelector
                    Layout.fillWidth: true
                    model: root.fonts
                    currentIndex: Math.max(0, root.fonts.indexOf(Backend.captionFontFamily))
                    popupWidth: 190
                    delegate: ItemDelegate {
                        required property int index
                        width: fontSelector.popup.width - 2
                        height: 38
                        highlighted: fontSelector.highlightedIndex === index
                        contentItem: Text {
                            text: fontSelector.textAt(index) + (index < 3 || Backend.downloadedCaptionFonts.indexOf(fontSelector.textAt(index)) >= 0 ? "" : " (download)")
                            color: Theme.textSecondary
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle { color: highlighted ? Theme.hover : Theme.bgPanel }
                    }
                    onActivated: {
                        if (currentIndex < 3 || Backend.downloadedCaptionFonts.indexOf(currentText) >= 0)
                            Backend.captionFontFamily = currentText
                    }
                }
                Button {
                    visible: fontSelector.currentIndex >= 3 &&
                             Backend.downloadedCaptionFonts.indexOf(fontSelector.currentText) < 0
                    text: "Download"
                    enabled: visible
                    onClicked: Backend.downloadCaptionFont(fontSelector.currentText)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text {
                    text: "Size"
                    color: Theme.textMuted
                    font.pixelSize: Theme.fsSm
                    Layout.minimumWidth: root.labelWidth
                    Layout.maximumWidth: root.labelWidth
                }
                CaptionSpinBox {
                    id: sizeSelector
                    Layout.preferredWidth: 118
                    from: 12
                    to: 120
                    value: Backend.captionFontSize
                    onValueModified: Backend.captionFontSize = value
                    onValueChanged: {
                        if (Backend.captionFontSize !== value)
                            Backend.captionFontSize = value
                    }
                }
                Item { Layout.fillWidth: true }
                CaptionToggleButton {
                    glyph: "B"
                    glyphBold: true
                    toolTipText: "Bold"
                    checked: Backend.captionBold
                    onToggled: Backend.captionBold = checked
                }
                CaptionToggleButton {
                    glyph: "I"
                    glyphItalic: true
                    toolTipText: "Italic"
                    checked: Backend.captionItalic
                    onToggled: Backend.captionItalic = checked
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text {
                    text: "Text color"
                    color: Theme.textMuted
                    font.pixelSize: Theme.fsSm
                    Layout.minimumWidth: root.labelWidth
                    Layout.maximumWidth: root.labelWidth
                }
                CaptionColorButton {
                    swatchColor: Backend.captionTextColor
                    toolTipText: "Choose caption text color"
                    onClicked: {
                        textColorDialog.selectedColor = Backend.captionTextColor
                        textColorDialog.open()
                    }
                }
                Text {
                    text: Backend.captionTextColor
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fsXs
                    font.family: Theme.monoFont
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text {
                    text: "Background"
                    color: Theme.textMuted
                    font.pixelSize: Theme.fsSm
                    Layout.minimumWidth: root.labelWidth
                    Layout.maximumWidth: root.labelWidth
                }
                CaptionSwitch {
                    text: "Show"
                    checked: Backend.captionBackgroundVisible
                    onToggled: Backend.captionBackgroundVisible = checked
                }
                Item { Layout.fillWidth: true }
                CaptionColorButton {
                    enabled: Backend.captionBackgroundVisible
                    swatchColor: Backend.captionBackgroundColor
                    toolTipText: "Choose caption background color"
                    onClicked: {
                        backgroundColorDialog.selectedColor = Backend.captionBackgroundColor
                        backgroundColorDialog.open()
                    }
                }
                Text {
                    text: Backend.captionBackgroundColor
                    color: Backend.captionBackgroundVisible
                           ? Theme.textSecondary : Theme.textMuted
                    font.pixelSize: Theme.fsXs
                    font.family: Theme.monoFont
                    Layout.preferredWidth: 78
                    elide: Text.ElideRight
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text {
                    text: "Position"
                    color: Theme.textMuted
                    font.pixelSize: Theme.fsSm
                    Layout.minimumWidth: root.labelWidth
                    Layout.maximumWidth: root.labelWidth
                }
                WhisperComboBox {
                    Layout.fillWidth: true
                    model: root.positions
                    currentIndex: Math.max(0, root.positions.indexOf(
                                               Backend.captionPosition.charAt(0).toUpperCase()
                                               + Backend.captionPosition.slice(1)))
                    popupWidth: 140
                    onActivated: Backend.captionPosition = currentText.toLowerCase()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text {
                    text: "Align"
                    color: Theme.textMuted
                    font.pixelSize: Theme.fsSm
                    Layout.minimumWidth: root.labelWidth
                    Layout.maximumWidth: root.labelWidth
                }
                WhisperComboBox {
                    Layout.fillWidth: true
                    model: root.alignments
                    currentIndex: Math.max(0, root.alignments.indexOf(
                                               Backend.captionAlignment.charAt(0).toUpperCase()
                                               + Backend.captionAlignment.slice(1)))
                    popupWidth: 140
                    onActivated: Backend.captionAlignment = currentText.toLowerCase()
                }
            }

            Item { Layout.fillHeight: true; Layout.minimumHeight: 16 }
        }
    }
}
