//qmllint disable
// Native OS window that offers an available update, instead of the in-canvas
// dropdown that used to hang off the top-bar update icon. Shows the version
// jump and release notes, and two buttons: "Update now" streams the setup .exe
// and launches it (the app quits so the installer can replace its files), while
// "Update later" just closes this window - the top-bar update icon stays, so
// the user can reopen it any time this session.
//
// Non-modal on purpose: the download is large, and blocking the whole editor
// behind an application-modal window while it streams would be hostile. The
// user can keep working; when the download finishes the updater quits the app
// (same behaviour as the old dropdown).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CutPro 1.0
import "../../theme"

NativeModalWindow {
    id: root

    // The big download shouldn't lock the editor; float above it instead.
    modality: Qt.NonModal

    dialogTitle: qsTr("Update available")
    dialogWidth: 460
    dialogHeight: 460

    readonly property bool downloading: Updater.state === "downloading"

    Rectangle {
        anchors.fill: parent
        color: Theme.bgPanel

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // Header: version jump.
            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: 18
                spacing: 4
                Text {
                    text: qsTr("A new version of Cut Pro is available")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fsLg
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: qsTr("Version %1 is available. You have %2.")
                          .arg(Updater.latestVersion).arg(Updater.currentVersion)
                    color: Theme.textMuted
                    font.pixelSize: Theme.fsSm
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

            // Release notes, scrollable if the changelog is long.
            Flickable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: 18
                Layout.rightMargin: 18
                Layout.topMargin: 14
                Layout.bottomMargin: 4
                visible: Updater.releaseNotes !== ""
                contentHeight: notesText.implicitHeight
                clip: true
                Text {
                    id: notesText
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: Updater.releaseNotes
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fsXs
                    textFormat: Text.PlainText
                }
            }

            Item { Layout.fillHeight: true; visible: Updater.releaseNotes === "" }

            Text {
                Layout.fillWidth: true
                Layout.leftMargin: 18
                Layout.rightMargin: 18
                Layout.topMargin: 4
                wrapMode: Text.WordWrap
                visible: Updater.errorString !== ""
                text: Updater.errorString
                color: "#e0b070"
                font.pixelSize: Theme.fsXs
            }

            // Download progress, shown only while the installer streams down.
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 18
                Layout.rightMargin: 18
                Layout.topMargin: 8
                spacing: 4
                visible: root.downloading
                Text {
                    text: qsTr("Downloading... %1%").arg(Math.round(Updater.downloadProgress * 100))
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fsXs
                }
                Rectangle {
                    Layout.fillWidth: true
                    height: 4
                    radius: 2
                    color: Theme.bgPrimary
                    Rectangle {
                        height: parent.height
                        radius: parent.radius
                        width: parent.width * Updater.downloadProgress
                        color: Theme.accent
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border; Layout.topMargin: 12 }

            // Actions.
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 14
                spacing: 8

                Button {
                    id: laterButton
                    text: qsTr("Update later")
                    enabled: !root.downloading
                    implicitHeight: 32
                    implicitWidth: 110
                    HoverHandler { cursorShape: laterButton.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
                    contentItem: Text {
                        text: laterButton.text
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fsSm
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: laterButton.down ? Theme.hover : "transparent"
                        border.color: Theme.border
                        radius: Theme.radiusSm
                        opacity: laterButton.enabled ? 1.0 : 0.5
                    }
                    onClicked: root.close()
                }

                Item { Layout.fillWidth: true }

                Button {
                    id: updateNowButton
                    text: root.downloading ? qsTr("Downloading...") : qsTr("Update now")
                    enabled: !root.downloading
                    implicitHeight: 32
                    implicitWidth: 130
                    HoverHandler { cursorShape: updateNowButton.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
                    contentItem: Text {
                        text: updateNowButton.text
                        color: "#ffffff"
                        font.pixelSize: Theme.fsSm
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: updateNowButton.down ? Qt.darker(Theme.accent, 1.2) : Theme.accent
                        radius: Theme.radiusSm
                        opacity: updateNowButton.enabled ? 1.0 : 0.5
                    }
                    onClicked: Updater.downloadAndInstall()
                }
            }
        }
    }
}
