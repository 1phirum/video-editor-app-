import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../theme"
import "../effects"
import "../export"
import "../lumetri"
import "../project"
import "../subtitles"
import "../timeline"

// Consistent search input: a dark well with a leading search glyph.
Rectangle {
    id: root

    property string placeholder: "Search"
    property alias text: input.text
    property int glyphSize: 13

    implicitHeight: 28
    implicitWidth: 200
    color: Theme.bgPrimary
    radius: Theme.radiusSm

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        spacing: 6

        Image {
            source: "../../assets/icons/search.svg"
            sourceSize.width: root.glyphSize
            sourceSize.height: root.glyphSize
            Layout.preferredWidth: root.glyphSize
            Layout.preferredHeight: root.glyphSize
            opacity: 0.55
        }

        TextField {
            id: input
            Layout.fillWidth: true
            Layout.fillHeight: true
            background: null
            leftPadding: 0
            rightPadding: 0
            topPadding: 0
            bottomPadding: 0
            verticalAlignment: TextInput.AlignVCenter
            color: Theme.textPrimary
            font.pixelSize: Theme.fsMd
            placeholderText: root.placeholder
            placeholderTextColor: Theme.textMuted
            selectionColor: Theme.accent
        }
    }
}
