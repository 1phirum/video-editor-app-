pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CutPro 1.0
import "../theme"

Item {
    id: root
    signal manageAccounts()
    readonly property var accounts: Pool.state.accounts || []
    readonly property var activeRequests: Pool.state.activeRequests || []
    readonly property var requests: activeRequests.concat(Pool.state.recentRequests || []).slice(0, 20)
    readonly property bool online: Pool.state.core === "running"
    readonly property var protection: Pool.state.protection || {}
    readonly property bool windowVisible: Window.window && Window.window.visible
        && Window.window.visibility !== Window.Minimized
    readonly property color amber: "#ffa600"
    readonly property color good: "#00ce98"
    readonly property color dim: "#96969f"
    readonly property color panelColor: "#131317"
    readonly property color borderColor: "#29292f"
    readonly property real hubX: width / 2
    readonly property real hubY: height / 2 - 16
    property real flowPhase: 0

    function stageLabel(stage) {
        if (stage === "sending") return qsTr("Sending")
        if (stage === "connecting") return qsTr("Connecting")
        if (stage === "retrying") return qsTr("Retrying")
        if (stage === "pacing") return qsTr("Waiting to send")
        if (stage === "deduplicating") return qsTr("Checking duplicates")
        if (stage === "rotating") return qsTr("Rotating")
        if (stage === "completed") return qsTr("Completed")
        if (stage === "failed") return qsTr("Failed")
        return qsTr("Queued")
    }
    function accountName(id) {
        var account = accounts.find(a => a.id === id)
        return account ? account.email : qsTr("Choosing account")
    }
    function accountRequest(id) { return activeRequests.find(r => r.accountId === id) }
    function statusLabel(status, enabled) {
        if (enabled === false) return qsTr("Disabled")
        if (status === "ready") return qsTr("Ready")
        if (status === "cooldown") return qsTr("Cooling down")
        if (status === "error") return qsTr("Needs attention")
        return qsTr("Verify Antigravity access")
    }
    function statusColor(status, enabled) {
        if (enabled === false) return dim
        return status === "ready" ? good : status === "cooldown" ? amber
            : status === "error" ? Theme.danger : dim
    }

    NumberAnimation on flowPhase {
        from: 0; to: 1; duration: 1600; loops: Animation.Infinite
        running: root.visible && root.windowVisible
            && root.activeRequests.some(r => r.accountId && r.stage === "sending")
    }
    onFlowPhaseChanged: routes.requestPaint()
    onActiveRequestsChanged: routes.requestPaint()
    onAccountsChanged: routes.requestPaint()

    Canvas {
        id: routes
        anchors.fill: parent
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            var x1 = flowLayout.x + requestsPanel.x + requestsPanel.width
            var x2 = root.hubX - 96
            ctx.lineWidth = 1.5
            ctx.strokeStyle = root.activeRequests.length ? "#956019" : "#303039"
            ctx.beginPath(); ctx.moveTo(x1, root.hubY); ctx.lineTo(x2, root.hubY); ctx.stroke()
            if (root.activeRequests.some(r => r.stage === "sending")) {
                ctx.fillStyle = root.amber
                ctx.beginPath(); ctx.arc(x1 + (x2 - x1) * root.flowPhase, root.hubY, 3, 0, Math.PI * 2); ctx.fill()
            }
            for (var i = 0; i < root.accounts.length; ++i) {
                var account = root.accounts[i]
                var y = flowLayout.y + accountsPanel.y + 52 + i * 76 + 34 - accountList.contentY
                if (y < flowLayout.y + accountsPanel.y + 52 || y > flowLayout.y + accountsPanel.y + accountsPanel.height - 12) continue
                var startX = root.hubX + 96, endX = flowLayout.x + accountsPanel.x
                var active = root.accountRequest(account.id)
                ctx.strokeStyle = active ? "#99641b" : "#303039"
                ctx.beginPath(); ctx.moveTo(startX, root.hubY)
                ctx.bezierCurveTo(startX + 36, root.hubY, endX - 36, y, endX, y); ctx.stroke()
                if (active && active.stage === "sending") {
                    var t = root.flowPhase, q = 1 - t
                    var px = q*q*q*startX + 3*q*q*t*(startX+36) + 3*q*t*t*(endX-36) + t*t*t*endX
                    var py = q*q*q*root.hubY + 3*q*q*t*root.hubY + 3*q*t*t*y + t*t*t*y
                    ctx.fillStyle = root.amber
                    ctx.beginPath(); ctx.arc(px, py, 3, 0, Math.PI * 2); ctx.fill()
                }
            }
        }
    }

    RowLayout {
        id: flowLayout
        anchors.fill: parent; anchors.margins: 22
        spacing: 24

        Rectangle {
            id: requestsPanel
            Layout.preferredWidth: root.width * 0.29
            Layout.fillHeight: true
            color: root.panelColor; radius: 12; border.color: root.borderColor
            RowLayout {
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                anchors.margins: 16; height: 24
                Text { Layout.fillWidth: true; text: qsTr("Requests"); color: Theme.textPrimary; font.pixelSize: 15; font.weight: Font.DemiBold }
                Text { text: qsTr("%1 active").arg(Pool.state.inFlight || 0); color: root.activeRequests.length ? root.amber : root.dim; font.pixelSize: Theme.fsXs }
            }
            ListView {
                id: requestList
                objectName: "pool-request-list"
                anchors.fill: parent; anchors.margins: 10; anchors.topMargin: 52
                clip: true; spacing: 6
                model: root.requests
                ScrollBar.vertical: ScrollBar { }
                delegate: Rectangle {
                    id: requestCard
                    required property var modelData
                    readonly property bool working: modelData.stage !== "completed" && modelData.stage !== "failed"
                    width: ListView.view.width; height: 76
                    color: working ? "#1c1913" : "#18181d"; radius: 8
                    border.color: working ? "#49351d" : "transparent"
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 12; spacing: 4
                        RowLayout {
                            Layout.fillWidth: true
                            Text { Layout.fillWidth: true; text: requestCard.modelData.model; color: Theme.textPrimary; font.pixelSize: Theme.fsSm; font.weight: Font.Medium; elide: Text.ElideRight }
                            Text { text: "#" + String(requestCard.modelData.id).slice(-5); color: root.dim; font.pixelSize: 9 }
                        }
                        Text { Layout.fillWidth: true; text: root.accountName(requestCard.modelData.accountId); color: root.dim; font.pixelSize: Theme.fsXs; elide: Text.ElideRight }
                        RowLayout {
                            Layout.fillWidth: true
                            Text { Layout.fillWidth: true; text: root.stageLabel(requestCard.modelData.stage); color: requestCard.working ? root.amber : requestCard.modelData.stage === "failed" ? Theme.danger : root.good; font.pixelSize: Theme.fsXs }
                            Text { visible: !requestCard.working; text: ((requestCard.modelData.durationMs || 0) / 1000).toFixed(1) + " s"; color: root.dim; font.pixelSize: Theme.fsXs }
                        }
                    }
                }
            }
            Column {
                anchors.centerIn: parent; width: parent.width - 40; spacing: 8
                visible: root.requests.length === 0
                Text { width: parent.width; text: qsTr("No requests yet"); color: Theme.textSecondary; font.pixelSize: Theme.fsMd; horizontalAlignment: Text.AlignHCenter }
                Text { width: parent.width; text: qsTr("Live transcription and translation\nrequests appear here."); color: root.dim; font.pixelSize: Theme.fsSm; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap }
            }
        }

        Item {
            Layout.fillWidth: true; Layout.fillHeight: true
            Column {
                anchors.centerIn: parent; spacing: 14; width: parent.width
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Math.min(192, parent.width); height: 140; radius: 18
                    color: root.activeRequests.length ? "#211a10" : "#17171c"
                    border.color: root.activeRequests.length ? "#9c681e" : "#38383f"
                    Column {
                        anchors.centerIn: parent; spacing: 12
                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 44; height: 44; radius: 12; color: "#342713"
                            Image { anchors.centerIn: parent; width: 24; height: 24; source: "qrc:/assets/icons/arrow-right-left.svg" }
                        }
                        Text { anchors.horizontalCenter: parent.horizontalCenter; text: qsTr("Pool router"); color: Theme.textPrimary; font.pixelSize: 18; font.weight: Font.DemiBold }
                        Text { anchors.horizontalCenter: parent.horizontalCenter; text: qsTr("%1 accounts ready").arg(Pool.state.readyCount || 0); color: root.dim; font.pixelSize: Theme.fsXs }
                    }
                }
                Text {
                    width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                    text: !root.online ? qsTr("Router stopped")
                        : root.protection.state === "cooldown" ? qsTr("Cooling down · %1 s").arg(root.protection.retryAfterSeconds || 0)
                        : root.activeRequests.some(r => r.stage === "sending") ? qsTr("Sending to Google")
                        : root.activeRequests.length ? root.stageLabel(root.activeRequests[0].stage)
                        : !(Pool.state.readyCount > 0) ? qsTr("No verified accounts ready")
                        : qsTr("Ready for requests")
                    color: root.activeRequests.length ? root.amber : root.dim; font.pixelSize: Theme.fsSm
                }
                Text {
                    width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                    visible: root.online && root.protection.state !== undefined
                    text: qsTr("Request protection active")
                    color: root.good; font.pixelSize: Theme.fsXs
                }
            }
        }

        Rectangle {
            id: accountsPanel
            Layout.preferredWidth: root.width * 0.29
            Layout.fillHeight: true
            color: root.panelColor; radius: 12; border.color: root.borderColor
            RowLayout {
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                anchors.margins: 16; height: 24
                Text { Layout.fillWidth: true; text: qsTr("Google accounts"); color: Theme.textPrimary; font.pixelSize: 15; font.weight: Font.DemiBold }
                Text { text: root.accounts.length; color: root.dim; font.pixelSize: Theme.fsXs }
            }
            ListView {
                id: accountList
                objectName: "pool-account-list"
                anchors.fill: parent; anchors.margins: 10; anchors.topMargin: 52
                clip: true; spacing: 8
                model: root.accounts
                onContentYChanged: routes.requestPaint()
                ScrollBar.vertical: ScrollBar { }
                delegate: Button {
                    id: accountCard
                    required property var modelData
                    readonly property var currentRequest: root.accountRequest(modelData.id)
                    width: ListView.view.width; height: 68; padding: 12
                    hoverEnabled: true
                    onClicked: root.manageAccounts()
                    background: Rectangle {
                        radius: 8; color: accountCard.currentRequest ? "#211a10" : accountCard.hovered ? "#232329" : "#1a1a20"
                        border.color: accountCard.currentRequest ? "#8c5e1b" : accountCard.activeFocus ? root.amber : "transparent"
                    }
                    contentItem: ColumnLayout {
                        spacing: 6
                        RowLayout {
                            Layout.fillWidth: true
                            Rectangle { implicitWidth: 7; implicitHeight: 7; radius: 4; color: accountCard.currentRequest ? root.amber : root.statusColor(accountCard.modelData.status, accountCard.modelData.enabled) }
                            Text { Layout.fillWidth: true; text: accountCard.modelData.email; color: Theme.textPrimary; font.pixelSize: Theme.fsSm; font.weight: Font.Medium; elide: Text.ElideRight }
                            Text { text: accountCard.modelData.tier === "paid" ? "PRO" : accountCard.modelData.tier === "free" ? "FREE" : "WEB"; color: root.dim; font.pixelSize: 9 }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Text { Layout.fillWidth: true; text: accountCard.currentRequest ? root.stageLabel(accountCard.currentRequest.stage) : root.statusLabel(accountCard.modelData.status, accountCard.modelData.enabled); color: accountCard.currentRequest ? root.amber : root.statusColor(accountCard.modelData.status, accountCard.modelData.enabled); font.pixelSize: Theme.fsXs }
                            Text { text: qsTr("%1 served").arg(accountCard.modelData.served || 0); color: root.dim; font.pixelSize: Theme.fsXs }
                        }
                    }
                    ToolTip.visible: hovered && (modelData.statusDetail || "") !== ""
                    ToolTip.text: modelData.statusDetail || ""
                }
            }
            Text {
                anchors.centerIn: parent; width: parent.width - 40
                visible: root.accounts.length === 0
                text: qsTr("Add a Google account\nto start routing.")
                color: root.dim; font.pixelSize: Theme.fsSm; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
            }
        }
    }
}
