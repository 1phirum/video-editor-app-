// qmllint disable
import QtQuick
import QtQuick.Controls
import QtQuick.Window
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

    // The caption font list is owned by the backend now (system + cloud +
    // user-imported), so importing or downloading a font shows up here live
    // without this file hardcoding families or an "is it a system font" index.
    readonly property var fontModel: Backend.captionFontModel
    readonly property var fonts: root.fontModel.map(e => e.family)

    // A cloud font that has not been fetched yet: shown with a "(download)" hint
    // and not selectable until its Download button pulls it in.
    function fontNeedsDownload(family) {
        for (let i = 0; i < root.fontModel.length; i++) {
            const entry = root.fontModel[i]
            if (entry.family === family)
                return Boolean(entry.downloadable) && !entry.available
        }
        return false
    }

    property var positions: ["Top", "Center", "Bottom", "Custom"]
    property var alignments: ["Left", "Center", "Right"]

    // One-click complete looks. Each card sets font size, weight, colour, box and
    // placement together so a client picks a style by eye instead of dialling in
    // six separate controls. Applied through the same Backend properties the rows
    // below drive, so the manual controls stay in sync afterwards.
    property var stylePresets: [
        { name: qsTr("Classic"),   size: 42, bold: false, textColor: "#ffffff", box: true,  bgColor: "#000000", pos: "bottom", align: "center" },
        { name: qsTr("Bold White"),size: 52, bold: true,  textColor: "#ffffff", box: false, bgColor: "#000000", pos: "bottom", align: "center" },
        { name: qsTr("Boxed"),     size: 40, bold: true,  textColor: "#ffffff", box: true,  bgColor: "#000000", pos: "bottom", align: "center" },
        { name: qsTr("Yellow Pop"),size: 50, bold: true,  textColor: "#ffe000", box: false, bgColor: "#000000", pos: "bottom", align: "center" },
        { name: qsTr("Highlight"), size: 44, bold: true,  textColor: "#000000", box: true,  bgColor: "#ffe000", pos: "bottom", align: "center" },
        { name: qsTr("Red Alert"), size: 48, bold: true,  textColor: "#ff3b30", box: false, bgColor: "#000000", pos: "bottom", align: "center" },
        { name: qsTr("Big Impact"),size: 72, bold: true,  textColor: "#ffffff", box: false, bgColor: "#000000", pos: "center", align: "center" },
        { name: qsTr("Top Bar"),   size: 38, bold: false, textColor: "#ffffff", box: true,  bgColor: "#000000", pos: "top",    align: "center" }
    ]

    function applyCaptionPreset(p) {
        Backend.captionFontSize = p.size
        Backend.captionBold = p.bold
        Backend.captionTextColor = p.textColor
        Backend.captionBackgroundVisible = p.box
        if (p.box)
            Backend.captionBackgroundColor = p.bgColor
        Backend.captionPosition = p.pos
        Backend.captionAlignment = p.align
    }

    function presetMatches(p) {
        return Backend.captionTextColor.toLowerCase() === p.textColor.toLowerCase()
                && Backend.captionBackgroundVisible === p.box
                && Backend.captionBold === p.bold
    }

    ColorPickerDialog {
        id: textColorDialog
        ownerWindow: root.Window.window
        dialogTitle: qsTr("Caption text color")
        onPicked: (value) => Backend.captionTextColor = value
    }
    ColorPickerDialog {
        id: backgroundColorDialog
        ownerWindow: root.Window.window
        dialogTitle: qsTr("Caption background color")
        // The box behind a caption is meant to be partly transparent - the
        // default is 70% black - so this one offers the alpha row.
        showAlpha: true
        onPicked: (value) => Backend.captionBackgroundColor = value
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
                text: qsTr("Caption appearance")
                color: Theme.textPrimary
                font.pixelSize: Theme.fsMd
                font.weight: Font.DemiBold
            }

            // Visual preset gallery: pick a look, fine-tune with the rows below.
            Text {
                text: qsTr("Quick styles")
                color: Theme.textMuted
                font.pixelSize: Theme.fsXs
            }
            Flow {
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: root.stylePresets
                    delegate: Rectangle {
                        id: presetCard
                        required property var modelData
                        readonly property bool current: root.presetMatches(modelData)
                        width: 78
                        height: 46
                        radius: 6
                        color: "#151515"
                        border.width: current ? 2 : 1
                        border.color: current ? Theme.accent : Theme.border
                        clip: true

                        // Live sample rendered in the preset's own look.
                        Rectangle {
                            anchors.centerIn: parent
                            width: sample.implicitWidth + 10
                            height: sample.implicitHeight + 4
                            radius: 3
                            visible: presetCard.modelData.box
                            color: presetCard.modelData.bgColor
                        }
                        Text {
                            id: sample
                            anchors.centerIn: parent
                            text: "Aa"
                            color: presetCard.modelData.textColor
                            font.pixelSize: 18
                            font.bold: presetCard.modelData.bold
                        }
                        Text {
                            anchors.bottom: parent.bottom
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.bottomMargin: 2
                            text: presetCard.modelData.name
                            color: Theme.textMuted
                            font.pixelSize: 8
                        }

                        HoverHandler { id: presetHover }
                        TapHandler { onTapped: root.applyCaptionPreset(presetCard.modelData) }
                        Rectangle {
                            anchors.fill: parent
                            radius: parent.radius
                            color: "transparent"
                            border.width: 1
                            border.color: presetHover.hovered ? Theme.accent : "transparent"
                        }
                        ToolTip.visible: presetHover.hovered
                        ToolTip.text: presetCard.modelData.name + qsTr(" caption style")
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text {
                    text: qsTr("Font")
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
                            text: fontSelector.textAt(index) + (root.fontNeedsDownload(fontSelector.textAt(index)) ? qsTr(" (download)") : "")
                            color: Theme.textSecondary
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle { color: highlighted ? Theme.hover : Theme.bgPanel }
                    }
                    onActivated: {
                        if (!root.fontNeedsDownload(currentText))
                            Backend.captionFontFamily = currentText
                    }
                }
                Button {
                    visible: root.fontNeedsDownload(fontSelector.currentText)
                    text: qsTr("Download")
                    enabled: visible
                    onClicked: Backend.downloadCaptionFont(fontSelector.currentText)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text {
                    text: qsTr("Size")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fsSm
                    Layout.minimumWidth: root.labelWidth
                    Layout.maximumWidth: root.labelWidth
                }
                CaptionSpinBox {
                    id: sizeSelector
                    Layout.preferredWidth: 118
                    from: 1
                    to: 400
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
                    toolTipText: qsTr("Bold")
                    checked: Backend.captionBold
                    onToggled: Backend.captionBold = checked
                }
                CaptionToggleButton {
                    glyph: "I"
                    glyphItalic: true
                    toolTipText: qsTr("Italic")
                    checked: Backend.captionItalic
                    onToggled: Backend.captionItalic = checked
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text {
                    text: qsTr("Text color")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fsSm
                    Layout.minimumWidth: root.labelWidth
                    Layout.maximumWidth: root.labelWidth
                }
                CaptionColorButton {
                    swatchColor: Backend.captionTextColor
                    toolTipText: qsTr("Choose caption text color")
                    onClicked: {
                        textColorDialog.openWith(Backend.captionTextColor)
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
                    text: qsTr("Background")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fsSm
                    Layout.minimumWidth: root.labelWidth
                    Layout.maximumWidth: root.labelWidth
                }
                CaptionSwitch {
                    text: qsTr("Show")
                    checked: Backend.captionBackgroundVisible
                    onToggled: Backend.captionBackgroundVisible = checked
                }
                Item { Layout.fillWidth: true }
                CaptionColorButton {
                    enabled: Backend.captionBackgroundVisible
                    swatchColor: Backend.captionBackgroundColor
                    toolTipText: qsTr("Choose caption background color")
                    onClicked: {
                        backgroundColorDialog.openWith(Backend.captionBackgroundColor)
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
                    text: qsTr("Position")
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
                    text: qsTr("Align")
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
