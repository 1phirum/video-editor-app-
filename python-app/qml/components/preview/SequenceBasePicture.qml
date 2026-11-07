import QtQuick
import QtMultimedia
import CutPro 1.0
import "../lumetri"

Item {
    id: root
    objectName: "sequenceBasePicture"
    property var activeMedia: null
    property var activeEffects: ({})
    property string clipId: ""
    property url sourceUrl: ""
    property bool useProcessPreview: true
    property int keyframeRevision: 0
    property alias videoOutput: videoOutput

    function animatedValue(key, fallback) {
        var value = activeEffects[key]
        var base = value === undefined || value === null ? fallback : Number(value)
        if (keyframeRevision < 0 || !clipId)
            return base
        return Backend.keyframeEngine.valueAt(clipId, key,
                    Math.max(0, Number(Backend.playheadMs)), base)
    }

    // Video content container — sized by Scale so only the
    // video scales, not the letterbox. Matches Premiere Pro
    // where the sequence frame stays fixed and the clip
    // content scales within it.
    Item {
        id: videoContainer
        // Non-uniform scale, matching the bounds the export
        // chain clamps to (ClipEffectsPipeline::videoFilters).
        // The monitor used to read `scale` alone, so turning
        // Uniform Scale off changed nothing on screen and the
        // squash only appeared in the rendered file.
        readonly property bool uniform: root.activeEffects.uniformScale !== false
        readonly property real baseScale:
            Math.max(10, Math.min(400, root.animatedValue("scale", 100)))
        readonly property real scaleX:
            (uniform ? baseScale
                     : Math.max(10, Math.min(600, root.animatedValue("scaleWidth", baseScale)))) / 100
        readonly property real scaleY:
            (uniform ? baseScale
                     : Math.max(10, Math.min(600, root.animatedValue("scaleHeight", baseScale)))) / 100
        // Anchor Point is the pivot that scale and rotation turn
        // about, and the point Position places. 0.5/0.5 is the
        // centre, so the default reduces to the old centre-based
        // arithmetic exactly.
        readonly property real anchorX:
            Math.max(0, Math.min(1, root.animatedValue("anchorPointX", 0.5)))
        readonly property real anchorY:
            Math.max(0, Math.min(1, root.animatedValue("anchorPointY", 0.5)))
        property real scaleFactor: baseScale / 100
        width: parent.width * scaleX
        height: parent.height * scaleY
        x: parent.width / 2
           + root.animatedValue("positionX", 0) / 100 * parent.width
           - anchorX * width
        y: parent.height / 2
           + root.animatedValue("positionY", 0) / 100 * parent.height
           - anchorY * height
        opacity: root.animatedValue("opacity", 100) / 100
        // Rotation pivots about the centre, not the anchor,
        // because FFmpeg's `rotate` is centre-based and export
        // has to agree with what this shows. Anchor Point is
        // therefore the pivot for scale and the point Position
        // places - which is what it is mostly used for (anchor
        // a corner and the clip grows away from it).
        rotation: root.animatedValue("rotation", 0)
        transformOrigin: Item.Center
        transform: Scale {
            origin.x: videoContainer.width / 2
            origin.y: videoContainer.height / 2
            xScale: root.activeEffects.horizontalFlip ? -1 : 1
            yScale: root.activeEffects.verticalFlip ? -1 : 1
        }

        Image {
            anchors.fill: parent
            anchors.margins: 8
            source: root.useProcessPreview && root.activeMedia
                    ? (root.activeMedia.thumbnailUrl || "") : ""
            fillMode: Image.PreserveAspectFit
            visible: root.useProcessPreview
                     && Backend.previewFrameUrl === ""
            asynchronous: true
        }

        // The Lumetri chain, in the fragment stage. It wraps the
        // two items that carry the picture, so the clip's own
        // scale/rotation/flip above still apply to the graded
        // result. The placeholder thumbnail and the VideoOutput
        // stay outside it: one stands in for a frame that has not
        // arrived, and the other only draws on the QMediaPlayer
        // path, which has no grade to show.
        LumetriGpuGrade {
            id: gradeLayer
            anchors.fill: parent
            gradeEnabled: Backend.gpuGrading

            // Both children fit inside their own box, so the
            // grade needs to know where the picture actually
            // landed - everything else is transparent letterbox.
            readonly property Image pictureItem:
                previewFrame.visible
                ? previewFrame
                : (stillFrame.visible ? stillFrame : null)
            readonly property bool pictureKnown:
                pictureItem !== null && width > 0 && height > 0
                && pictureItem.paintedWidth > 0
                && pictureItem.paintedHeight > 0
            // Latched. A commit from the colour panel emits
            // timelineChanged(), which can drop the frame item
            // for a moment; falling back to the whole item then
            // hands the vignette the item's corners instead of
            // the frame's and lets the sharpen and blur taps
            // average in the bars - a colour change that looks
            // like the picture warped. The last rect that was
            // actually known holds until a new one is.
            property rect heldPicture: Qt.rect(0.0, 0.0, 1.0, 1.0)
            property size heldPictureSize: Qt.size(width, height)
            onPictureChanged: if (pictureKnown) heldPicture = picture
            onPictureSizeChanged:
                if (pictureKnown) heldPictureSize = pictureSize
            picture: pictureKnown
                     ? Qt.rect((pictureItem.x + (pictureItem.width - pictureItem.paintedWidth) / 2) / width,
                               (pictureItem.y + (pictureItem.height - pictureItem.paintedHeight) / 2) / height,
                               pictureItem.paintedWidth / width,
                               pictureItem.paintedHeight / height)
                     : gradeLayer.heldPicture
            pictureSize: pictureKnown
                         ? Qt.size(pictureItem.paintedWidth,
                                   pictureItem.paintedHeight)
                         : gradeLayer.heldPictureSize

            Image {
                id: previewFrame
                anchors.fill: parent
                source: root.useProcessPreview
                        ? Backend.previewFrameUrl : ""
                fillMode: Image.PreserveAspectFit
                visible: root.useProcessPreview
                         && Backend.previewFrameUrl !== ""
                asynchronous: false
                cache: false
            }

            Image {
                id: stillFrame
                anchors.fill: parent
                anchors.margins: 8
                source: root.activeMedia && root.activeMedia.kind === "image" ? root.sourceUrl : ""
                fillMode: Image.PreserveAspectFit
                visible: root.activeMedia && root.activeMedia.kind === "image"
                asynchronous: true
            }
        }

        VideoOutput {
            id: videoOutput
            anchors.fill: parent
            fillMode: VideoOutput.PreserveAspectFit
            visible: root.activeMedia && root.activeMedia.kind === "video"
                     && !root.useProcessPreview
        }
    }
}
