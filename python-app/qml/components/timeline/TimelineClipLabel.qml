pragma ComponentBehavior: Bound
//qmllint disable
import QtQuick
import "../../theme"
import "../common"
import "../effects"
import "../export"
import "../lumetri"
import "../project"
import "../subtitles"

// A clip's name plate. Kept in its own file because the rules around it are not
// about text: the label has to stay legible over whatever thumbnail is behind
// it, and it has to get out of the way once the clip is too narrow to hold it.
Item {
    id: root

    required property string label
    required property bool overThumbnail
    // Subtitle clips use a light fill, so their text is dark and unshadowed.
    property bool darkText: false
    property int leftInset: 6

    implicitHeight: text.implicitHeight + 4

    // Legibility scrim: a plain outline breaks up over busy footage, a gradient
    // strip behind the text does not.
    Rectangle {
        anchors.fill: parent
        visible: root.overThumbnail && !root.darkText
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.55) }
            GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.0) }
        }
    }

    Text {
        id: text
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: 2
        anchors.leftMargin: root.leftInset
        anchors.rightMargin: 6
        text: root.label
        color: root.darkText ? "#191919" : "white"
        font.pixelSize: Theme.fsXs
        font.weight: Font.Medium
        elide: Text.ElideRight
        maximumLineCount: 1
        style: root.darkText ? Text.Normal : Text.Outline
        styleColor: Qt.rgba(0, 0, 0, 0.55)
    }
}
