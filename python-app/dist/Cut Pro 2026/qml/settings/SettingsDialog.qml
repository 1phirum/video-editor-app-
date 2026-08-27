pragma ComponentBehavior: Bound
// qmllint disable
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CutPro 1.0
import "../theme"
import "../components/common"
import "../components/effects"
import "../components/export"
import "../components/lumetri"
import "../components/project"
import "../components/subtitles"
import "../components/timeline"

NativeModalWindow {
    id: root
    dialogTitle: "Settings"
    dialogWidth: 960
    dialogHeight: 690

    property int currentCategory: 0
    property var originalSettings: ({})
    property var draftSettings: ({})
    property bool committedClose: false
    readonly property var categories: [
        "General", "Appearance", "Audio", "Auto Save", "Media",
        "Media Cache", "Playback", "Timeline", "Transcription", "Translation"
    ]

    function cloneSettings(value) {
        return JSON.parse(JSON.stringify(value || {}))
    }

    function setDraftValue(key, value) {
        var next = cloneSettings(draftSettings)
        next[key] = value
        draftSettings = next

        if (key === "appearanceBrightness" || key === "accentColor") {
            Backend.applyAppSettings(draftSettings)
        }
    }

    function resetDraft() {
        draftSettings = cloneSettings(Backend.defaultAppSettings())
    }

    signal accepted()
    signal rejected()

    function open() {
        originalSettings = cloneSettings(Backend.appSettings)
        draftSettings = cloneSettings(Backend.appSettings)
        currentCategory = 0
        committedClose = false
        openNative()
    }

    function accept() {
        if (Backend.applyAppSettings(root.draftSettings)) {
            committedClose = true
            root.accepted()
            close()
        }
    }

    function reject() {
        Backend.applyAppSettings(originalSettings)
        committedClose = true
        root.rejected()
        close()
    }

    onClosing: function(closeEvent) {
        if (!committedClose)
            Backend.applyAppSettings(originalSettings)
        committedClose = false
    }

    color: Theme.bgPanel

    Rectangle {
        anchors.fill: parent
        color: Theme.bgPanel
        radius: Theme.radiusMd
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

        Rectangle {
            Layout.preferredWidth: 210
            Layout.fillHeight: true
            color: Theme.bgSidebar

            ListView {
                anchors.fill: parent
                anchors.margins: 10
                model: root.categories
                clip: true
                spacing: 2

                delegate: Rectangle {
                    id: categoryRow
                    required property int index
                    required property string modelData
                    width: ListView.view.width
                    height: 34
                    radius: Theme.radiusSm
                    color: root.currentCategory === index
                           ? Theme.hover : categoryHover.hovered
                             ? Qt.rgba(1, 1, 1, 0.035) : "transparent"
                    border.width: root.currentCategory === index ? 1 : 0
                    border.color: Theme.accent

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: categoryRow.modelData
                        color: root.currentCategory === categoryRow.index
                               ? Theme.textPrimary : Theme.textSecondary
                        font.pixelSize: Theme.fsSm
                    }
                    HoverHandler {
                        id: categoryHover
                        cursorShape: Qt.PointingHandCursor
                    }
                    TapHandler {
                        onTapped: root.currentCategory = categoryRow.index
                    }
                }
            }

            Rectangle {
                anchors.right: parent.right
                width: 1
                height: parent.height
                color: Theme.border
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.currentCategory

            GeneralSettingsPage {
                settings: root.draftSettings
                onSettingChanged: (key, value) => root.setDraftValue(key, value)
            }
            AppearanceSettingsPage {
                settings: root.draftSettings
                onSettingChanged: (key, value) => root.setDraftValue(key, value)
            }
            AudioSettingsPage {
                settings: root.draftSettings
                onSettingChanged: (key, value) => root.setDraftValue(key, value)
            }
            AutoSaveSettingsPage {
                settings: root.draftSettings
                onSettingChanged: (key, value) => root.setDraftValue(key, value)
            }
            MediaSettingsPage {
                settings: root.draftSettings
                onSettingChanged: (key, value) => root.setDraftValue(key, value)
            }
            MediaCacheSettingsPage {
                settings: root.draftSettings
                onSettingChanged: (key, value) => root.setDraftValue(key, value)
            }
            PlaybackSettingsPage {
                settings: root.draftSettings
                onSettingChanged: (key, value) => root.setDraftValue(key, value)
            }
            TimelineSettingsPage {
                settings: root.draftSettings
                onSettingChanged: (key, value) => root.setDraftValue(key, value)
            }
            TranscriptionSettingsPage {
                settings: root.draftSettings
                onSettingChanged: (key, value) => root.setDraftValue(key, value)
            }
            TranslationSettingsPage {
                settings: root.draftSettings
                onSettingChanged: (key, value) => root.setDraftValue(key, value)
            }
        }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            color: Theme.bgSidebar

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            spacing: 10

            SettingsFooterButton {
                text: "Reset"
                onClicked: root.resetDraft()
            }
            Item { Layout.fillWidth: true }
            SettingsFooterButton {
                text: "Cancel"
                onClicked: root.reject()
            }
            SettingsFooterButton {
                text: "OK"
                primary: true
                onClicked: {
                    root.accept()
                }
            }
        }

        Rectangle {
            anchors.top: parent.top
            width: parent.width
            height: 1
            color: Theme.border
        }
    }

    component SettingsFooterButton: Button {
        id: footerButton
        property bool primary: false
        implicitWidth: 110
        implicitHeight: 32
        padding: 0
        HoverHandler { cursorShape: parent.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
        contentItem: Text {
            text: footerButton.text
            color: footerButton.primary ? "white" : Theme.textPrimary
            font.pixelSize: Theme.fsSm
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            color: footerButton.down ? Theme.hover
                  : footerButton.primary ? Theme.accent : Theme.bgPrimary
            border.color: footerButton.primary ? Theme.accent : Theme.textMuted
            radius: height / 2
        }
        }
    }
}
