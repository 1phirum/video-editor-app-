pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CutPro 1.0
import "../theme"
import "../components/common"
import "../components/subtitles"

// Draft preferences belong to SettingsDialog; connection and job state belong
// to the native Mcp singleton. Never simulate connected agents or job progress.
ScrollView {
    id: root
    property var settings: ({})
    signal settingChanged(string key, var value)
    property int currentTab: 0
    property bool revealKey: false
    property bool showSetup: false
    property string copyStatus: ""
    readonly property int draftPort: Number(settings.mcpPort || 20129)
    readonly property bool draftEnabled: Boolean(settings.mcpEnabled)
    readonly property bool pending: draftEnabled !== Boolean(Backend.appSettings.mcpEnabled)
                                    || draftPort !== Number(Backend.appSettings.mcpPort || 20129)
    readonly property string draftEndpoint: "http://127.0.0.1:" + draftPort + "/mcp"
    readonly property var capabilities: [
        { title: qsTr("Projects & media"), detail: qsTr("Import video and audio; open and save projects.") },
        { title: qsTr("Timeline & playback"), detail: qsTr("Move, trim, split and select clips; seek and play.") },
        { title: qsTr("Effects & color"), detail: qsTr("Add effects, adjust color and animate keyframes.") },
        { title: qsTr("Transcription & translation"), detail: qsTr("Transcribe speech, translate and edit subtitles.") },
        { title: qsTr("Voice & audio"), detail: qsTr("Generate voices, fit speech and adjust audio.") },
        { title: qsTr("Export & jobs"), detail: qsTr("Export video, track progress and cancel jobs.") }
    ]
    clip: true
    onVisibleChanged: if (!visible) revealKey = false
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

    function copy(kind) {
        copyStatus = Mcp.copyConnectionDetails(kind) ? qsTr("Copied") : qsTr("Could not copy")
        copiedTimer.restart()
    }
    function remaining(seconds) {
        var value = Math.max(0, Math.floor(Number(seconds)))
        return Math.floor(value / 60) + ":" + String(value % 60).padStart(2, "0")
    }
    Timer { id: copiedTimer; interval: 2000; onTriggered: root.copyStatus = "" }

    ColumnLayout {
        id: content
        x: 24; y: 16
        width: Math.max(0, root.availableWidth - 48)
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true; spacing: 4
                Text { Layout.fillWidth: true; text: qsTr("MCP"); color: Theme.textPrimary; font.pixelSize: 22; font.weight: Font.DemiBold }
                Text { Layout.fillWidth: true; text: qsTr("Let AI agents edit your project in Cut Pro."); color: Theme.textSecondary; font.pixelSize: Theme.fsMd; wrapMode: Text.WordWrap }
            }
            Rectangle {
                implicitWidth: 96; implicitHeight: 26; radius: 13
                color: Mcp.running ? Qt.rgba(0.3, 0.65, 0.45, 0.14) : Theme.bgCard
                Row {
                    anchors.centerIn: parent; spacing: 8
                    Rectangle { width: 6; height: 6; radius: 3; anchors.verticalCenter: parent.verticalCenter; color: Mcp.running ? "#7dcea4" : Theme.textMuted }
                    Text { text: Mcp.running ? qsTr("Running") : qsTr("Off"); color: Mcp.running ? "#7dcea4" : Theme.textSecondary; font.pixelSize: Theme.fsSm }
                }
            }
        }

        Card {
            Layout.fillWidth: true; implicitHeight: 60
            RowLayout {
                anchors.fill: parent; anchors.margins: 16; spacing: 14
                Text { text: "MCP"; color: Theme.accent; font.pixelSize: Theme.fsMd; font.weight: Font.DemiBold }
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 4
                    Text { Layout.fillWidth: true; text: qsTr("Enable MCP server"); color: Theme.textPrimary; font.pixelSize: Theme.fsLg; font.weight: Font.DemiBold }
                    Text { Layout.fillWidth: true; text: qsTr("Allow connected agents to use your editing tools."); color: Theme.textSecondary; font.pixelSize: Theme.fsSm; wrapMode: Text.WordWrap }
                }
                CaptionSwitch {
                    objectName: "mcpEnabledSwitch"
                    checked: root.draftEnabled
                    onToggled: root.settingChanged("mcpEnabled", checked)
                    Accessible.name: qsTr("Enable MCP server")
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true; spacing: 12
            Repeater {
                model: [qsTr("Connection"), qsTr("Available tools"), qsTr("Activity")]
                delegate: Button {
                    id: tab
                    required property int index
                    required property string modelData
                    objectName: "mcpTab" + index
                    text: modelData; implicitWidth: Math.max(100, contentItem.implicitWidth + 20); implicitHeight: 34
                    padding: 10
                    contentItem: Text { text: tab.text; color: root.currentTab === tab.index ? Theme.textPrimary : Theme.textSecondary; font.pixelSize: Theme.fsMd; font.weight: root.currentTab === tab.index ? Font.DemiBold : Font.Normal; verticalAlignment: Text.AlignVCenter }
                    background: Rectangle { color: "transparent"; Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 2; color: Theme.accent; visible: root.currentTab === tab.index } }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    onClicked: root.currentTab = index
                }
            }
            Item { Layout.fillWidth: true }
            Text { text: root.copyStatus; color: Theme.textSecondary; font.pixelSize: Theme.fsSm }
        }

        ColumnLayout {
            visible: root.currentTab === 0
            Layout.fillWidth: true; spacing: 10
            Card {
                Layout.fillWidth: true; implicitHeight: details.implicitHeight + 24
                ColumnLayout {
                    id: details
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 12; spacing: 8
                    Text { text: qsTr("Connection details"); color: Theme.textPrimary; font.pixelSize: Theme.fsLg; font.weight: Font.DemiBold }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 12
                        FieldLabel { text: qsTr("Server URL") }
                        ConnectionField { Layout.fillWidth: true; text: root.draftEndpoint; font.family: Theme.monoFont }
                        ActionButton { text: qsTr("Copy"); enabled: !root.pending; onClicked: root.copy("url") }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 12
                        FieldLabel { text: qsTr("Connection key") }
                        ConnectionField { Layout.fillWidth: true; text: Mcp.connectionKey; echoMode: root.revealKey ? TextInput.Normal : TextInput.Password; selectByMouse: root.revealKey }
                        ActionButton { text: root.revealKey ? qsTr("Hide") : qsTr("Show"); enabled: Mcp.connectionKey.length > 0; onClicked: root.revealKey = !root.revealKey }
                        ActionButton { text: qsTr("Copy"); enabled: Mcp.connectionKey.length > 0; onClicked: root.copy("key") }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 12
                        FieldLabel { text: qsTr("Port") }
                        SettingsSpinBox { objectName: "mcpPortField"; from: 1024; to: 65535; value: root.draftPort; onValueModified: root.settingChanged("mcpPort", value) }
                        Text { text: qsTr("This computer only"); color: Theme.textMuted; font.pixelSize: Theme.fsSm }
                        Item { Layout.fillWidth: true }
                        Text { text: qsTr("Streamable HTTP"); color: Theme.textMuted; font.pixelSize: Theme.fsSm }
                    }
                }
            }
            Card {
                Layout.fillWidth: true; implicitHeight: setup.implicitHeight + 24
                ColumnLayout {
                    id: setup
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 12; spacing: 6
                    RowLayout {
                        Layout.fillWidth: true; spacing: 12
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 6
                            Text { text: Mcp.running ? qsTr("Connect your AI agent") : qsTr("Start the server to connect"); color: Theme.textPrimary; font.pixelSize: Theme.fsLg; font.weight: Font.DemiBold }
                            Text { Layout.fillWidth: true; text: Mcp.running ? qsTr("Add Cut Pro to your agent’s MCP connections.") : qsTr("Turn on the server above, then click OK."); color: Theme.textSecondary; font.pixelSize: Theme.fsSm; wrapMode: Text.WordWrap }
                        }
                        ActionButton { text: qsTr("Copy config"); highlighted: true; enabled: Mcp.running && !root.pending; onClicked: root.copy("config") }
                        ActionButton { text: qsTr("Setup guide"); onClicked: root.showSetup = !root.showSetup }
                    }
                    Text {
                        Layout.fillWidth: true; visible: root.showSetup; wrapMode: Text.WordWrap
                        text: qsTr("Keep Cut Pro open. In your agent, add an HTTP MCP server using the Server URL and an Authorization header with Bearer followed by your connection key. Copy config provides both. Changes to the switch or port apply when you click OK.")
                        color: Theme.textSecondary; font.pixelSize: Theme.fsSm
                    }
                }
            }
            Card {
                Layout.fillWidth: true; implicitHeight: 54
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 12; spacing: 4
                    Text { text: Mcp.lastClient.length ? Mcp.lastClient : qsTr("No agent requests yet"); textFormat: Text.PlainText; color: Theme.textPrimary; font.pixelSize: Theme.fsMd; font.weight: Font.DemiBold }
                    Text { text: Mcp.lastSeen.length ? qsTr("Last request at %1").arg(Mcp.lastSeen) : qsTr("Agent activity appears here when it connects."); color: Theme.textSecondary; font.pixelSize: Theme.fsSm }
                }
            }
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap
                text: root.pending ? qsTr("Changes apply when you click OK.") : Mcp.status !== "Running" && Mcp.status !== "Off" ? Mcp.status : qsTr("You can stop agent access at any time by turning off the server.")
                color: root.pending ? Theme.textSecondary : Mcp.status !== "Running" && Mcp.status !== "Off" ? Theme.danger : Theme.textMuted
                font.pixelSize: Theme.fsSm
            }
        }

        ColumnLayout {
            visible: root.currentTab === 1
            Layout.fillWidth: true; spacing: 14
            Text { text: qsTr("Your agent can discover %1 editing tools.").arg(Mcp.toolCount); color: Theme.textSecondary; font.pixelSize: Theme.fsMd }
            GridLayout {
                Layout.fillWidth: true; columns: width >= 520 ? 2 : 1; rowSpacing: 12; columnSpacing: 12
                Repeater {
                    model: root.capabilities
                    delegate: Card {
                        id: capability
                        required property var modelData
                        Layout.fillWidth: true; Layout.preferredWidth: 300; implicitHeight: 80
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 14; spacing: 8
                            Text { Layout.fillWidth: true; text: capability.modelData.title; color: Theme.textPrimary; font.pixelSize: Theme.fsLg; font.weight: Font.DemiBold; elide: Text.ElideRight }
                            Text { Layout.fillWidth: true; text: capability.modelData.detail; color: Theme.textSecondary; font.pixelSize: Theme.fsSm; wrapMode: Text.WordWrap }
                            Item { Layout.fillHeight: true }
                        }
                    }
                }
            }
            Text { Layout.fillWidth: true; text: qsTr("Timeline edits use Cut Pro’s existing undo and redo."); color: Theme.textMuted; font.pixelSize: Theme.fsSm; wrapMode: Text.WordWrap }
        }

        ColumnLayout {
            visible: root.currentTab === 2
            Layout.fillWidth: true; spacing: 12
            RowLayout {
                Layout.fillWidth: true
                Text { text: qsTr("Live activity"); color: Theme.textPrimary; font.pixelSize: Theme.fsLg; font.weight: Font.DemiBold }
                Item { Layout.fillWidth: true }
                Text { text: qsTr("%1 jobs running").arg(Mcp.jobs.length); color: Theme.textSecondary; font.pixelSize: Theme.fsSm }
            }
            Text { visible: Mcp.jobs.length === 0; text: qsTr("No jobs running. Agent requests will appear below."); color: Theme.textMuted; font.pixelSize: Theme.fsMd }
            Repeater {
                model: root.currentTab === 2 && root.visible ? Mcp.jobs : []
                delegate: Card {
                    id: job
                    required property var modelData
                    Layout.fillWidth: true; implicitHeight: jobBody.implicitHeight + 28
                    ColumnLayout {
                        id: jobBody
                        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 14; spacing: 8
                        RowLayout {
                            Layout.fillWidth: true
                            Text { Layout.fillWidth: true; text: job.modelData.title; color: Theme.textPrimary; font.pixelSize: Theme.fsLg; font.weight: Font.DemiBold }
                            Text { visible: !job.modelData.indeterminate && job.modelData.progress !== undefined; text: Math.round(Number(job.modelData.progress || 0) * 100) + "%"; color: Theme.textPrimary; font.pixelSize: Theme.fsMd }
                            ActionButton { text: qsTr("Cancel"); visible: job.modelData.id !== "voice"; onClicked: Mcp.cancelJob(job.modelData.id) }
                        }
                        Text { Layout.fillWidth: true; visible: text.length > 0; text: job.modelData.status || ""; textFormat: Text.PlainText; color: Theme.textSecondary; font.pixelSize: Theme.fsSm; wrapMode: Text.WordWrap }
                        ProgressBar {
                            Layout.fillWidth: true; from: 0; to: 1; value: Number(job.modelData.progress || 0); indeterminate: Boolean(job.modelData.indeterminate)
                            background: Rectangle { implicitHeight: 4; radius: 2; color: Theme.bgPrimary }
                            contentItem: Item { implicitHeight: 4; Rectangle { width: jobBody.width * (job.modelData.indeterminate ? 0.3 : Number(job.modelData.progress || 0)); height: 4; radius: 2; color: Theme.accent } }
                        }
                        Text {
                            Layout.fillWidth: true; visible: job.modelData.id === "export"
                            text: (Number(job.modelData.speed) > 0 ? qsTr("%1× speed").arg(Number(job.modelData.speed).toFixed(1)) : qsTr("Calculating speed"))
                                  + (Number(job.modelData.remainingSeconds) > 0 ? "  ·  " + qsTr("%1 remaining").arg(root.remaining(job.modelData.remainingSeconds)) : "")
                            color: Theme.textSecondary; font.pixelSize: Theme.fsSm
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Text { text: qsTr("Recent agent actions"); color: Theme.textSecondary; font.pixelSize: Theme.fsMd }
                Item { Layout.fillWidth: true }
                ActionButton { text: qsTr("Clear"); enabled: Mcp.activities.length > 0; onClicked: Mcp.clearActivity() }
            }
            Text { visible: Mcp.activities.length === 0; text: qsTr("No editing requests yet."); color: Theme.textMuted; font.pixelSize: Theme.fsSm }
            ListView {
                objectName: "mcpActionHistory"
                Layout.fillWidth: true; Layout.preferredHeight: Math.min(180, Mcp.activities.length * 34)
                model: root.currentTab === 2 && root.visible ? Mcp.activities : []; clip: true; reuseItems: true
                delegate: RowLayout {
                    id: action
                    required property var modelData
                    width: ListView.view.width; height: 34; spacing: 10
                    Text { text: action.modelData.failed ? "!" : "✓"; color: action.modelData.failed ? Theme.danger : "#7dcea4"; font.pixelSize: Theme.fsMd }
                    Text { Layout.fillWidth: true; text: action.modelData.title; color: Theme.textPrimary; font.pixelSize: Theme.fsSm; elide: Text.ElideRight }
                    Text { text: action.modelData.status; color: Theme.textSecondary; font.pixelSize: Theme.fsSm }
                    Text { text: action.modelData.time; color: Theme.textMuted; font.pixelSize: Theme.fsSm }
                }
            }
        }
        Item { Layout.preferredHeight: 10 }
    }
    component Card: Rectangle { color: Theme.bgCard; radius: Theme.radiusMd }
    component FieldLabel: Text { Layout.preferredWidth: 116; color: Theme.textSecondary; font.pixelSize: Theme.fsMd }
    component ConnectionField: TextField {
        readOnly: true; selectByMouse: true; implicitHeight: 32; color: Theme.textPrimary; font.pixelSize: Theme.fsMd
        background: Rectangle { color: Theme.bgPrimary; radius: Theme.radiusSm; border.color: parent.activeFocus ? Theme.accent : Theme.outline }
    }
    component ActionButton: DarkDialogButton { implicitWidth: Math.max(64, contentItem.implicitWidth + 24); implicitHeight: 32 }
}
