// qmllint disable
// Voice changer: a list of voices you audition by ear, then commit.
//
// The row layout is deliberate. Picking a voice is a listening task, so every
// row carries its own play button and the whole list stays on screen while you
// compare - a dropdown would hide the alternatives at the moment you are trying
// to choose between them. "Original" is in the list and is rendered through a
// passthrough chain like the rest, so A/B against your untouched voice is a
// fair comparison rather than two different decode paths.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CutPro 1.0
import "../../theme"
import "../common"

NativeModalWindow {
    id: dialog

    dialogTitle: qsTr("Voice Changer")
    dialogWidth: 520
    dialogHeight: 560

    property string clipId: ""
    property var targetIds: []
    property string clipName: ""
    // The voice already committed to this clip, so the list can mark it.
    property string appliedVoice: ""
    property string chosenVoice: ""

    readonly property bool busy: Backend.voiceFxBusy

    function openFor(clip, ids) {
        clipId = clip ? String(clip.id) : ""
        targetIds = Backend.editableClipIds(ids && ids.length ? ids : [clipId], "audio")
        clipName = clip && clip.name ? String(clip.name) : ""
        appliedVoice = clipId.length > 0 ? Backend.clipVoiceId(clipId) : ""
        chosenVoice = appliedVoice.length > 0 ? appliedVoice : "none"
        openNative()
    }

    onClosing: Backend.stopVoicePreview()

    Rectangle {
        anchors.fill: parent
        color: Theme.bgSidebar

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 10

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Pick a voice for \"%1\". Press ▶ to hear five seconds of it. Applying renders the whole clip, so the new voice is audible on the timeline and in the export, not just here.").arg(dialog.targetIds.length > 1 ? qsTr("%1 selected clips").arg(dialog.targetIds.length) : dialog.clipName)
                color: Theme.textSecondary
                font.pixelSize: Theme.fsSm
            }

            ListView {
                id: voiceList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: Backend.voicePresets()
                spacing: 2
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: Rectangle {
                    id: row
                    required property var modelData
                    readonly property bool chosen: dialog.chosenVoice === String(modelData.id)
                    readonly property bool isApplied: dialog.appliedVoice === String(modelData.id)
                    readonly property bool previewing:
                        Backend.voiceFxPreviewId === String(modelData.id)
                    width: voiceList.width
                    height: 46
                    radius: Theme.radiusSm
                    color: row.chosen ? Qt.alpha(Theme.accent, 0.18)
                                      : (rowHover.hovered ? Theme.bgPanel : "transparent")
                    border.width: 1
                    border.color: row.chosen ? Theme.accent : "transparent"

                    HoverHandler { id: rowHover }
                    TapHandler {
                        onTapped: dialog.chosenVoice = String(row.modelData.id)
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 8
                        spacing: 10

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            RowLayout {
                                spacing: 6
                                Label {
                                    text: row.modelData.name
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.fsSm
                                    font.bold: row.chosen
                                }
                                Rectangle {
                                    visible: row.isApplied
                                    implicitWidth: appliedLabel.implicitWidth + 10
                                    implicitHeight: 14
                                    radius: 7
                                    color: Qt.alpha(Theme.accent, 0.3)
                                    Label {
                                        id: appliedLabel
                                        anchors.centerIn: parent
                                        text: qsTr("applied")
                                        color: Theme.textPrimary
                                        font.pixelSize: 9
                                    }
                                }
                            }
                            Label {
                                Layout.fillWidth: true
                                text: row.modelData.description
                                color: Theme.textMuted
                                font.pixelSize: 10
                                elide: Text.ElideRight
                            }
                        }

                        // Per-row audition. Rendering is cached per voice and
                        // per source span, so hearing one a second time is
                        // instant.
                        Rectangle {
                            Layout.preferredWidth: 26
                            Layout.preferredHeight: 22
                            radius: 3
                            color: row.previewing ? Theme.accent
                                                  : (playHover.hovered ? Theme.bgPrimary : "transparent")
                            border.width: 1
                            border.color: row.previewing ? Theme.accent : Theme.border

                            Label {
                                anchors.centerIn: parent
                                text: row.previewing ? "■" : "▶"
                                color: row.previewing ? "#ffffff" : Theme.textSecondary
                                font.pixelSize: 11
                            }
                            HoverHandler { id: playHover }
                            TapHandler {
                                onTapped: {
                                    dialog.chosenVoice = String(row.modelData.id)
                                    if (row.previewing)
                                        Backend.stopVoicePreview()
                                    else
                                        Backend.previewVoice(dialog.clipId,
                                                             String(row.modelData.id))
                                }
                            }
                        }
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                visible: Backend.voiceFxStatus.length > 0
                text: Backend.voiceFxStatus
                color: Theme.textMuted
                font.pixelSize: Theme.fsSm
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                DarkDialogButton {
                    text: qsTr("Remove voice")
                    flat: true
                    visible: dialog.targetIds.some(function(id) { return Backend.clipVoiceId(String(id)).length > 0 })
                    enabled: !dialog.busy
                    onClicked: {
                        Backend.applyClipsVoice(dialog.targetIds, "none")
                        dialog.appliedVoice = ""
                        dialog.chosenVoice = "none"
                    }
                }
                Item { Layout.fillWidth: true }
                DarkDialogButton {
                    text: qsTr("Close")
                    flat: true
                    onClicked: dialog.close()
                }
                DarkDialogButton {
                    text: qsTr("Apply")
                    highlighted: true
                    enabled: !dialog.busy && dialog.chosenVoice.length > 0
                             && (dialog.targetIds.length > 1 || dialog.chosenVoice !== dialog.appliedVoice)
                    onClicked: Backend.applyClipsVoice(dialog.targetIds, dialog.chosenVoice)
                }
            }
        }
    }

    Connections {
        target: Backend
        function onVoiceFxFinished(ok, presetId, error) {
            // Only an Apply changes what is committed; a finished audition
            // leaves the choice where the user put it.
            if (ok && !Backend.voiceFxBusy && presetId === dialog.chosenVoice
                    && dialog.targetIds.every(function(id) {
                        var voice = Backend.clipVoiceId(String(id))
                        return presetId === "none" ? voice.length === 0 : voice === presetId
                    })) {
                dialog.appliedVoice = presetId
                dialog.close()
            }
        }
    }
}
