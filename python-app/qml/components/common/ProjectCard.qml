// qmllint disable
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../theme"

// A project tile on the home screen: a thumbnail area (a placeholder gradient
// with a film glyph until real project thumbnails exist), the project name, and
// a "last edited" line. The whole card opens the project; a hover-revealed
// overflow button offers rename/delete.
Rectangle {
    id: card

    property string projectName: ""
    property string modifiedLabel: ""
    // A file URL for the project's first still-able media, or "" when the
    // project is empty or audio-only. mediaKind ("video"/"image"/"audio"/"")
    // picks which glyph the empty state shows.
    property string thumbnailUrl: ""
    property string mediaKind: ""

    signal open()
    signal rename()
    signal remove()

    radius: 10
    color: hover.hovered ? Theme.bgCardHeader : Theme.bgCard
    border.color: Theme.outline
    border.width: 1

    // Hover is a plain background lift — no scale, no accent border.

    Column {
        anchors.fill: parent
        spacing: 0

        // Thumbnail
        Rectangle {
            id: thumb
            width: parent.width
            height: parent.height - 56
            topLeftRadius: 10
            topRightRadius: 10
            clip: true
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.lighter(Theme.bgPrimary, 1.25) }
                GradientStop { position: 1.0; color: Qt.darker(Theme.bgPrimary, 1.15) }
            }

            // Real media poster when the project has one.
            Image {
                anchors.fill: parent
                source: card.thumbnailUrl
                visible: card.thumbnailUrl.length > 0 && status === Image.Ready
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                cache: true
            }

            // Empty / non-visual state: glyph matches the media the project holds.
            Image {
                anchors.centerIn: parent
                visible: card.thumbnailUrl.length === 0
                source: card.mediaKind === "audio"
                        ? "../../assets/icons/music.svg"
                        : "../../assets/icons/film.svg"
                sourceSize.width: 34
                sourceSize.height: 34
                opacity: 0.35
            }
        }

        // Meta row
        Item {
            width: parent.width
            height: 56
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 8
                spacing: 8

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        Layout.fillWidth: true
                        text: card.projectName
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fsLg
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Text {
                        text: qsTr("Edited %1").arg(card.modifiedLabel)
                        color: Theme.textMuted
                        font.pixelSize: Theme.fsSm
                    }
                }

                // Overflow button (rename / delete)
                Rectangle {
                    Layout.preferredWidth: 26
                    Layout.preferredHeight: 26
                    radius: Theme.radiusSm
                    color: moreArea.hovered || menu.opened ? Theme.hover : "transparent"
                    opacity: hover.hovered || menu.opened ? 1.0 : 0.0

                    Image {
                        anchors.centerIn: parent
                        source: "../../assets/icons/more-horizontal.svg"
                        sourceSize.width: 16
                        sourceSize.height: 16
                        opacity: 0.85
                    }
                    HoverHandler { id: moreArea; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: menu.opened ? menu.close() : menu.open() }

                    // Tiny inline menu. A Popup (not a plain Rectangle) so it renders
                    // in the window overlay and closes on outside-press / Escape.
                    Popup {
                        id: menu
                        z: 10
                        width: 130
                        height: menuCol.implicitHeight + 8
                        // Under the dot button, right-aligned (matches the old anchors).
                        x: parent.width - width
                        y: parent.height + 4
                        padding: 4
                        // Modal (but undimmed) so the menu captures every click.
                        // It is positioned below the overflow button, which in the
                        // grid lands it on top of the card in the next row; without
                        // this, a tap on "Delete"/"Rename" fell through to that card
                        // and opened it (TapHandler's passive grab reaches both), so
                        // deleting looked like it "opened another project".
                        modal: true
                        dim: false
                        closePolicy: Popup.CloseOnPressOutside | Popup.CloseOnEscape

                        background: Rectangle {
                            radius: Theme.radiusSm
                            color: Theme.bgPanel
                            border.color: Theme.outline
                        }

                        contentItem: Column {
                            id: menuCol
                            spacing: 0

                            Repeater {
                                model: [
                                    { key: "rename", label: qsTr("Rename"), icon: "edit-2", danger: false },
                                    { key: "delete", label: qsTr("Delete"), icon: "trash-2", danger: true }
                                ]
                                delegate: Rectangle {
                                    required property var modelData
                                    width: menuCol.width
                                    height: 28
                                    radius: Theme.radiusSm
                                    color: itemHover.hovered ? Theme.hover : "transparent"
                                    Row {
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.left: parent.left
                                        anchors.leftMargin: 8
                                        spacing: 8
                                        Image {
                                            anchors.verticalCenter: parent.verticalCenter
                                            source: "../../assets/icons/" + modelData.icon + ".svg"
                                            sourceSize.width: 14
                                            sourceSize.height: 14
                                            opacity: 0.85
                                        }
                                        Text {
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: modelData.label
                                            color: modelData.danger ? Theme.danger : Theme.textPrimary
                                            font.pixelSize: Theme.fsMd
                                        }
                                    }
                                    HoverHandler { id: itemHover; cursorShape: Qt.PointingHandCursor }
                                    TapHandler {
                                        onTapped: {
                                            menu.close()
                                            if (modelData.key === "rename")
                                                card.rename()
                                            else
                                                card.remove()
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    HoverHandler { id: hover }

    // Whole-card open. A MouseArea, not a TapHandler, on purpose: the overflow
    // menu is a modal Popup, and a modal overlay blocks mouse events from
    // reaching a MouseArea behind it but NOT a TapHandler (pointer handlers keep
    // their passive grab through the overlay). With a TapHandler here, tapping
    // "Delete" in a card's menu - which sits on top of the card in the next row -
    // also fired that card's open handler, so deleting looked like it opened
    // another project. The MouseArea is correctly suppressed while the menu is up.
    MouseArea {
        anchors.fill: parent
        // Keep it below the meta row's overflow button and its menu.
        z: -1
        onClicked: {
            if (!menu.opened)
                card.open()
        }
    }
}
