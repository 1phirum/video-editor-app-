import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia
import CutPro 1.0
import "../../theme"
import "../common"
import "../subtitles"

NativeModalWindow {
    id: dialog
    objectName: "voiceOverDialog"
    property string armedTrack: ""
    property real startMs: 0
    property bool webcamMode: false
    property bool mirrorPreview: true
    property var deviceList: []
    property var voiceList: []
    property string voiceId: "none"
    property real elapsedMs: 0
    readonly property var webcam: Backend.voiceOverWebcam
    readonly property bool recording: Backend.voiceOverRecording
    readonly property int countdown: Backend.voiceOverCountdown
    readonly property bool busy: Backend.voiceOverBusy
    readonly property bool handsFree: Backend.appSettings.voiceOverHandsFree !== false
    readonly property string sessionState: countdown > 0 ? qsTr("Starting in %1…").arg(countdown)
        : busy && !recording ? qsTr("Preparing recording…")
        : recording ? (handsFree && !meter.live ? qsTr("Listening") : qsTr("Recording"))
        : qsTr("Ready to record")

    dialogTitle: qsTr("Voice-over & Webcam")
    dialogWidth: 880
    dialogHeight: 640
    color: Theme.bgPrimary
    minimumWidth: 760
    minimumHeight: 580

    component Heading: Label {
        color: Theme.textPrimary
        font.pixelSize: 17
        font.bold: true
    }
    component Caption: Label {
        color: Theme.textSecondary
        font.pixelSize: Theme.fsLg
    }
    component Card: Rectangle {
        color: Theme.bgPanel
        radius: 12
        border.color: Theme.bgCardHeader
        border.width: 1
    }

    function writeSetting(key, value) {
        var next = Object.assign({}, Backend.appSettings)
        next[key] = value
        Backend.applyAppSettings(next)
    }
    function indexOfVoice(id) {
        for (var i = 0; i < voiceList.length; ++i)
            if (String(voiceList[i].id) === id) return i
        return 0
    }
    function refreshDevices() {
        deviceList = Backend.voiceOverInputDevices()
        var saved = String(Backend.appSettings.voiceOverDeviceId || "")
        var selected = 0
        for (var i = 0; i < deviceList.length; ++i)
            if (String(deviceList[i].id) === saved || (saved === "" && deviceList[i].isDefault))
                selected = i
        deviceBox.currentIndex = deviceList.length ? selected : -1
    }
    function openFor(track, atMs) {
        armedTrack = track || ""
        startMs = Number(atMs || 0)
        meter.reset()
        elapsedMs = 0
        voiceList = Backend.voicePresets()
        voiceId = String(Backend.appSettings.voiceOverVoiceId || "none")
        voiceBox.currentIndex = indexOfVoice(voiceId)
        refreshDevices()
        var available = { x: Screen.virtualX, y: Screen.virtualY,
            width: Math.min(Screen.width, Screen.desktopAvailableWidth),
            height: Math.min(Screen.height, Screen.desktopAvailableHeight) }
        width = Math.min(dialogWidth, available.width)
        height = Math.min(dialogHeight, available.height)
        x = Math.max(available.x, Math.min(available.x + available.width - width,
            ownerWindow ? ownerWindow.x + (ownerWindow.width - width) / 2 : available.x + (available.width - width) / 2))
        y = Math.max(available.y, Math.min(available.y + available.height - height,
            ownerWindow ? ownerWindow.y + (ownerWindow.height - height) / 2 : available.y + (available.height - height) / 2))
        show()
        raise()
        requestActivate()
        Backend.startVoiceOverMonitor()
        webcam.setEnabled(webcamMode)
    }
    onWebcamModeChanged: if (visible) webcam.setEnabled(webcamMode)
    onClosing: {
        if (busy) Backend.cancelVoiceOver()
        Backend.stopVoiceOverMonitor()
        if (!webcam.busy) webcam.setEnabled(false)
        Backend.stopVoicePreview()
    }
    Timer {
        running: dialog.visible
        interval: 40; repeat: true
        onTriggered: {
            var levels = Backend.voiceOverMeterLevels()
            meter.push(levels[0], levels[1])
            if (dialog.recording) dialog.elapsedMs = Backend.voiceOverElapsedMs()
        }
    }
    Connections {
        target: Backend
        function onVoiceOverFinished(ok, clipId, error) {
            if (dialog.visible) Backend.startVoiceOverMonitor()
            else dialog.webcam.setEnabled(false)
        }
    }

    ScrollView {
        id: scroll
        objectName: "recorderSetupScroll"
        anchors.top: parent.top
        anchors.bottom: footer.top
        anchors.left: parent.left
        anchors.right: parent.right
        clip: true
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical.policy: ScrollBar.AlwaysOff
        ColumnLayout {
            width: scroll.availableWidth
            spacing: 18
            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: 24
                Layout.bottomMargin: 0
                spacing: 16
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        Heading { text: qsTr("Voice-over & Webcam"); font.pixelSize: 25 }
                        Caption { text: qsTr("Record to your timeline, with or without a camera.") }
                    }
                    Item { Layout.fillWidth: true }
                    Rectangle {
                        width: 7; height: 7; radius: 4
                        color: dialog.recording && meter.live ? Theme.danger : Theme.meterGreen
                    }
                    Caption {
                        text: dialog.sessionState
                        color: dialog.recording && meter.live ? Theme.danger : Theme.meterGreen
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Rectangle {
                        Layout.preferredWidth: 340; Layout.preferredHeight: 40
                        radius: 8; color: Theme.divider
                        Row {
                            anchors.fill: parent; anchors.margins: 3
                            Repeater {
                                model: [qsTr("Voice only"), qsTr("Voice + webcam")]
                                Button {
                                    required property int index
                                    required property string modelData
                                    objectName: index === 0 ? "voiceOnlyMode" : "webcamMode"
                                    width: 167; height: 34
                                    text: modelData; enabled: !dialog.busy
                                    onClicked: dialog.webcamMode = index === 1
                                    contentItem: Text {
                                        text: parent.text; color: Theme.textPrimary
                                        font.pixelSize: Theme.fsLg
                                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                                    }
                                    background: Rectangle {
                                        radius: 6
                                        color: dialog.webcamMode === (parent.index === 1) ? Theme.bgCardHeader : "transparent"
                                        opacity: parent.enabled ? 1 : 0.45
                                    }
                                }
                            }
                        }
                    }
                    Item { Layout.fillWidth: true }
                    Caption { text: qsTr("Playhead  %1 s").arg((dialog.startMs / 1000).toFixed(1)); color: Theme.textMuted }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 24
                    Card {
                        id: previewCard
                        Layout.fillWidth: true
                        Layout.preferredWidth: 570
                        Layout.preferredHeight: Math.max(300, microphoneControls.implicitHeight + 44)
                        clip: true
                        color: Theme.viewer
                        VideoOutput {
                            id: cameraPreview
                            objectName: "recorderCameraPreview"
                            anchors.fill: parent
                            anchors.bottomMargin: 54
                            fillMode: VideoOutput.PreserveAspectFit
                            mirrored: dialog.mirrorPreview
                            visible: dialog.webcamMode && dialog.webcam.active
                            Component.onCompleted: dialog.webcam.setPreviewSink(videoSink)
                        }
                        ColumnLayout {
                            anchors.centerIn: parent
                            width: parent.width - 64
                            visible: !cameraPreview.visible
                            spacing: 18
                            Heading {
                                Layout.alignment: Qt.AlignHCenter
                                text: dialog.webcamMode ? qsTr("Camera preview") : qsTr("Your voice, on the timeline")
                                font.pixelSize: 20
                            }
                            Row {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.preferredHeight: 80
                                Layout.preferredWidth: 247
                                spacing: 6
                                Repeater {
                                    model: 23
                                    Rectangle {
                                        required property int index
                                        width: 5
                                        height: 10 + (1 - Math.abs(index - 11) / 12) * 65 * meter.fraction
                                        y: (80 - height) / 2
                                        radius: 3
                                        color: meter.live ? Theme.accent : Theme.textMuted
                                        opacity: meter.live ? 1 : 0.35
                                    }
                                }
                            }
                            Caption {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                horizontalAlignment: Text.AlignHCenter
                                text: dialog.webcamMode
                                      ? (dialog.webcam.error || qsTr("Starting your camera…"))
                                      : qsTr("Speak to check your level. Record when you’re ready.")
                            }
                        }
                        Rectangle {
                            x: 16; y: 16; width: 90; height: 28; radius: 6; color: Theme.divider
                            Caption { anchors.centerIn: parent; text: dialog.recording ? qsTr("● REC") : qsTr("● PREVIEW"); font.pixelSize: Theme.fsXs; color: dialog.recording ? Theme.danger : Theme.meterGreen }
                        }
                        Rectangle {
                            visible: dialog.webcamMode && dialog.webcam.formatLabel.length > 0
                            anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 16
                            width: formatBadge.implicitWidth + 20; height: 28; radius: 6; color: Theme.divider
                            Caption { id: formatBadge; anchors.centerIn: parent; text: dialog.webcam.formatLabel; font.pixelSize: Theme.fsXs }
                        }
                        Rectangle {
                            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                            anchors.margins: 12
                            height: 38; radius: 8; color: Theme.divider
                            visible: dialog.webcamMode
                            RowLayout {
                                anchors.fill: parent; anchors.margins: 5; spacing: 8
                                DarkComboBox {
                                    objectName: "recorderCameraDevice"
                                    Layout.fillWidth: true
                                    enabled: !dialog.busy
                                    model: dialog.webcam.devices.map(function(d) { return d.description })
                                    currentIndex: {
                                        var devices = dialog.webcam.devices
                                        for (var i = 0; i < devices.length; ++i)
                                            if (devices[i].id === dialog.webcam.deviceId) return i
                                        return -1
                                    }
                                    onActivated: index => dialog.webcam.selectDevice(String(dialog.webcam.devices[index].id))
                                }
                                CaptionSwitch {
                                    text: qsTr("Mirror")
                                    checked: dialog.mirrorPreview
                                    onToggled: dialog.mirrorPreview = checked
                                }
                                DarkDialogButton {
                                    implicitWidth: 68; text: qsTr("Settings"); enabled: !dialog.busy; flat: true
                                    onClicked: cameraSettings.open()
                                }
                            }
                        }
                    }
                    Card {
                        objectName: "recorderMicrophoneCard"
                        Layout.preferredWidth: 310
                        Layout.preferredHeight: previewCard.height
                        ColumnLayout {
                            id: microphoneControls
                            anchors.fill: parent; anchors.margins: 22; spacing: 12
                            Caption { text: qsTr("MICROPHONE"); font.bold: true; font.pixelSize: Theme.fsSm }
                            DarkComboBox {
                                id: deviceBox
                                objectName: "recorderMicrophone"
                                Layout.fillWidth: true; Layout.preferredHeight: 36
                                enabled: !dialog.busy
                                model: dialog.deviceList.map(function(d) { return d.description + (d.isDefault ? qsTr(" (default)") : "") })
                                onActivated: index => {
                                    dialog.writeSetting("voiceOverDeviceId", String(dialog.deviceList[index].id))
                                    meter.reset()
                                    Backend.stopVoiceOverMonitor()
                                    Backend.startVoiceOverMonitor()
                                }
                            }
                            RecorderLevelMeter { id: meter; Layout.fillWidth: true }
                            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.bgCardHeader }
                            Caption { text: qsTr("VOICE"); font.bold: true; font.pixelSize: Theme.fsSm }
                            RowLayout {
                                Layout.fillWidth: true
                                DarkComboBox {
                                    id: voiceBox
                                    objectName: "recorderVoicePreset"
                                    Layout.fillWidth: true
                                    enabled: !dialog.busy
                                    model: dialog.voiceList.map(function(v) { return v.name })
                                    onActivated: index => {
                                        dialog.voiceId = String(dialog.voiceList[index].id)
                                        dialog.writeSetting("voiceOverVoiceId", dialog.voiceId)
                                    }
                                }
                                DarkDialogButton {
                                    implicitWidth: 34; text: Backend.voiceFxPreviewId.length ? "■" : "▶"
                                    enabled: !dialog.busy && Backend.voiceOverLastClipId.length > 0 && !Backend.voiceFxBusy
                                    onClicked: Backend.voiceFxPreviewId.length ? Backend.stopVoicePreview()
                                        : Backend.previewVoice(Backend.voiceOverLastClipId, dialog.voiceId)
                                }
                            }
                            Item { Layout.fillHeight: true }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true; spacing: 18
                    enabled: !dialog.busy
                    opacity: enabled ? 1 : 0.45
                    Card {
                        Layout.fillWidth: true; Layout.preferredHeight: options.implicitHeight + 36
                        ColumnLayout {
                            id: options
                            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                            anchors.margins: 18; spacing: 10
                            RowLayout {
                                Layout.fillWidth: true
                                Heading { text: qsTr("Hands-free clips"); Layout.fillWidth: true }
                                CaptionSwitch { checked: dialog.handsFree; onToggled: dialog.writeSetting("voiceOverHandsFree", checked) }
                            }
                            Caption { text: qsTr("Save each spoken line as its own clip."); font.pixelSize: Theme.fsSm }
                            GridLayout {
                                Layout.fillWidth: true; columns: 3; columnSpacing: 8
                                enabled: dialog.handsFree
                                Caption { text: qsTr("Silence below"); font.pixelSize: Theme.fsSm }
                                DarkSlider {
                                    id: silenceSlider; Layout.fillWidth: true
                                    from: -70; to: -20; stepSize: 1
                                    value: Number(Backend.appSettings.voiceOverSilenceDb ?? -42)
                                    onMoved: dialog.writeSetting("voiceOverSilenceDb", value)
                                }
                                Caption { text: qsTr("%1 dB").arg(silenceSlider.value.toFixed(0)); font.pixelSize: Theme.fsSm }
                                Caption { text: qsTr("Pause ends a line"); font.pixelSize: Theme.fsSm }
                                DarkSlider {
                                    id: pauseSlider; Layout.fillWidth: true
                                    from: 300; to: 4000; stepSize: 100
                                    value: Number(Backend.appSettings.voiceOverHangoverMs ?? 1200)
                                    onMoved: dialog.writeSetting("voiceOverHangoverMs", value)
                                }
                                Caption { text: qsTr("%1 s").arg((pauseSlider.value / 1000).toFixed(1)); font.pixelSize: Theme.fsSm }
                            }
                        }
                    }
                    Card {
                        Layout.fillWidth: true; Layout.preferredHeight: options.implicitHeight + 36
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 18; spacing: 10
                            Heading { text: qsTr("Playback & recording") }
                            RowLayout {
                                Layout.fillWidth: true
                                Caption { text: qsTr("Count-in"); font.pixelSize: Theme.fsSm }
                                DarkComboBox {
                                    Layout.fillWidth: true
                                    model: [qsTr("Off"), qsTr("1 second"), qsTr("2 seconds"), qsTr("3 seconds"), qsTr("4 seconds"), qsTr("5 seconds"), qsTr("6 seconds"), qsTr("7 seconds"), qsTr("8 seconds")]
                                    currentIndex: Number(Backend.appSettings.voiceOverCountdownSec ?? 3)
                                    onActivated: index => dialog.writeSetting("voiceOverCountdownSec", index)
                                }
                                Caption { text: qsTr("Channels"); font.pixelSize: Theme.fsSm }
                                DarkComboBox {
                                    Layout.preferredWidth: 85; model: [qsTr("Mono"), qsTr("Stereo")]
                                    currentIndex: Number(Backend.appSettings.voiceOverChannels || 1) - 1
                                    onActivated: index => {
                                        dialog.writeSetting("voiceOverChannels", index + 1)
                                        Backend.stopVoiceOverMonitor()
                                        Backend.startVoiceOverMonitor()
                                    }
                                }
                            }
                            DarkCheckBox {
                                text: qsTr("Play sequence")
                                checked: Backend.appSettings.voiceOverPlayWhileRecording !== false
                                onToggled: dialog.writeSetting("voiceOverPlayWhileRecording", checked)
                            }
                            DarkCheckBox {
                                objectName: "recorderMuteProgram"
                                text: qsTr("Mute program audio")
                                checked: Backend.appSettings.voiceOverMuteMonitor === true
                                onToggled: dialog.writeSetting("voiceOverMuteMonitor", checked)
                            }
                            Item { Layout.fillHeight: true }
                        }
                    }
                }
                Caption {
                    Layout.fillWidth: true
                    visible: dialog.deviceList.length === 0 || (dialog.webcamMode && dialog.webcam.error.length > 0)
                    text: dialog.deviceList.length === 0 ? qsTr("No microphone found. Connect one, then refresh devices.") : dialog.webcam.error
                    color: Theme.danger; wrapMode: Text.WordWrap
                }
                DarkDialogButton {
                    visible: !dialog.busy && (dialog.deviceList.length === 0 ||
                        (dialog.webcamMode && (dialog.webcam.devices.length === 0 || dialog.webcam.error.length > 0)))
                    text: qsTr("Refresh devices"); flat: true
                    onClicked: {
                        dialog.refreshDevices()
                        Backend.stopVoiceOverMonitor()
                        Backend.startVoiceOverMonitor()
                        if (dialog.webcamMode) dialog.webcam.setEnabled(true)
                    }
                }
            }
            Item { Layout.preferredHeight: 6 }
        }
    }
    Rectangle {
        id: footer
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        height: 96; color: Theme.divider
        RowLayout {
            anchors.fill: parent; anchors.margins: 18; spacing: 22
            ColumnLayout {
                spacing: 4
                Caption { text: qsTr("Session"); font.pixelSize: Theme.fsXs; color: Theme.textMuted }
                Label {
                    text: {
                        var seconds = Math.floor(dialog.elapsedMs / 1000)
                        return Math.floor(seconds / 60).toString().padStart(2, "0") + ":"
                            + (seconds % 60).toString().padStart(2, "0") + "." + Math.floor(dialog.elapsedMs % 1000 / 100)
                    }
                    color: Theme.textPrimary; font.pixelSize: 25
                }
            }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 5
                Caption {
                    Layout.fillWidth: true
                    text: Backend.voiceOverStatus || qsTr("Ready for your first take")
                    elide: Text.ElideRight
                }
                Caption {
                    text: Backend.voiceOverTakeCount > 0 ? qsTr("%1 clips captured").arg(Backend.voiceOverTakeCount)
                         : dialog.webcamMode ? qsTr("Video + voice → available timeline tracks")
                         : qsTr("Voice → %1").arg(dialog.armedTrack || qsTr("available audio track"))
                    color: Theme.textMuted; font.pixelSize: Theme.fsSm
                }
            }
            DarkDialogButton {
                text: dialog.busy ? qsTr("Discard") : qsTr("Close")
                implicitWidth: 102; implicitHeight: 42
                onClicked: dialog.busy ? Backend.cancelVoiceOver() : dialog.close()
            }
            DarkDialogButton {
                objectName: "recorderRecordButton"
                text: dialog.busy ? qsTr("Stop") : qsTr("Record")
                highlighted: true; implicitWidth: 142; implicitHeight: 42
                enabled: dialog.busy || (dialog.deviceList.length > 0 && (!dialog.webcamMode || dialog.webcam.active))
                onClicked: {
                    if (dialog.busy) Backend.stopVoiceOver()
                    else {
                        Backend.stopVoicePreview()
                        meter.resetPeak()
                        dialog.elapsedMs = 0
                        Backend.startVoiceOver(dialog.armedTrack, dialog.startMs, dialog.webcamMode)
                    }
                }
            }
        }
    }
    Popup {
        id: cameraSettings
        anchors.centerIn: Overlay.overlay
        width: 360; padding: 20; modal: true
        background: Card {}
        ColumnLayout {
            width: parent.width; spacing: 14
            Heading { text: qsTr("Camera settings") }
            Caption { text: qsTr("Resolution & frame rate") }
            DarkComboBox {
                Layout.fillWidth: true
                model: dialog.webcam.formats.map(function(f) { return f.label })
                currentIndex: dialog.webcam.formatIndex
                onActivated: index => dialog.webcam.selectFormat(index)
            }
            Caption {
                Layout.fillWidth: true; wrapMode: Text.WordWrap
                text: qsTr("Mirror changes the preview only. Your saved recording keeps its original orientation.")
                font.pixelSize: Theme.fsSm
            }
            DarkDialogButton { text: qsTr("Done"); Layout.alignment: Qt.AlignRight; onClicked: cameraSettings.close() }
        }
    }
}
