pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
import "../components/common"
import "../components/ai"

Rectangle {
    id: root
    required property var assistant
    signal closeRequested
    signal poolsRequested
    signal importRequested
    signal detachRequested(point position, point offset, bool dragging)
    signal dockRequested
    signal windowMoveRequested
    property bool detachable: false
    property bool floating: false
    property int currentTab: 0
    objectName: "aiAssistantPanel"
    color: Theme.bgPanel
    radius: floating ? 3 : 0
    focus: true
    Keys.onEscapePressed: event => {
        root.closeRequested();
        event.accepted = true;
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            Item {
                id: header
                Layout.fillWidth: true
                implicitHeight: 32
                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radiusSm
                    color: headerDrag.containsMouse && root.detachable ? Theme.bgCardHeader : "transparent"
                }
                RowLayout {
                    anchors.fill: parent
                    spacing: 8
                    IconButton { iconName: "sparkles"; restColor: Theme.accent; enabled: false }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("AI Assistant")
                        color: Theme.textPrimary
                        font.pixelSize: 14; font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Text {
                        visible: root.detachable && headerDrag.containsMouse
                        text: "⋮⋮"; color: Theme.textMuted; font.pixelSize: 12
                    }
                }
                MouseArea {
                    id: headerDrag
                    objectName: "aiHeaderDrag"
                    anchors.fill: parent
                    enabled: root.detachable
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton
                    cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                    property point pressPosition
                    property bool moving: false
                    onPressed: mouse => { pressPosition = Qt.point(mouse.x, mouse.y); moving = false }
                    onPositionChanged: mouse => {
                        if (!pressed || moving) return
                        const distance = Math.abs(mouse.x - pressPosition.x) + Math.abs(mouse.y - pressPosition.y)
                        if (distance < Qt.styleHints.startDragDistance) return
                        moving = true
                        if (root.floating) root.windowMoveRequested()
                        else root.detachRequested(mapToGlobal(mouse.x, mouse.y), mapToItem(root, mouse.x, mouse.y), true)
                    }
                    onReleased: moving = false
                    onCanceled: moving = false
                    onDoubleClicked: mouse => {
                        if (root.floating) root.dockRequested()
                        else root.detachRequested(mapToGlobal(mouse.x, mouse.y), mapToItem(root, mouse.x, mouse.y), false)
                    }
                    ToolTip.visible: containsMouse && !pressed
                    ToolTip.delay: 700
                    ToolTip.text: root.floating ? qsTr("Drag to move · double-click to dock") : qsTr("Drag to pop out · double-click to pop out")
                }
            }
            IconButton {
                objectName: "aiNewConversation"
                iconName: "plus"
                Accessible.name: qsTr("New conversation")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("New conversation")
                onClicked: root.assistant.newConversation()
            }
            IconButton {
                objectName: "aiDockToggle"
                visible: root.detachable
                iconName: root.floating ? "layout-panel" : "maximize-2"
                Accessible.name: root.floating ? qsTr("Dock AI panel") : qsTr("Pop out AI panel")
                ToolTip.visible: hovered
                ToolTip.text: root.floating ? qsTr("Dock AI panel") : qsTr("Pop out AI panel")
                onClicked: {
                    if (root.floating) root.dockRequested()
                    else root.detachRequested(header.mapToGlobal(0, 0), header.mapToItem(root, 0, 0), false)
                }
            }
            IconButton {
                objectName: "aiClose"
                iconName: "x"
                Accessible.name: qsTr("Close AI panel")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Close AI panel")
                onClicked: root.closeRequested()
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Repeater {
                model: [qsTr("Chat"), qsTr("Activity"), qsTr("Tools")]
                delegate: Button {
                    id: tab
                    required property int index
                    required property string modelData
                    objectName: "aiTab" + index
                    implicitWidth: 75
                    implicitHeight: 30
                    flat: true
                    contentItem: Text {
                        text: tab.modelData
                        color: root.currentTab === tab.index ? Theme.textPrimary : Theme.textSecondary
                        font.pixelSize: Theme.fsMd
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: "transparent"
                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width
                            height: 2
                            color: root.currentTab === tab.index ? Theme.accent : "transparent"
                        }
                    }
                    onClicked: root.currentTab = index
                }
            }
            Item {
                Layout.fillWidth: true
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            DarkComboBox {
                id: scopeBox
                objectName: "aiScope"
                Layout.fillWidth: true
                model: [qsTr("Selected clips"), qsTr("Current sequence"), qsTr("Project")]
                currentIndex: ["selection", "sequence", "project"].indexOf(root.assistant.scope)
                enabled: !root.assistant.busy && !root.assistant.awaitingReview
                Accessible.name: qsTr("AI editing scope")
                onActivated: root.assistant.scope = ["selection", "sequence", "project"][index]
            }
            IconButton {
                objectName: "aiPoolRefresh"
                iconName: "arrow-up-down"
                Accessible.name: qsTr("Refresh pool resources")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Refresh pool resources")
                onClicked: root.assistant.refreshResources()
            }
            Button {
                objectName: "aiOpenPools"
                text: qsTr("Pools")
                implicitWidth: 52
                implicitHeight: 30
                flat: true
                palette.windowText: Theme.accent
                palette.buttonText: Theme.accent
                onClicked: root.poolsRequested()
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Rectangle {
                width: 5
                height: 5
                radius: 3
                color: root.assistant.ready ? "#7dcea4" : Theme.textMuted
            }
            Text {
                Layout.fillWidth: true
                text: root.assistant.resourceStatus
                textFormat: Text.PlainText
                color: root.assistant.ready ? Theme.textSecondary : Theme.textMuted
                font.pixelSize: Theme.fsXs
                wrapMode: Text.WordWrap
            }
        }
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.surface("#454545")
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.currentTab
            ListView {
                id: conversation
                objectName: "aiConversation"
                clip: true
                spacing: 15
                model: root.assistant.messages
                ScrollBar.vertical: ScrollBar {}
                onCountChanged: Qt.callLater(() => {
                    if (conversation) conversation.positionViewAtEnd()
                })
                delegate: Column {
                    id: message
                    required property string speaker
                    required property string messageText
                    width: ListView.view.width
                    spacing: 6
                    Text {
                        text: message.speaker === "user" ? qsTr("You") : message.speaker === "error" ? qsTr("Request failed") : qsTr("AI Assistant")
                        color: message.speaker === "error" ? Theme.danger : Theme.textSecondary
                        font.pixelSize: Theme.fsSm
                        font.weight: Font.DemiBold
                    }
                    Rectangle {
                        width: parent.width
                        implicitHeight: messageBody.implicitHeight + 20
                        color: message.speaker === "user" ? Theme.surface("#383838") : "transparent"
                        radius: Theme.radiusMd
                        TextEdit {
                            id: messageBody
                            anchors.fill: parent
                            anchors.margins: 10
                            text: message.messageText
                            textFormat: TextEdit.PlainText
                            readOnly: true
                            selectByMouse: true
                            color: message.speaker === "error" ? Theme.danger : Theme.textPrimary
                            font.pixelSize: Theme.fsMd
                            wrapMode: TextEdit.Wrap
                        }
                    }
                }
                Connections {
                    target: root.assistant
                    function onStateChanged() {
                        Qt.callLater(() => {
                            if (conversation) conversation.positionViewAtEnd()
                        });
                    }
                }
                footer: Column {
                    width: conversation.width
                    spacing: 12
                    Item {
                        width: parent.width
                        height: conversation.count === 0 ? Math.max(8, (conversation.height - welcome.implicitHeight) / 2) : 8
                    }
                    Column {
                        id: welcome
                        width: parent.width
                        spacing: 13
                        visible: conversation.count === 0
                        Text {
                            width: parent.width
                            text: !root.assistant.ready ? qsTr("Connect your pool resources") : !root.assistant.hasMedia ? qsTr("Start with your media") : qsTr("What would you like to edit?")
                            color: Theme.textPrimary
                            font.pixelSize: 18
                            font.weight: Font.DemiBold
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            width: parent.width
                            text: root.assistant.ready ? qsTr("Describe the result. AI uses your editor tools and shows changes before applying them.") : qsTr("Open Pools to add or verify accounts, start the router, and check available models.")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fsMd
                            wrapMode: Text.WordWrap
                        }
                        DarkDialogButton {
                            text: qsTr("Import media")
                            visible: !root.assistant.hasMedia
                            onClicked: root.importRequested()
                        }
                        Repeater {
                            model: [
                                {
                                    title: qsTr("Tighten the edit"),
                                    prompt: qsTr("Find long pauses in the selected clips and propose a tighter edit.")
                                },
                                {
                                    title: qsTr("Add captions"),
                                    prompt: qsTr("Transcribe speech in this sequence using the account pool and add captions.")
                                },
                                {
                                    title: qsTr("Adjust color"),
                                    prompt: qsTr("Propose a warmer color balance for the selected clips.")
                                },
                                {
                                    title: qsTr("Prepare export"),
                                    prompt: qsTr("Suggest export settings for this sequence. Ask me for the destination before exporting.")
                                }
                            ]
                            delegate: Button {
                                id: suggestion
                                required property var modelData
                                width: parent.width
                                implicitHeight: 39
                                text: modelData.title
                                palette.buttonText: Theme.textPrimary
                                background: Rectangle {
                                    radius: Theme.radiusSm
                                    color: suggestion.hovered ? Theme.hover : Theme.surface("#383838")
                                }
                                onClicked: composer.prefill(modelData.prompt)
                            }
                        }
                    }
                    ColumnLayout {
                        width: parent.width
                        visible: root.assistant.awaitingReview
                        spacing: 8
                        Text {
                            text: qsTr("Proposed changes")
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fsLg
                            font.weight: Font.DemiBold
                        }
                        AiActionList {
                            Layout.fillWidth: true
                            Layout.preferredHeight: Math.min(190, contentHeight)
                            assistant: root.assistant
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            DarkDialogButton {
                                objectName: "aiApplyChanges"
                                text: qsTr("Apply changes")
                                enabled: root.assistant.mode === "Edit"
                                highlighted: true
                                onClicked: root.assistant.applyPlan()
                            }
                            DarkDialogButton {
                                objectName: "aiRevise"
                                text: qsTr("Revise")
                                onClicked: {
                                    root.assistant.stop();
                                    composer.prefill(qsTr("Revise the previous plan: "));
                                }
                            }
                        }
                    }
                    RowLayout {
                        width: parent.width
                        visible: root.assistant.state === "completed"
                        DarkDialogButton {
                            text: qsTr("Inspect timeline")
                            onClicked: root.closeRequested()
                        }
                        DarkDialogButton {
                            text: qsTr("Undo last edit")
                            onClicked: root.assistant.undoLastEdit()
                        }
                    }
                }
            }
            ColumnLayout {
                Text {
                    Layout.fillWidth: true
                    text: root.assistant.status || qsTr("No assistant actions yet")
                    textFormat: Text.PlainText
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fsLg
                    wrapMode: Text.WordWrap
                }
                AiActionList {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    assistant: root.assistant
                    showToolNames: true
                }
            }
            ListView {
                id: tools
                objectName: "aiTools"
                clip: true
                spacing: 10
                model: root.assistant.toolGroups
                ScrollBar.vertical: ScrollBar {}
                delegate: Column {
                    id: group
                    required property var modelData
                    width: ListView.view.width
                    spacing: 7
                    Text {
                        text: group.modelData.name
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fsMd
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: qsTr("%1 available editor actions").arg(group.modelData.count)
                        color: Theme.textMuted
                        font.pixelSize: Theme.fsSm
                    }
                    Rectangle {
                        width: parent.width
                        height: 1
                        color: Theme.surface("#454545")
                    }
                }
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            visible: root.assistant.busy
            spacing: 5
            RowLayout {
                Layout.fillWidth: true
                BusyIndicator {
                    implicitWidth: 20
                    implicitHeight: 20
                    running: root.assistant.busy
                }
                Text {
                    Layout.fillWidth: true
                    text: root.assistant.status
                    textFormat: Text.PlainText
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fsSm
                    elide: Text.ElideRight
                }
                DarkDialogButton {
                    objectName: "aiStop"
                    text: qsTr("Stop")
                    onClicked: root.assistant.stop()
                }
            }
            ProgressBar {
                Layout.fillWidth: true
                visible: root.assistant.state === "running"
                indeterminate: root.assistant.progress < 0
                value: Math.max(0, root.assistant.progress)
            }
        }
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.surface("#454545")
        }
        AiComposer {
            id: composer
            Layout.fillWidth: true
            assistant: root.assistant
        }
    }
}
