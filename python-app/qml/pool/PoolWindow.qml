pragma ComponentBehavior: Bound
// qmllint disable
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CutPro 1.0
import "../theme"
import "../components/common"
import "../settings"

NativeModalWindow {
    id: root
    dialogWidth: Math.min(1040, Screen.desktopAvailableWidth - 64)
    dialogHeight: Math.min(700, Screen.desktopAvailableHeight - 88)
    minimumWidth: Math.min(820, dialogWidth)
    minimumHeight: Math.min(560, dialogHeight)
    dialogTitle: qsTr("Pool Pro")
    flags: Qt.Dialog | Qt.FramelessWindowHint
    color: "transparent"
    readonly property real windowRadius: visibility === Window.Maximized ? 0 : 3

    readonly property color roomColor: "#0b0b0d"
    readonly property color nodeColor: "#17171b"
    readonly property color lineColor: "#29292f"
    readonly property color amber: "#ffa600"
    readonly property color good: "#00ce98"
    readonly property color bad: Theme.danger
    readonly property color ink: Theme.textPrimary
    readonly property color dim: "#96969f"
    readonly property var accounts: Pool.state.accounts || []
    readonly property int accountCount: accounts.length
    readonly property int readyCount: Pool.state.readyCount || 0
    readonly property bool coreRunning: Pool.state.core === "running"
    readonly property int inFlight: Pool.state.inFlight || 0
    readonly property var activeRequests: Pool.state.activeRequests || []
    readonly property string routingStatus: {
        if (!coreRunning) return qsTr("Router stopped")
        var protection = Pool.state.protection || {}
        if (protection.state === "cooldown") return qsTr("Cooling down · %1 s").arg(protection.retryAfterSeconds || 0)
        if (activeRequests.some(r => r.stage === "sending")) return qsTr("Sending requests")
        if (activeRequests.some(r => r.stage === "connecting")) return qsTr("Connecting to Google")
        if (activeRequests.some(r => r.stage === "retrying")) return qsTr("Retrying request")
        if (activeRequests.some(r => r.stage === "pacing")) return qsTr("Waiting to send")
        if (activeRequests.some(r => r.stage === "deduplicating")) return qsTr("Checking duplicates")
        if (activeRequests.some(r => r.stage === "rotating")) return qsTr("Choosing next account")
        if (inFlight > 0) return qsTr("Waiting for an account")
        if (readyCount === 0) return qsTr("No verified accounts ready")
        return qsTr("Ready for requests")
    }
    readonly property string sharedModel: Backend.appSettings.geminiModel || Backend.appSettings.transcriptionGeminiModel || "gemini-2.5-flash"
    readonly property var modelChoices: {
        var choices = Pool.models.slice()
        if (choices.indexOf(root.sharedModel) < 0) choices.unshift(root.sharedModel)
        return choices
    }
    property string removalId: ""
    property string removalEmail: ""

    function strategyLabel(value) {
        if (value === "least-used") return qsTr("Least used")
        if (value === "random") return qsTr("Random")
        return qsTr("Round robin")
    }
    function statusColor(status, enabled) {
        if (enabled === false) return dim
        return status === "ready" ? good : status === "cooldown" ? amber
             : status === "error" ? bad : dim
    }
    function statusLabel(status, enabled) {
        if (enabled === false) return qsTr("Disabled")
        return status === "ready" ? qsTr("Ready") : status === "cooldown" ? qsTr("Cooling down")
             : status === "error" ? qsTr("Needs attention") : qsTr("Verify Antigravity access")
    }
    function tierLabel(tier) {
        return tier === "paid" ? "PRO" : tier === "free" ? qsTr("FREE") : "WEB"
    }
    function chooseModel(value) {
        var next = JSON.parse(JSON.stringify(Backend.appSettings || {}))
        next.geminiModel = value
        next.transcriptionGeminiModel = value
        next.translationGeminiModel = value
        Backend.applyAppSettings(next)
    }
    function toggleMaximized() {
        if (visibility === Window.Maximized) showNormal()
        else showMaximized()
    }

    component PoolButton: Button {
        id: control
        property bool primary: false
        implicitHeight: 36
        implicitWidth: label.implicitWidth + 30
        padding: 0
        font.pixelSize: Theme.fsMd
        hoverEnabled: true
        background: Rectangle {
            radius: 18
            color: control.primary ? (control.hovered ? "#ffb52e" : root.amber)
                 : control.hovered ? "#242429" : root.nodeColor
            opacity: control.enabled ? 1 : 0.45
            border.width: control.primary ? 0 : 1
            border.color: control.activeFocus ? root.amber : root.lineColor
        }
        contentItem: Text {
            id: label
            text: control.text
            color: control.primary ? "#12100b" : (control.enabled ? root.ink : root.dim)
            font.pixelSize: control.font.pixelSize
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    component StatusChip: Rectangle {
        property string label: ""
        property color dotColor: root.dim
        implicitWidth: chipRow.implicitWidth + 24
        implicitHeight: 30
        radius: 15
        color: root.nodeColor
        border.color: root.lineColor
        Row {
            id: chipRow
            anchors.centerIn: parent
            spacing: 8
            Rectangle {
                width: 7; height: 7; radius: 4
                color: parent.parent.dotColor
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: parent.parent.label
                color: root.dim
                font.pixelSize: Theme.fsSm
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    component Counter: Row {
        property int value: 0
        property string label: ""
        spacing: 6
        Text {
            text: parent.value
            color: root.ink
            font.pixelSize: 16
            font.weight: Font.DemiBold
        }
        Text {
            text: parent.label
            color: root.dim
            font.pixelSize: Theme.fsSm
            anchors.baseline: parent.children[0].baseline
        }
    }

    Rectangle { anchors.fill: parent; color: root.roomColor; radius: root.windowRadius }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 1
        spacing: 0
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 64
            color: "#101013"
            topLeftRadius: root.windowRadius
            topRightRadius: root.windowRadius
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 22
                spacing: 12
                Text {
                    text: qsTr("Pool Pro")
                    color: root.ink
                    font.pixelSize: 20
                    font.weight: Font.DemiBold
                }
                StatusChip {
                    label: root.coreRunning ? qsTr("Router online") : qsTr("Router offline")
                    dotColor: root.coreRunning ? root.good : root.dim
                }
                WindowDragArea {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    ownerWindow: root
                    onToggleMaximized: root.toggleMaximized()
                }
                PoolButton {
                    text: qsTr("↻  Verify all")
                    enabled: !Pool.busy && !Pool.checkingModels && root.accountCount > 0 && root.inFlight === 0
                    onClicked: Pool.verifyAll()
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Test Antigravity access with one small request per account")
                }
                PoolButton {
                    primary: true
                    text: qsTr("+  Add Google account")
                    enabled: !Pool.busy
                    onClicked: Pool.addAccount()
                }
                WindowControls {
                    Layout.fillHeight: true
                    ownerWindow: root
                    onToggleMaximized: root.toggleMaximized()
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 52
            color: "#101013"
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 22
                anchors.topMargin: 6
                anchors.bottomMargin: 14
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    Text { text: qsTr("ROUTING"); color: root.dim; font.pixelSize: Theme.fsXs; font.letterSpacing: 1.3 }
                    Rectangle {
                        implicitWidth: strategyRow.implicitWidth + 8
                        implicitHeight: 32
                        radius: 16; color: root.nodeColor; border.color: root.lineColor
                        Row {
                            id: strategyRow
                            anchors.centerIn: parent
                            spacing: 2
                            Repeater {
                                model: ["round-robin", "least-used", "random"]
                                delegate: Button {
                                    id: strategyPill
                                    required property string modelData
                                    readonly property bool selected: (Pool.state.strategy || "round-robin") === modelData
                                    width: strategyText.implicitWidth + 22; height: 26
                                    enabled: !Pool.busy
                                    padding: 0
                                    onClicked: Pool.setStrategy(modelData)
                                    background: Rectangle {
                                        radius: 13
                                        color: strategyPill.selected ? root.amber
                                             : strategyPill.hovered ? "#29292e" : "transparent"
                                        border.width: strategyPill.activeFocus ? 1 : 0
                                        border.color: root.amber
                                    }
                                    contentItem: Text {
                                        id: strategyText
                                        text: root.strategyLabel(strategyPill.modelData)
                                        color: strategyPill.selected ? "#12100b" : root.dim
                                        font.pixelSize: Theme.fsSm
                                        font.weight: Font.Medium
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                }
                            }
                        }
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: qsTr("Google accounts · Gemini quota · No API key needed")
                        color: root.dim
                        font.pixelSize: Theme.fsSm
                        visible: root.width >= 900
                    }
                    PoolButton {
                        objectName: "pool-manage-accounts"
                        text: qsTr("Accounts  %1").arg(root.accountCount)
                        implicitHeight: 30
                        onClicked: accountDrawer.open()
                    }
                }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: root.lineColor }
        }

        PoolRoutingView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            onManageAccounts: accountDrawer.open()
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? noticeRow.implicitHeight + 22 : 0
            visible: Pool.poolMessage !== "" && !Pool.signingIn
            color: Pool.messageKind === "error" ? "#23151a" : Pool.messageKind === "success" ? "#11231e" : "#1b1a16"
            RowLayout {
                id: noticeRow
                anchors.left: parent.left; anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.margins: 22
                spacing: 12
                Text {
                    Layout.fillWidth: true
                    text: Pool.poolMessage
                    color: Pool.messageKind === "error" ? root.bad : Pool.messageKind === "success" ? root.good : root.dim
                    font.pixelSize: Theme.fsSm; wrapMode: Text.Wrap
                }
                PoolButton {
                    text: "×"; implicitWidth: 26; implicitHeight: 26
                    onClicked: Pool.clearMessage()
                    Accessible.name: qsTr("Dismiss message")
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 62
            color: "#101013"
            bottomLeftRadius: root.windowRadius
            bottomRightRadius: root.windowRadius
            Rectangle { width: parent.width; height: 1; color: root.lineColor }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 22; anchors.rightMargin: 22
                spacing: 20
                StatusChip {
                    label: Pool.signingIn ? qsTr("Signing in") : Pool.busy ? qsTr("Working…")
                         : root.coreRunning ? root.routingStatus
                         : root.accountCount === 0 ? qsTr("No accounts")
                         : root.readyCount === 0 ? qsTr("Needs attention") : qsTr("Router stopped")
                    dotColor: Pool.busy ? root.amber : root.coreRunning ? root.good
                            : root.accountCount > 0 && root.readyCount === 0 ? root.bad : root.dim
                }
                Item { Layout.fillWidth: true }
                Counter { value: root.readyCount; label: qsTr("ready") }
                Counter { value: root.inFlight; label: qsTr("active") }
                Counter { value: Pool.state.servedTotal || 0; label: qsTr("completed") }
                PoolButton {
                    text: root.coreRunning ? qsTr("Stop router") : qsTr("Start router")
                    enabled: !Pool.busy && (root.coreRunning || root.readyCount > 0)
                    implicitHeight: 30
                    onClicked: root.coreRunning ? Pool.stopCore() : Pool.ensureCore()
                }
            }
        }
    }

    Popup {
        id: accountDrawer
        objectName: "pool-account-drawer"
        parent: Overlay.overlay
        x: parent.width - width; y: 0
        width: 370; height: parent.height
        padding: 22
        modal: true; dim: true; focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { color: "#111115"; border.color: root.lineColor }
        contentItem: ColumnLayout {
            spacing: 16
            RowLayout {
                Layout.fillWidth: true
                Text { Layout.fillWidth: true; text: qsTr("Your accounts"); color: root.ink; font.pixelSize: 18; font.weight: Font.DemiBold }
                Button {
                    objectName: "pool-account-close"
                    implicitWidth: 30; implicitHeight: 30
                    padding: 0; flat: true; hoverEnabled: true
                    display: AbstractButton.IconOnly
                    icon.source: "qrc:/assets/icons/window-close.svg"
                    icon.width: 14; icon.height: 14; icon.color: hovered ? root.ink : root.dim
                    background: Item { }
                    onClicked: accountDrawer.close()
                    Accessible.name: qsTr("Close account manager")
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Close account manager")
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                }
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("%1 ready · %2 connected").arg(root.readyCount).arg(root.accountCount)
                color: root.dim; font.pixelSize: Theme.fsMd
            }
            Text { text: qsTr("Shared Gemini model"); color: root.ink; font.pixelSize: Theme.fsMd; font.weight: Font.DemiBold }
            SettingsComboBox {
                Layout.fillWidth: true
                model: root.modelChoices
                currentIndex: Math.max(0, root.modelChoices.indexOf(root.sharedModel))
                onActivated: root.chooseModel(currentText)
            }
            PoolButton {
                Layout.fillWidth: true
                text: Pool.checkingModels ? qsTr("Checking accounts…") : qsTr("Check all account models")
                enabled: root.accountCount > 0 && !Pool.busy && !Pool.checkingModels
                onClicked: Pool.checkModels()
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Checked accounts handle new transcription and translation requests. Unchecking an account lets its current requests finish.")
                color: root.dim; font.pixelSize: Theme.fsSm; wrapMode: Text.Wrap
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Each Google account needs Antigravity access. Open Antigravity, sign in with the same email and finish any setup or verification, then press Recheck access. Each recheck sends one small test request.")
                color: root.dim; font.pixelSize: Theme.fsSm; wrapMode: Text.Wrap
            }
            Text {
                Layout.fillWidth: true
                visible: Pool.modelStatus !== ""
                text: Pool.modelStatus
                color: root.dim; font.pixelSize: Theme.fsSm
                wrapMode: Text.Wrap; maximumLineCount: 4; elide: Text.ElideRight
            }
            ScrollView {
                Layout.fillWidth: true; Layout.fillHeight: true
                clip: true
                contentWidth: availableWidth
                Column {
                    width: parent.width
                    spacing: 10
                    Text {
                        width: parent.width
                        visible: root.accountCount === 0
                        text: qsTr("No accounts connected yet. Add your first Google account below.")
                        color: root.dim; font.pixelSize: Theme.fsMd; wrapMode: Text.Wrap
                        topPadding: 24; bottomPadding: 24
                    }
                    Repeater {
                        model: root.accounts
                        delegate: Rectangle {
                            id: accountCard
                            required property var modelData
                            width: parent.width
                            implicitHeight: cardContent.implicitHeight + 28
                            radius: 10; color: root.nodeColor; border.color: root.lineColor
                            ColumnLayout {
                                id: cardContent
                                anchors.left: parent.left; anchors.right: parent.right
                                anchors.top: parent.top; anchors.margins: 14
                                spacing: 9
                                RowLayout {
                                    Layout.fillWidth: true
                                    CheckBox {
                                        id: accountCheck
                                        objectName: "pool-account-enabled-" + accountCard.modelData.id
                                        implicitWidth: 18; implicitHeight: 20; padding: 0
                                        checked: accountCard.modelData.enabled !== false
                                        enabled: !Pool.busy && !Pool.checkingModels
                                        onClicked: Pool.setAccountEnabled(accountCard.modelData.id, checked)
                                        Accessible.name: qsTr("Use %1 for new requests").arg(accountCard.modelData.email)
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Use this account for new requests")
                                        indicator: Rectangle {
                                            width: 16; height: 16; radius: 3
                                            x: 0; y: (accountCheck.height - height) / 2
                                            color: accountCheck.checked ? root.amber : "transparent"
                                            border.color: accountCheck.activeFocus || accountCheck.checked ? root.amber : root.dim
                                            opacity: accountCheck.enabled ? 1 : 0.45
                                            Image {
                                                anchors.centerIn: parent
                                                source: "qrc:/assets/icons/check.svg"
                                                sourceSize.width: 12; sourceSize.height: 12
                                                visible: accountCheck.checked
                                            }
                                        }
                                        HoverHandler { cursorShape: accountCheck.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
                                    }
                                    Rectangle { implicitWidth: 8; implicitHeight: 8; radius: 4; color: root.statusColor(accountCard.modelData.status, accountCard.modelData.enabled) }
                                    Text {
                                        Layout.fillWidth: true; text: accountCard.modelData.email
                                        color: root.ink; font.pixelSize: Theme.fsMd; font.weight: Font.DemiBold; elide: Text.ElideRight
                                    }
                                    Text { text: root.tierLabel(accountCard.modelData.tier); color: root.dim; font.pixelSize: Theme.fsXs }
                                }
                                Text {
                                    text: accountCard.modelData.inFlight > 0 ? qsTr("Working…") : root.statusLabel(accountCard.modelData.status, accountCard.modelData.enabled)
                                    color: root.statusColor(accountCard.modelData.status, accountCard.modelData.enabled); font.pixelSize: Theme.fsSm
                                }
                                Text {
                                    Layout.fillWidth: true
                                    visible: text !== ""
                                    text: accountCard.modelData.statusDetail || ""
                                    color: root.dim; font.pixelSize: Theme.fsSm; wrapMode: Text.Wrap
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: accountCard.modelData.modelError || ((accountCard.modelData.availableModels || []).length > 0
                                          ? qsTr("%1 models available").arg(accountCard.modelData.availableModels.length) : qsTr("Models not checked yet"))
                                    color: root.dim; font.pixelSize: Theme.fsXs; wrapMode: Text.Wrap
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    PoolButton {
                                        objectName: "pool-account-setup-" + accountCard.modelData.id
                                        text: qsTr("Open Antigravity"); implicitHeight: 28
                                        onClicked: Qt.openUrlExternally(accountCard.modelData.verificationUrl || "https://antigravity.google/")
                                    }
                                    PoolButton {
                                        objectName: "pool-account-verify-" + accountCard.modelData.id
                                        text: qsTr("Recheck access"); implicitHeight: 28
                                        enabled: !Pool.busy && !Pool.checkingModels && !(accountCard.modelData.inFlight > 0)
                                        onClicked: Pool.verifyAccount(accountCard.modelData.id)
                                    }
                                    Item { Layout.fillWidth: true }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { Layout.fillWidth: true; text: qsTr("%1 requests served").arg(accountCard.modelData.served || 0); color: root.dim; font.pixelSize: Theme.fsXs }
                                    PoolButton {
                                        objectName: "pool-account-remove-" + accountCard.modelData.id
                                        text: qsTr("Remove"); implicitHeight: 28
                                        enabled: !Pool.busy
                                        onClicked: {
                                            root.removalId = accountCard.modelData.id
                                            root.removalEmail = accountCard.modelData.email
                                            removeDialog.open()
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            PoolButton {
                Layout.fillWidth: true
                primary: true; text: qsTr("+  Add Google account")
                enabled: !Pool.busy
                onClicked: { accountDrawer.close(); Pool.addAccount() }
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Sign-in opens in your browser. Google sessions stay encrypted on this device.")
                color: root.dim; font.pixelSize: Theme.fsSm; wrapMode: Text.Wrap
            }
        }
    }

    Popup {
        id: signInPopup
        parent: Overlay.overlay
        x: (parent.width - width) / 2; y: (parent.height - height) / 2
        width: 450
        padding: 28
        modal: true; dim: true
        visible: Pool.signingIn
        closePolicy: Popup.NoAutoClose
        background: Rectangle { radius: 18; color: "#141418"; border.color: root.lineColor }
        contentItem: ColumnLayout {
            spacing: 16
            BusyIndicator {
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: 44; implicitHeight: 44
                running: Pool.signingIn
                palette.dark: root.amber
            }
            Text {
                Layout.fillWidth: true
                text: Pool.authUrl ? qsTr("Continue in your browser") : qsTr("Preparing Google sign-in…")
                color: root.ink; font.pixelSize: 20; font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Choose a Google account and approve access.\nReturn here when you're done; the pool updates automatically.")
                color: root.dim; font.pixelSize: Theme.fsMd; wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
            }
            Text {
                Layout.fillWidth: true
                text: Pool.poolMessage
                color: Pool.messageKind === "error" ? root.bad : root.dim
                font.pixelSize: Theme.fsSm; wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
            }
            PoolButton {
                Layout.fillWidth: true
                primary: true; text: qsTr("Open browser again")
                enabled: Pool.authUrl !== ""
                onClicked: Pool.reopenSignIn()
            }
            TextArea {
                id: signInLink
                visible: false
                text: Pool.authUrl
                readOnly: true
            }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 10
                PoolButton {
                    text: qsTr("Copy sign-in link")
                    enabled: Pool.authUrl !== ""
                    onClicked: { signInLink.selectAll(); signInLink.copy(); signInLink.deselect() }
                }
                PoolButton { text: qsTr("Cancel"); onClicked: Pool.cancelSignIn() }
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Free and Pro accounts supported · No API key needed")
                color: root.dim; font.pixelSize: Theme.fsXs
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }

    Popup {
        id: removeDialog
        parent: Overlay.overlay
        x: (parent.width - width) / 2; y: (parent.height - height) / 2
        width: 360; padding: 24
        modal: true; dim: true
        background: Rectangle { radius: 14; color: "#17171b"; border.color: root.lineColor }
        contentItem: ColumnLayout {
            spacing: 18
            Text { text: qsTr("Remove this account?"); color: root.ink; font.pixelSize: 18; font.weight: Font.DemiBold }
            Text {
                Layout.fillWidth: true
                text: qsTr("%1 will leave the pool. To use it again, sign in with Google.").arg(root.removalEmail)
                color: root.dim; font.pixelSize: Theme.fsMd; wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                PoolButton { text: qsTr("Cancel"); onClicked: removeDialog.close() }
                PoolButton {
                    text: qsTr("Remove"); primary: true; enabled: !Pool.busy
                    onClicked: { Pool.removeAccount(root.removalId); removeDialog.close() }
                }
            }
        }
    }

    Timer {
        interval: root.coreRunning ? 3000 : 10000
        repeat: true
        running: root.visible && !Pool.busy
        onTriggered: Pool.refresh()
    }
    onVisibleChanged: if (visible) Pool.refresh()
    WindowResizeHandles { anchors.fill: parent; ownerWindow: root; z: 1000 }
    Rectangle { anchors.fill: parent; color: "transparent"; radius: root.windowRadius; border.color: root.lineColor; z: 999 }
}
