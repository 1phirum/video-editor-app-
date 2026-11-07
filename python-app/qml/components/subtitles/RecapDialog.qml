// qmllint disable
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import CutPro 1.0
import "../../theme"
import "../common"

NativeModalWindow {
    id: dialog

    dialogTitle: qsTr("Video recap")
    dialogWidth: 780
    dialogHeight: 560

    // The recap text shown in the panel. Seeded from Backend.recapText when a run
    // finishes, filled from the transcript by "Load subtitle", or typed by hand.
    // Not bound, so a hand edit and Regenerate can each replace it.
    property string draft: ""
    property bool confirmingReplace: false

    function open() {
        draft = ""
        confirmingReplace = false
        openNative()
    }

    Connections {
        target: Backend
        function onRecapChanged() {
            if (!Backend.recapInProgress && Backend.recapText !== "")
                dialog.draft = Backend.recapText
        }
    }

    // Reusable pill button for the sidebar grid and the confirm panel.
    component SidebarButton: Button {
        property color fillColor: Theme.bgPrimary
        property color textColor: Theme.textPrimary
        property color strokeColor: "#4a4a4a"
        property int textWeight: Font.Normal
        Layout.fillWidth: true
        Layout.preferredHeight: 40
        implicitHeight: 40
        HoverHandler { cursorShape: parent.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
        contentItem: Text {
            text: parent.text
            color: parent.textColor
            font.pixelSize: Theme.fsSm
            font.weight: parent.textWeight
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            color: parent.down ? Theme.hover : parent.fillColor
            border.color: parent.strokeColor
            border.width: 1
            radius: 16
            opacity: parent.enabled ? 1.0 : 0.45
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.translationDialogSurface
        border.color: Theme.translationRowBorder
        border.width: 1
        radius: Theme.translationDialogRadius
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        anchors.topMargin: 14
        anchors.bottomMargin: 14
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Text {
                text: dialog.dialogTitle
                color: Theme.textPrimary
                font.pixelSize: Theme.fsMd
                font.weight: Font.DemiBold
            }
            InfoTip {
                Layout.alignment: Qt.AlignVCenter
                text: qsTr("Import a subtitle, or Load subtitle to pull the existing ")
                      + qsTr("transcript. Regenerate builds the recap; Replace swaps ")
                      + qsTr("the whole subtitle for it.")
            }
            Item { Layout.fillWidth: true }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            visible: Backend.recapInProgress
            BusyIndicator {
                running: Backend.recapInProgress
                implicitWidth: 22
                implicitHeight: 22
            }
            Text {
                Layout.fillWidth: true
                text: Backend.recapStatus !== "" ? Backend.recapStatus
                                                 : qsTr("Generating recap...")
                color: Theme.textSecondary
                font.pixelSize: Theme.fsSm
                elide: Text.ElideRight
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                TextArea {
                    id: recapArea
                    text: dialog.draft
                    onTextChanged: dialog.draft = text
                    wrapMode: TextArea.Wrap
                    selectByMouse: true
                    placeholderText: qsTr("Import a subtitle file, or click Load subtitle to pull the existing transcript. Then Regenerate to build the recap.")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fsSm
                    background: Rectangle {
                        color: Theme.bgPrimary
                        border.color: Theme.translationRowBorder
                        radius: Theme.radiusSm
                    }
                }
            }

            ColumnLayout {
                Layout.preferredWidth: 250
                Layout.maximumWidth: 250
                Layout.fillHeight: true
                spacing: 8

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 8
                    rowSpacing: 8

                    SidebarButton {
                        text: qsTr("Import")
                        enabled: !Backend.recapInProgress
                        onClicked: importSubtitleFileDialog.open()
                    }
                    SidebarButton {
                        text: qsTr("Load subtitle")
                        strokeColor: Theme.accent
                        textColor: "#cfe0fb"
                        enabled: Backend.transcriptCount > 0 && !Backend.recapInProgress
                        onClicked: dialog.draft = Backend.transcriptText()
                    }
                    SidebarButton {
                        text: Backend.recapInProgress ? qsTr("Cancel") : qsTr("Regenerate")
                        enabled: Backend.recapInProgress || Backend.transcriptCount > 0
                        onClicked: {
                            if (Backend.recapInProgress)
                                Backend.cancelRecap()
                            else
                                Backend.generateRecap()
                        }
                    }
                    SidebarButton {
                        text: qsTr("Replace")
                        fillColor: Theme.accent
                        strokeColor: Theme.accent
                        textColor: "white"
                        textWeight: Font.DemiBold
                        enabled: dialog.draft !== "" && !Backend.recapInProgress
                        onClicked: dialog.confirmingReplace = true
                    }
                }

                Item { Layout.fillHeight: true }
            }
        }
    }

    // Themed confirm for the destructive Replace, instead of a platform dialog.
    Rectangle {
        anchors.fill: parent
        visible: dialog.confirmingReplace
        color: "#aa000000"
        MouseArea { anchors.fill: parent; onClicked: {} }

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(420, parent.width - 48)
            height: confirmCol.implicitHeight + 32
            radius: Theme.radiusSm
            color: Theme.translationDialogSurface
            border.color: Theme.translationRowBorder
            border.width: 1

            ColumnLayout {
                id: confirmCol
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                spacing: 10

                Text {
                    Layout.fillWidth: true
                    text: qsTr("Replace the entire subtitle with this recap?")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fsMd
                    font.weight: Font.DemiBold
                    wrapMode: Text.Wrap
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("This overwrites all current subtitle lines and re-times the recap across the video. This cannot be undone from here.")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fsSm
                    wrapMode: Text.Wrap
                }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    spacing: 8
                    Item { Layout.fillWidth: true }
                    SidebarButton {
                        text: qsTr("Cancel")
                        Layout.fillWidth: false
                        Layout.preferredWidth: 96
                        onClicked: dialog.confirmingReplace = false
                    }
                    SidebarButton {
                        text: qsTr("Replace")
                        Layout.fillWidth: false
                        Layout.preferredWidth: 110
                        fillColor: Theme.accent
                        strokeColor: Theme.accent
                        textColor: "white"
                        textWeight: Font.DemiBold
                        onClicked: {
                            dialog.confirmingReplace = false
                            Backend.replaceTranscriptWithRecap(dialog.draft)
                        }
                    }
                }
            }
        }
    }

    FileDialog {
        id: importSubtitleFileDialog
        title: qsTr("Import subtitle")
        fileMode: FileDialog.OpenFile
        nameFilters: ["Subtitle files (*.srt *.ttml *.vtt *.xml)", "All files (*)"]
        onAccepted: Backend.importSubtitles(selectedFile.toString())
    }
}
