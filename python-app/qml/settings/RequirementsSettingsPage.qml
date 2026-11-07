// qmllint disable
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CutPro 1.0
import "../theme"

SettingsPage {
    id: root

    // Don't re-probe the machine every time this page is shown. Backend restores
    // the last saved result at startup, so the page shows what was installed as
    // of the last check; the user refreshes it on demand with Recheck. The only
    // automatic probe is a one-time check when nothing has ever been saved (a
    // fresh install), so the list is never left empty. Both hooks are needed: in
    // the Settings dialog's StackLayout this page is built while hidden (onCompleted
    // sees visible=false), so the real trigger is becoming visible the first time;
    // once a result exists (length > 0) neither fires again.
    Component.onCompleted: maybeProbe()
    onVisibleChanged: maybeProbe()
    function maybeProbe() {
        if (visible && Backend.systemRequirements.length === 0)
            Backend.checkSystemRequirements()
    }

    SettingsSection {
        title: qsTr("Dependencies")
        help: qsTr("Whisper (offline transcription), Demucs and MDX-Net (vocal separation) are large and are not shipped in the installer. Click Download next to any of them - no separate Python and no commands needed. Whisper just downloads a model file and works straight away; Demucs and MDX-Net install into the app's own bundled Python. Demucs gives the cleanest separation but is heavy; MDX-Net is much lighter and faster on a weak computer. Pick which engine to use under Settings > Audio. Each download runs once; after it finishes the feature is ready. Gemini transcription, translation and text-to-speech work out of the box without this.")

        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: Backend.requirementsSatisfied
                  ? qsTr("Everything required for the built-in cloud features is installed. The optional engines below add offline transcription and vocal separation.")
                  : qsTr("Some components required for the cloud features are missing. Install them, then press Recheck.")
            color: Theme.textMuted
            font.pixelSize: Theme.fsSm
        }

        Repeater {
            model: Backend.systemRequirements
            delegate: Rectangle {
                id: reqCard
                required property var modelData
                Layout.fillWidth: true
                Layout.topMargin: 4
                implicitHeight: reqRow.implicitHeight + 20
                radius: Theme.radiusSm
                color: Theme.bgPrimary
                border.color: Theme.border
                readonly property bool ok: reqCard.modelData.status === "ok"
                readonly property bool checking: reqCard.modelData.status === "checking"
                readonly property bool installingThis: Backend.installingRequirement === reqCard.modelData.id

                RowLayout {
                    id: reqRow
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 12

                    Item {
                        Layout.preferredWidth: 18
                        Layout.preferredHeight: 18
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: 1
                        Image {
                            anchors.fill: parent
                            visible: !reqCard.checking
                            source: reqCard.ok ? "../assets/icons/check.svg"
                                               : "../assets/icons/alert-circle.svg"
                            sourceSize.width: 18
                            sourceSize.height: 18
                        }
                        BusyIndicator {
                            anchors.centerIn: parent
                            running: reqCard.checking
                            visible: running
                            implicitWidth: 18
                            implicitHeight: 18
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        RowLayout {
                            spacing: 6
                            Text {
                                text: reqCard.modelData.label
                                color: Theme.textPrimary
                                font.pixelSize: Theme.fsMd
                                font.weight: Font.DemiBold
                            }
                            Rectangle {
                                visible: !reqCard.modelData.required
                                radius: 3
                                color: Theme.bgSidebar
                                Layout.preferredHeight: 16
                                Layout.preferredWidth: optionalLabel.implicitWidth + 10
                                Text {
                                    id: optionalLabel
                                    anchors.centerIn: parent
                                    text: qsTr("optional")
                                    color: Theme.textMuted
                                    font.pixelSize: Theme.fsXs
                                }
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: reqCard.modelData.feature
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fsXs
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: reqCard.modelData.detail
                            color: reqCard.checking ? Theme.textMuted
                                                    : (reqCard.ok ? "#67b26f"
                                                                  : (reqCard.modelData.required ? "#e0b070"
                                                                                                : Theme.textMuted))
                            font.pixelSize: Theme.fsXs
                        }
                    }
                    // While this component is installing, show a spinner; if it
                    // can be installed, show a Download button; otherwise just
                    // the status word.
                    RowLayout {
                        Layout.alignment: Qt.AlignTop
                        spacing: 6
                        Text {
                            visible: reqCard.installingThis
                            text: qsTr("Installing...")
                            color: Theme.accent
                            font.pixelSize: Theme.fsXs
                            font.weight: Font.DemiBold
                            verticalAlignment: Text.AlignVCenter
                        }
                        BusyIndicator {
                            running: reqCard.installingThis
                            visible: running
                            implicitWidth: 18
                            implicitHeight: 18
                        }
                        Button {
                            visible: reqCard.modelData.installable && !reqCard.installingThis && !reqCard.checking
                            enabled: Backend.installingRequirement === ""
                            text: qsTr("Download")
                            implicitHeight: 28
                            implicitWidth: 96
                            onClicked: Backend.installRequirement(reqCard.modelData.id)
                            HoverHandler { cursorShape: parent.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
                            contentItem: RowLayout {
                                spacing: 5
                                Image {
                                    source: "../assets/icons/cloud-download.svg"
                                    sourceSize.width: 14
                                    sourceSize.height: 14
                                }
                                Text {
                                    text: qsTr("Download")
                                    color: "white"
                                    font.pixelSize: Theme.fsXs
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                            background: Rectangle {
                                color: parent.down ? Theme.hover : Theme.accent
                                border.color: Theme.accent
                                radius: Theme.radiusSm
                                opacity: parent.enabled ? 1.0 : 0.4
                            }
                        }
                        Text {
                            visible: reqCard.checking && !reqCard.installingThis
                            text: qsTr("Checking...")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fsXs
                            font.weight: Font.DemiBold
                            verticalAlignment: Text.AlignVCenter
                        }
                        Text {
                            visible: !reqCard.modelData.installable && !reqCard.installingThis && !reqCard.checking
                            text: reqCard.ok ? qsTr("Ready") : (reqCard.modelData.required ? qsTr("Required") : qsTr("Not installed"))
                            color: reqCard.ok ? "#67b26f"
                                              : (reqCard.modelData.required ? "#e0b070" : Theme.textMuted)
                            font.pixelSize: Theme.fsXs
                            font.weight: Font.DemiBold
                        }
                    }
                }
            }
        }

        // Live download progress while an optional engine installs. pip does not
        // emit a reliable overall percentage over a pipe, so the bar animates
        // (indeterminate) and the line under it shows exactly what pip is doing
        // right now - which package and how big.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4
            spacing: 6
            visible: Backend.installingRequirement !== "" || Backend.requirementInstallStatus !== ""
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text {
                    text: Backend.installingRequirement !== ""
                          ? qsTr("Installing %1...").arg(Backend.installingRequirement)
                          : qsTr("Install")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fsSm
                    font.weight: Font.DemiBold
                }
                Item { Layout.fillWidth: true }
            }
            ProgressBar {
                Layout.fillWidth: true
                visible: Backend.installingRequirement !== ""
                indeterminate: true
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: Backend.requirementInstallStatus
                color: Theme.textMuted
                font.pixelSize: Theme.fsXs
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 6
            spacing: 8
            BusyIndicator {
                running: Backend.requirementsChecking
                visible: running
                implicitWidth: 20
                implicitHeight: 20
            }
            Text {
                visible: Backend.requirementsChecking
                text: qsTr("Checking your computer...")
                color: Theme.textMuted
                font.pixelSize: Theme.fsSm
            }
            Item { Layout.fillWidth: true }
            Button {
                text: Backend.requirementsChecking ? qsTr("Checking...") : qsTr("Recheck")
                enabled: !Backend.requirementsChecking
                implicitHeight: 32
                implicitWidth: 110
                onClicked: Backend.checkSystemRequirements()
                HoverHandler { cursorShape: parent.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
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
                    opacity: parent.enabled ? 1.0 : 0.5
                }
            }
        }
    }
}
