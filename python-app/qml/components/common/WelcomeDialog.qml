// qmllint disable
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CutPro 1.0
import "../../theme"

// First-run welcome + dependency check. Shows once on a fresh install (gated by
// the onboardingComplete app setting), walks a non-technical client through the
// optional engines that make transcription / vocal removal work, and offers a
// one-click install for each missing one. Reuses the same Backend requirement
// probe and installer the Settings > Requirements page drives, so nothing new is
// wired on the backend side. "Get Started" persists onboardingComplete = true.
Popup {
    id: root

    function begin() {
        Backend.checkSystemRequirements()
        root.open()
    }

    function finish() {
        var merged = {}
        var current = Backend.appSettings
        for (var k in current)
            merged[k] = current[k]
        merged["onboardingComplete"] = true
        Backend.applyAppSettings(merged)
        root.close()
    }

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    modal: true
    dim: true
    focus: true
    // Deliberately not closable by clicking outside / Escape: the client should
    // land on Get Started so the flag is written and it never nags again.
    closePolicy: Popup.NoAutoClose
    width: Math.min(560, (parent ? parent.width : 560) - 80)
    padding: 0

    background: Rectangle {
        color: Theme.bgPanel
        border.color: Theme.border
        radius: Theme.radiusSm
    }

    contentItem: ColumnLayout {
        spacing: 0

        // Header
        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: 20
            Layout.bottomMargin: 10
            spacing: 4
            Text {
                text: qsTr("Welcome to Cut Pro")
                color: Theme.textPrimary
                font.pixelSize: Theme.fsXl
                font.weight: Font.DemiBold
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Cut Pro turns long videos into short recaps: import, auto-transcribe, ")
                      + qsTr("caption and export. A few optional engines unlock the automatic ")
                      + qsTr("features. Install the ones you want now, or later from Settings.")
                color: Theme.textSecondary
                font.pixelSize: Theme.fsSm
                wrapMode: Text.WordWrap
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }

        // Requirement checklist (mirrors Settings > Requirements)
        Flickable {
            Layout.fillWidth: true
            Layout.margins: 16
            Layout.preferredHeight: Math.min(reqColumn.implicitHeight, 260)
            contentHeight: reqColumn.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            ColumnLayout {
                id: reqColumn
                width: parent.width
                spacing: 8

                Repeater {
                    model: Backend.systemRequirements
                    delegate: Rectangle {
                        required property var modelData
                        readonly property bool ok: modelData.status === "ok"
                        readonly property bool installingThis:
                            Backend.installingRequirement === modelData.id
                        Layout.fillWidth: true
                        implicitHeight: reqRow.implicitHeight + 16
                        radius: Theme.radiusSm
                        color: Theme.bgPrimary
                        border.width: 1
                        border.color: Theme.border

                        RowLayout {
                            id: reqRow
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 10

                            // Status dot
                            Rectangle {
                                width: 10; height: 10; radius: 5
                                Layout.alignment: Qt.AlignVCenter
                                color: parent.parent.ok ? Theme.meterGreen
                                       : (modelData.required ? "#e0b070" : Theme.textMuted)
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 1
                                RowLayout {
                                    spacing: 6
                                    Text {
                                        text: modelData.label
                                        color: Theme.textPrimary
                                        font.pixelSize: Theme.fsSm
                                        font.weight: Font.DemiBold
                                    }
                                    Text {
                                        visible: !modelData.required
                                        text: qsTr("optional")
                                        color: Theme.textMuted
                                        font.pixelSize: Theme.fsXs
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.feature
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fsXs
                                    wrapMode: Text.WordWrap
                                }
                            }

                            BusyIndicator {
                                visible: parent.parent.installingThis
                                running: visible
                                implicitWidth: 22; implicitHeight: 22
                            }
                            Button {
                                visible: modelData.installable && !parent.parent.installingThis
                                enabled: Backend.installingRequirement === ""
                                text: qsTr("Install")
                                implicitHeight: 30
                                implicitWidth: 84
                                onClicked: Backend.installRequirement(modelData.id)
                                HoverHandler { cursorShape: Qt.PointingHandCursor }
                                contentItem: Text {
                                    text: parent.text
                                    color: "white"
                                    font.pixelSize: Theme.fsSm
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                                background: Rectangle {
                                    color: parent.enabled ? Theme.accent : Theme.bgPrimary
                                    border.color: parent.enabled ? Theme.accent : Theme.border
                                    radius: Theme.radiusSm
                                }
                            }
                            Text {
                                visible: parent.parent.ok
                                text: qsTr("Ready")
                                color: Theme.meterGreen
                                font.pixelSize: Theme.fsSm
                            }
                        }
                    }
                }
            }
        }

        // Live install progress line, same source the Requirements page reads.
        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 18
            Layout.rightMargin: 18
            visible: Backend.installingRequirement !== "" || Backend.requirementInstallStatus !== ""
            text: Backend.installingRequirement !== ""
                  ? qsTr("Installing %1...").arg(Backend.installingRequirement)
                  : Backend.requirementInstallStatus
            color: Theme.textMuted
            font.pixelSize: Theme.fsXs
            elide: Text.ElideRight
        }

        // API-key nudge for the cloud translation / Gemini features.
        Text {
            Layout.fillWidth: true
            Layout.margins: 18
            Layout.topMargin: 10
            Layout.bottomMargin: 6
            text: qsTr("Tip: cloud translation and Gemini transcription need an API key. ")
                  + qsTr("Add yours any time under Settings > Translation.")
            color: Theme.textSecondary
            font.pixelSize: Theme.fsXs
            wrapMode: Text.WordWrap
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }

        // Footer
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            spacing: 8
            Button {
                text: qsTr("Recheck")
                implicitHeight: 32
                implicitWidth: 96
                enabled: !Backend.requirementsChecking
                onClicked: Backend.checkSystemRequirements()
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                contentItem: Text {
                    text: parent.text
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fsSm
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: parent.down ? Theme.hover : Theme.bgPrimary
                    border.color: Theme.border
                    radius: Theme.radiusSm
                }
            }
            Item { Layout.fillWidth: true }
            Button {
                text: qsTr("Get Started")
                implicitHeight: 32
                implicitWidth: 120
                onClicked: root.finish()
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                contentItem: Text {
                    text: parent.text
                    color: "white"
                    font.pixelSize: Theme.fsSm
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: parent.down ? Theme.hover : Theme.accent
                    border.color: Theme.accent
                    radius: Theme.radiusSm
                }
            }
        }
    }
}
