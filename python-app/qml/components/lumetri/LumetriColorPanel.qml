pragma ComponentBehavior: Bound
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
import "../project"
import "../subtitles"
import "../timeline"

Item {
    id: root

    property int currentTab: 0
    readonly property var activeClip: Backend.activeColorClip
    readonly property var activeMedia: Backend.activeColorMedia
    readonly property var correction: activeClip && activeClip.lumetri ? activeClip.lumetri : ({})
    readonly property var sourceColor: activeMedia && activeMedia.color ? activeMedia.color : ({})

    function clipValue(key, fallback) {
        var value = correction[key]
        return value === undefined || value === null ? fallback : value
    }
    function projectValue(key, fallback) {
        var value = Backend.colorSettings[key]
        return value === undefined || value === null ? fallback : value
    }
    function setClip(key, value) {
        if (activeClip && activeClip.id)
            Backend.setClipColorSetting(activeClip.id, key, value)
    }
    function setMedia(key, value) {
        if (activeMedia && activeMedia.id)
            Backend.setMediaColorSetting(activeMedia.id, key, value)
    }
    function browseInputLut() {
        lutDialog.open()
    }
    function defaultClipValue(key) {
        var defaults = {
            "saturation": 100,
            "lookIntensity": 100,
            "creativeSaturation": 100,
            "look": "None",
            "curvePreset": "None",
            "hslHueWidth": 30,
            "hslSaturationMax": 100,
            "hslLumaMax": 100,
            "vignetteMidpoint": 50,
            "vignetteFeather": 50
        }
        return defaults[key] === undefined ? 0 : defaults[key]
    }
    function resetKeys(keys) {
        for (var i = 0; i < keys.length; ++i)
            setClip(keys[i], defaultClipValue(keys[i]))
    }

    SrtSettingsDialog { id: srtDialog; parent: Overlay.overlay }

    FileDialog {
        id: lutDialog
        title: "Choose Input LUT"
        fileMode: FileDialog.OpenFile
        nameFilters: ["3D LUT (*.cube *.3dl *.lut)", "All files (*)"]
        onAccepted: {
            if (root.activeMedia && root.activeMedia.id)
                Backend.setMediaColorSetting(root.activeMedia.id,
                                              "inputLut", selectedFile.toString())
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 43
            color: Theme.bgPanel
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 7
                anchors.rightMargin: 8
                spacing: 18
                Repeater {
                    model: ["Edit", "Settings"]
                    delegate: Item {
                        required property int index
                        required property string modelData
                        Layout.fillHeight: true
                        implicitWidth: label.implicitWidth
                        Text { id: label; anchors.centerIn: parent; text: modelData; color: root.currentTab === index ? Theme.textPrimary : Theme.textMuted; font.pixelSize: Theme.fsMd; font.weight: root.currentTab === index ? Font.DemiBold : Font.Normal }
                        Rectangle { anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter; width: label.implicitWidth; height: 2; color: Theme.accent; visible: root.currentTab === index }
                        TapHandler { cursorShape: Qt.PointingHandCursor; onTapped: root.currentTab = index }
                    }
                }
                Item { Layout.fillWidth: true }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ScrollView {
                anchors.fill: parent
                visible: root.currentTab === 0
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                contentWidth: availableWidth

                ColumnLayout {
                    width: parent.width
                    spacing: 0

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 54
                        color: Theme.bgPanel
                        visible: !root.activeClip || !root.activeClip.id
                        Text { anchors.centerIn: parent; width: parent.width - 28; text: "Move the playhead over a video or image clip to edit its color."; color: Theme.textMuted; font.pixelSize: Theme.fsSm; wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        visible: !!(root.activeClip && root.activeClip.id)

                        LumetriBasicSection { panel: root }
                        LumetriCreativeSection { panel: root }
                        LumetriCurvesSection { panel: root }
                        LumetriColorWheelsSection { panel: root }
                        LumetriHslSecondarySection { panel: root }
                        LumetriVignetteSection { panel: root }
                    }
                }
            }

            ScrollView {
                anchors.fill: parent
                visible: root.currentTab === 1
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                contentWidth: availableWidth

                ColumnLayout {
                    width: parent.width
                    spacing: 0

                        LumetriSection {
                            title: "Preferences"
                            expanded: false
                        CaptionSwitch { text: "Display Color Management"; checked: root.projectValue("displayColorManagement", false); onToggled: Backend.setColorSetting("displayColorManagement", checked) }
                        CaptionSwitch { text: "Extended Dynamic Range Monitoring"; checked: root.projectValue("extendedDynamicRange", false); onToggled: Backend.setColorSetting("extendedDynamicRange", checked) }
                    }

                    LumetriSection {
                        title: "Display Color"
                        expanded: false
                        LumetriSettingRow { label: "Viewer Gamma"; WhisperComboBox { model: ["2.2 (Web)", "2.4 (Broadcast)"]; currentIndex: Math.max(0, model.indexOf(root.projectValue("viewerGamma", "2.4 (Broadcast)"))); Layout.fillWidth: true; onActivated: Backend.setColorSetting("viewerGamma", currentText) } }
                        LumetriSettingRow { label: "HDR Graphics White"; WhisperComboBox { model: ["100 (SDR)", "203 (75% HLG, 58% PQ)", "300 (HDR)"]; currentIndex: Math.max(0, model.indexOf(root.projectValue("hdrGraphicsWhite", "203 (75% HLG, 58% PQ)"))); Layout.fillWidth: true; onActivated: Backend.setColorSetting("hdrGraphicsWhite", currentText) } }
                    }

                    LumetriSection {
                        title: "Transmit Device Playback"
                        expanded: false
                        CaptionSwitch { text: "Transmit Video Stream"; checked: root.projectValue("transmitVideoStream", false); onToggled: Backend.setColorSetting("transmitVideoStream", checked) }
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: root.projectValue("transmitMode", "Listener") + "  |  Port " + root.projectValue("transmitPort", 4201); color: Theme.textMuted; font.pixelSize: Theme.fsXs; Layout.fillWidth: true }
                            LumetriActionButton {
                                text: "SRT Settings"
                                onClicked: srtDialog.open()
                            }
                        }
                    }

                    LumetriSection {
                        title: "Project"
                        expanded: false
                        LumetriSettingRow { label: "3D LUT Interpolation"; WhisperComboBox { model: ["Trilinear", "Tetrahedral"]; currentIndex: Math.max(0, model.indexOf(root.projectValue("lutInterpolation", "Tetrahedral"))); Layout.fillWidth: true; onActivated: Backend.setColorSetting("lutInterpolation", currentText) } }
                        CaptionSwitch { text: "Auto Detect Log Video Color Space"; checked: root.projectValue("autoDetectLogColorSpace", true); onToggled: Backend.setColorSetting("autoDetectLogColorSpace", checked) }
                    }

                    LumetriSection {
                        title: "Source Clip"
                        expanded: false
                        enabled: !!(root.activeMedia && root.activeMedia.id)
                        opacity: enabled ? 1 : 0.45
                        LumetriSettingRow {
                            label: "Input LUT"
                            RowLayout {
                                Layout.fillWidth: true
                                LumetriTextField {
                                    Layout.fillWidth: true
                                    text: root.sourceColor.inputLut || "None"
                                    onEditingFinished: if (root.activeMedia && root.activeMedia.id) Backend.setMediaColorSetting(root.activeMedia.id, "inputLut", text)
                                }
                                LumetriActionButton {
                                    text: "Browse"
                                    onClicked: lutDialog.open()
                                }
                            }
                        }
                        CaptionSwitch { text: "Use Media Color Space"; checked: root.sourceColor.useMediaColorSpace === undefined ? true : root.sourceColor.useMediaColorSpace; onToggled: if (root.activeMedia && root.activeMedia.id) Backend.setMediaColorSetting(root.activeMedia.id, "useMediaColorSpace", checked) }
                        LumetriSettingRow { label: "Override Media Color Space"; WhisperComboBox { model: ["None", "Rec. 709", "Rec. 2100 HLG", "Rec. 2100 PQ"]; currentIndex: Math.max(0, model.indexOf(root.sourceColor.overrideMediaColorSpace || "None")); Layout.fillWidth: true; onActivated: if (root.activeMedia && root.activeMedia.id) Backend.setMediaColorSetting(root.activeMedia.id, "overrideMediaColorSpace", currentText) } }
                    }

                    LumetriSection {
                        title: "Sequence"
                        expanded: false
                        LumetriSettingRow { label: "Working Color Space"; WhisperComboBox { model: ["Rec. 709", "Rec. 2100 HLG", "Rec. 2100 PQ"]; currentIndex: Math.max(0, model.indexOf(root.projectValue("workingColorSpace", "Rec. 709"))); Layout.fillWidth: true; onActivated: Backend.setColorSetting("workingColorSpace", currentText) } }
                        CaptionSwitch { text: "Auto Tone Map Media"; checked: root.projectValue("autoToneMapMedia", true); onToggled: Backend.setColorSetting("autoToneMapMedia", checked) }
                    }
                }
            }
        }
    }
}
