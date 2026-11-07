// qmllint disable
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CutPro 1.0
import "../../theme"
import "../common"
import "../lumetri"

// The Text Effects tab. Two sub-tabs:
//   Add Text - drop a free text overlay on the picture and edit its words.
//   Effects  - collapsible galleries of letter styles and caption animations,
//              and the speed they all run at.
//
// Every effect card is the same AnimatedCaption the monitor draws with, fed the
// same solved numbers from C++, so what a card shows is what the burned-in
// export writes - the cards are not mock-ups, they are the effect.
Rectangle {
    id: root
    color: Theme.bgPanel

    // 0 = Add Text, 1 = Effects. Opens on Effects, where the panel always landed.
    property int subTab: 1

    // False for the first frame(s) after the Effects view appears, while ~18
    // captions do their opening layout pass and the custom caption font's
    // metrics resolve. The gallery is held invisible until this flips, so that
    // one-time reflow settles off-screen instead of showing as a jitter, and no
    // card counts as "live" until then (see GalleryCard.live).
    property bool _settled: false

    readonly property string sample: qsTr("Sample text")

    // One clock for the whole tab. The cards read it, so they run in step and
    // the panel costs a single timer instead of one per card.
    property real previewMs: 0
    readonly property real loopMs: 2600

    // A hand-placed text overlay is a subtitle clip with no transcript line
    // behind it (transcriptIndex < 0). When one is selected the Add Text tab
    // edits its words and shows its timing.
    readonly property var selectedTextClip: {
        var c = Backend.selectedClip
        return (c && c.kind === "subtitle"
                && c.transcriptIndex !== undefined && c.transcriptIndex < 0) ? c : null
    }
    property string _overlayEditingId: ""
    onSelectedTextClipChanged: {
        var newId = selectedTextClip ? String(selectedTextClip.id) : ""
        if (newId !== _overlayEditingId) {
            _overlayEditingId = newId
            if (selectedTextClip)
                overlayField.text = selectedTextClip.text || ""
        }
    }

    function formatTimecode(ms) {
        var t = ms < 0 ? 0 : ms
        var cs = Math.round(t / 10)
        var hundredths = cs % 100
        var totalSeconds = Math.floor(cs / 100)
        var seconds = totalSeconds % 60
        var minutes = Math.floor(totalSeconds / 60)
        function pad(n) { return (n < 10 ? "0" : "") + n }
        return pad(minutes) + ":" + pad(seconds) + "." + pad(hundredths)
    }

    // Start every card from the same clean frame each time the Effects view is
    // opened, so the animations don't jump in from wherever the clock was last
    // frozen - and re-arm the settle gate so the opening reflow stays hidden.
    function _openEffects() {
        previewMs = 0
        _settled = false
        settleTimer.restart()
    }
    onVisibleChanged: if (visible && subTab === 1) _openEffects()
    onSubTabChanged: if (visible && subTab === 1) _openEffects()
    // Cover the case where the panel is already the visible view at startup, so
    // neither change handler above fires and the gate would never open.
    Component.onCompleted: if (visible && subTab === 1) _openEffects()

    // One shot: let the gallery lay out for a couple of frames, then reveal it.
    Timer {
        id: settleTimer
        interval: 60
        repeat: false
        onTriggered: root._settled = true
    }

    Timer {
        interval: 40
        running: root.visible && root.subTab === 1
        repeat: true
        onTriggered: root.previewMs = (root.previewMs + interval) % root.loopMs
    }

    // One gallery tile: the sample drawn for real, its name, and a selected ring.
    component GalleryCard: Rectangle {
        id: card
        property string cardId: ""
        property string label: ""
        property bool selected: false
        property string previewStyle: "none"
        property string previewAnimation: "none"
        signal picked

        readonly property bool live: {
            if (card.previewAnimation === "none" || !root.visible
                    || root.subTab !== 1 || !root._settled)
                return false
            const watch = scroller.contentY + scroller.height + card.y
                        + (card.parent ? card.parent.y : 0)
            const top = card.mapToItem(scroller, 0, 0).y
            return watch >= 0 && top > -card.height && top < scroller.height
        }

        width: 106
        height: 82
        radius: Theme.radiusMd
        clip: true
        color: selected ? Theme.bgCardHeader : Theme.bgCard
        border.width: selected ? 2 : 1
        border.color: selected ? Theme.accent
                     : hover.hovered ? Theme.textMuted : Theme.outline

        HoverHandler { id: hover }

        // Fixed, clipped frame that reserves the label strip at the bottom. The
        // caption fills it and centres its drawing inside, so the caption item
        // itself never moves while its content size settles on first paint -
        // that was the "shake". Any residual settle happens inside this
        // stationary clipped box instead of nudging a content-sized item that
        // is in turn centred in the card.
        Item {
            id: captionBox
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 16
            clip: true

            AnimatedCaption {
                anchors.centerIn: parent
                width: captionBox.width
                height: captionBox.height
                text: root.sample
                styleId: card.previewStyle
                animationId: card.previewAnimation
                emSize: 16
                maxWidth: 98
                maximumLineCount: 2
                lowDetail: true
                elapsedMs: card.live ? root.previewMs : root.loopMs
                cueDurationMs: root.loopMs
            }
        }

        Text {
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 5
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width - 10
            horizontalAlignment: Text.AlignHCenter
            text: card.label
            elide: Text.ElideRight
            color: card.selected ? Theme.textPrimary : Theme.textMuted
            font.pixelSize: Theme.fsXs
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: card.picked()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- Tab bar --------------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            color: Theme.bgPanel

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                spacing: 18

                Repeater {
                    model: [qsTr("Add Text"), qsTr("Effects")]
                    delegate: Item {
                        id: tab
                        required property int index
                        required property string modelData
                        readonly property bool active: root.subTab === index
                        Layout.fillHeight: true
                        implicitWidth: tabLabel.implicitWidth

                        Text {
                            id: tabLabel
                            anchors.centerIn: parent
                            text: tab.modelData
                            font.pixelSize: Theme.fsMd
                            font.weight: tab.active ? Font.DemiBold : Font.Normal
                            color: tab.active ? Theme.textPrimary : Theme.textMuted
                        }
                        Rectangle {
                            anchors.bottom: parent.bottom
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: tabLabel.implicitWidth
                            height: 2
                            color: Theme.accent
                            visible: tab.active
                        }
                        TapHandler {
                            cursorShape: Qt.PointingHandCursor
                            onTapped: root.subTab = tab.index
                        }
                    }
                }
                Item { Layout.fillWidth: true }
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.border
            }
        }

        // ---- Pages ----------------------------------------------------------
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.subTab

            // ===== Add Text ==================================================
            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 12

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Text {
                            text: qsTr("Text overlay")
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fsMd
                            font.weight: Font.DemiBold
                        }
                        InfoTip {
                            Layout.alignment: Qt.AlignVCenter
                            text: qsTr("Drop a line of text onto the picture at the playhead. ")
                                  + qsTr("It draws in the monitor and burns into the export, ")
                                  + qsTr("and takes the letter style and animation from the ")
                                  + qsTr("Effects tab.")
                        }
                        Item { Layout.fillWidth: true }
                    }

                    Button {
                        Layout.fillWidth: true
                        implicitHeight: 36
                        text: qsTr("+  Add text at playhead")
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
                            radius: Theme.radiusSm
                        }
                        onClicked: {
                            var clipId = Backend.addTextClip()
                            if (clipId)
                                Backend.selectedClipId = clipId
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: Theme.divider
                    }

                    RowLayout {
                        visible: root.selectedTextClip !== null
                        Layout.fillWidth: true
                        spacing: 6
                        Text {
                            text: qsTr("Selected title")
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fsSm
                            font.weight: Font.DemiBold
                        }
                        InfoTip {
                            Layout.alignment: Qt.AlignVCenter
                            text: qsTr("Drag the clip on the timeline to move it or change ")
                                  + qsTr("how long it stays on screen.")
                        }
                        Item { Layout.fillWidth: true }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 92
                        visible: root.selectedTextClip !== null
                        color: Theme.bgPrimary
                        radius: Theme.radiusSm
                        border.color: overlayField.activeFocus ? Theme.accent : Theme.border

                        ScrollView {
                            id: overlayScroll
                            anchors.fill: parent
                            anchors.margins: 6
                            clip: true
                            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                            TextArea {
                                id: overlayField
                                width: overlayScroll.availableWidth
                                wrapMode: TextArea.Wrap
                                color: Theme.textPrimary
                                font.pixelSize: Theme.fsMd
                                selectByMouse: true
                                placeholderText: qsTr("Type your text...")
                                placeholderTextColor: Theme.textMuted
                                background: null
                                onTextChanged: if (root.selectedTextClip)
                                                   Backend.updateTextClip(root.selectedTextClip.id, text)
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: root.selectedTextClip === null
                        text: qsTr("No title selected. Add one above, or click an ")
                              + qsTr("existing text overlay on the timeline to edit it here.")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fsXs
                        wrapMode: Text.WordWrap
                    }

                    Item { Layout.fillHeight: true }
                }
            }

            // ===== Effects (collapsible sections) ============================
            Item {
                Flickable {
                    id: scroller
                    anchors.fill: parent
                    clip: true
                    contentWidth: width
                    contentHeight: sections.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    ColumnLayout {
                        id: sections
                        width: parent.width
                        spacing: 0
                        // Hidden until the opening layout pass settles, then a
                        // plain fade - opacity only, no movement, so it reads as
                        // a reveal rather than the jitter it replaces.
                        opacity: root._settled ? 1 : 0
                        Behavior on opacity { NumberAnimation { duration: 140 } }

                        // ----- Letter style -----------------------------------
                        LumetriSection {
                            title: qsTr("Letter style")
                            expanded: true

                            Flow {
                                Layout.fillWidth: true
                                spacing: 8

                                Repeater {
                                    model: Backend.textEffectStyles
                                    delegate: GalleryCard {
                                        required property var modelData
                                        cardId: modelData.id
                                        label: modelData.label
                                        previewStyle: modelData.id
                                        previewAnimation: "none"
                                        selected: Backend.captionTextEffect === modelData.id
                                        onPicked: Backend.captionTextEffect = modelData.id
                                    }
                                }
                            }
                        }

                        // ----- Animation --------------------------------------
                        LumetriSection {
                            title: qsTr("Animation")
                            expanded: true
                            help: qsTr("Cards play at the speed below, in the letter ")
                                  + qsTr("style you picked above.")
                                  + "\n\n"
                                  + qsTr("The monitor is driven by the playhead, so ")
                                  + qsTr("scrubbing lands on the frame the export ")
                                  + qsTr("writes. Burned-in captions animate when the ")
                                  + qsTr("transcript is short enough to fit the ")
                                  + qsTr("export's image budget; longer ones burn in ")
                                  + qsTr("settled.")

                            Flow {
                                Layout.fillWidth: true
                                spacing: 8

                                Repeater {
                                    model: Backend.textEffectAnimations
                                    delegate: GalleryCard {
                                        required property var modelData
                                        cardId: modelData.id
                                        label: modelData.label
                                        previewStyle: Backend.captionTextEffect
                                        previewAnimation: modelData.id
                                        selected: Backend.captionAnimation === modelData.id
                                        onPicked: Backend.captionAnimation = modelData.id
                                    }
                                }
                            }

                            LumetriSlider {
                                Layout.fillWidth: true
                                Layout.topMargin: 4
                                label: qsTr("Speed")
                                from: 0.25
                                to: 3.0
                                decimals: 1
                                suffix: "x"
                                value: Backend.captionAnimationSpeed
                                onValueCommitted: speed => Backend.captionAnimationSpeed = speed
                            }
                        }

                        Item { Layout.fillWidth: true; Layout.preferredHeight: 12 }
                    }
                }
            }
        }
    }
}
