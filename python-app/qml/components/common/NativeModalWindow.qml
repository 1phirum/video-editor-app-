//qmllint disable
// Reusable native Windows modal shell. Content is supplied as normal QML children.
import QtQuick
import "../effects"
import "../export"
import "../lumetri"
import "../project"
import "../subtitles"
import "../timeline"

Window {
    id: shell

    property var ownerWindow: null
    property int dialogWidth: 720
    property int dialogHeight: 480
    property string dialogTitle: ""

    default property alias contentData: contentHost.data

    visible: false
    modality: Qt.ApplicationModal
    flags: Qt.Dialog | Qt.WindowTitleHint | Qt.WindowSystemMenuHint | Qt.WindowCloseButtonHint
    width: dialogWidth
    height: dialogHeight
    transientParent: ownerWindow
    title: dialogTitle

    Item {
        id: contentHost
        anchors.fill: parent
    }

    function centerOnOwner() {
        if (!ownerWindow)
            return
        x = Math.round(ownerWindow.x + (ownerWindow.width - width) / 2)
        y = Math.round(ownerWindow.y + (ownerWindow.height - height) / 2)
    }

    function openNative() {
        centerOnOwner()
        show()
        raise()
        requestActivate()
    }
}
