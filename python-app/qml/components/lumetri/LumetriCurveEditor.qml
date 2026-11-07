//qmllint disable
import QtQuick
import QtQuick.Shapes
import CutPro 1.0
import "../../theme"
import "../common"
import "../effects"
import "../export"
import "../project"
import "../subtitles"
import "../timeline"

// Premiere's curve editor: a black plot with a thin grid and no frame or colour
// wash around the line. The curve is a ShapePath rebuilt from the control points
// by binding, and every point carries its own MouseArea, so a drag grabs that
// point directly instead of hit-testing the whole plot on each press.
Item {
    id: root
    property var points: []
    property color curveColor: "#ffffff"
    property bool flatIdentity: false
    // The Hue/Saturation rows and the RGB plot are the same height: one short,
    // wide plot, so the section scrolls like Premiere's instead of one tall
    // square pushing the rows below it off the panel.
    property bool wideRow: false
    property int gridColumns: wideRow ? 8 : 4
    property int gridRows: wideRow ? 8 : 4
    readonly property real aspect: 2.4
    signal pointsCommitted(var points)

    implicitWidth: 188
    // Premiere sizes the plot from the panel width, so the curve keeps its shape
    // as the panel is resized instead of flattening into a strip. Height never
    // feeds back into width here - the column above supplies that - so the
    // dependency is one-way.
    implicitHeight: width > 0 ? Math.max(96, Math.min(280, width / aspect)) : 124

    property var editPoints: defaultPoints()
    // The point a MouseArea currently holds, or -1. Also the guard that keeps an
    // incoming points change from overwriting the drag in progress.
    property int activePoint: -1
    property real grabDx: 0
    property real grabDy: 0
    readonly property color plotColor: Theme.surface("#070707")
    readonly property color gridColor: Theme.surface("#343434")

    function defaultPoints() {
        return flatIdentity ? [{ x: 0, y: 0.5 }, { x: 1, y: 0.5 }]
                            : [{ x: 0, y: 0 }, { x: 1, y: 1 }]
    }
    function clonePoints(source) {
        var result = []
        if (source && source.length >= 2) {
            for (var i = 0; i < source.length; ++i)
                result.push({ x: Number(source[i].x), y: Number(source[i].y) })
        } else {
            result = defaultPoints()
        }
        result.sort(function(a, b) { return a.x - b.x })
        return result
    }
    function syncPoints() {
        if (root.activePoint < 0)
            editPoints = clonePoints(points)
    }
    onPointsChanged: syncPoints()
    onFlatIdentityChanged: syncPoints()
    Component.onCompleted: syncPoints()

    // Natural cubic spline coefficients for editPoints - the same solve
    // src/app/lumetri/lumetri_preview.cpp::curveLut runs, and vf_curves under
    // it, so the line on screen is the table the grade will actually apply.
    // It is a binding, which is what makes a drag recompute the whole curve
    // rather than only the span next to the point being moved.
    readonly property var spline: solveSpline(editPoints)

    function solveSpline(pts) {
        var n = pts ? pts.length : 0
        var b = [], c = [], d = []
        if (n < 2)
            return { b: b, c: c, d: d }
        var h = [], alpha = [], l = [], mu = [], z = []
        var i
        for (i = 0; i < n - 1; ++i)
            h.push(Math.max(0.000001, pts[i + 1].x - pts[i].x))
        for (i = 0; i < n; ++i) {
            alpha.push(0); l.push(0); mu.push(0); z.push(0)
            b.push(0); c.push(0); d.push(0)
        }
        for (i = 1; i < n - 1; ++i)
            alpha[i] = 3 * ((pts[i + 1].y - pts[i].y) / h[i]
                            - (pts[i].y - pts[i - 1].y) / h[i - 1])
        l[0] = 1
        for (i = 1; i < n - 1; ++i) {
            l[i] = 2 * (pts[i + 1].x - pts[i - 1].x) - h[i - 1] * mu[i - 1]
            mu[i] = h[i] / l[i]
            z[i] = (alpha[i] - h[i - 1] * z[i - 1]) / l[i]
        }
        for (var j = n - 2; j >= 0; --j) {
            c[j] = z[j] - mu[j] * c[j + 1]
            b[j] = (pts[j + 1].y - pts[j].y) / h[j]
                   - h[j] * (c[j + 1] + 2 * c[j]) / 3
            d[j] = (c[j + 1] - c[j]) / (3 * h[j])
        }
        return { b: b, c: c, d: d }
    }
    // Held flat outside the first and last point, again matching the filter.
    function curveY(x) {
        var pts = root.editPoints
        if (!pts || pts.length < 2)
            return 0
        var last = pts.length - 1
        if (x <= pts[0].x)
            return pts[0].y
        if (x >= pts[last].x)
            return pts[last].y
        var j = 0
        while (j < last - 1 && x > pts[j + 1].x)
            ++j
        var dx = x - pts[j].x
        var s = root.spline
        return pts[j].y + dx * (s.b[j] + dx * (s.c[j] + dx * s.d[j]))
    }

    // One SVG cubic per span. The conversion is exact - each Bezier handle sits a
    // third of the span away along the polynomial's own slope there - so the
    // stroke lands on the sampled table instead of near it.
    readonly property string curvePath: buildPath(editPoints, spline, width, height)

    function buildPath(pts, s, w, h) {
        if (!pts || pts.length < 2 || w <= 0 || h <= 0)
            return ""
        var last = pts.length - 1
        var out = "M 0 " + (1 - pts[0].y) * h
                  + " L " + pts[0].x * w + " " + (1 - pts[0].y) * h
        for (var j = 0; j < last; ++j) {
            var span = Math.max(0.000001, pts[j + 1].x - pts[j].x)
            var slope0 = s.b[j]
            var slope1 = s.b[j] + span * (2 * s.c[j] + 3 * s.d[j] * span)
            out += " C " + (pts[j].x + span / 3) * w
                   + " " + (1 - (pts[j].y + slope0 * span / 3)) * h
                   + " " + (pts[j + 1].x - span / 3) * w
                   + " " + (1 - (pts[j + 1].y - slope1 * span / 3)) * h
                   + " " + pts[j + 1].x * w
                   + " " + (1 - pts[j + 1].y) * h
        }
        return out + " L " + w + " " + (1 - pts[last].y) * h
    }

    function addPoint(px, py) {
        var next = root.clonePoints(root.editPoints)
        var x = Math.max(0, Math.min(1, px / width))
        // A press that lands on top of an existing point grabs that point rather
        // than stacking a second one beside it: two points sharing an x are the
        // one input the spline solve cannot answer sensibly.
        for (var k = 0; k < next.length; ++k) {
            if (Math.abs(next[k].x - x) < 0.02)
                return k
        }
        if (next.length >= 16)
            return -1
        var added = { x: x, y: Math.max(0, Math.min(1, 1 - py / height)) }
        next.push(added)
        next.sort(function(a, b) { return a.x - b.x })
        root.editPoints = next
        return next.indexOf(added)
    }
    function beginDrag(index, px, py) {
        root.activePoint = index
        root.grabDx = 0
        root.grabDy = 0
        if (index < 0)
            return
        // Where inside the point the press landed, so the point follows the
        // cursor instead of jumping its centre under it.
        root.grabDx = px - root.editPoints[index].x * width
        root.grabDy = py - (1 - root.editPoints[index].y) * height
    }
    function dragTo(px, py) {
        var i = root.activePoint
        if (i < 0 || width <= 0 || height <= 0)
            return
        var next = root.clonePoints(root.editPoints)
        // Points keep their order, and stay far enough apart to be a curve the
        // 256-entry LUT can actually hold: two points a fraction of an entry
        // apart make a near-vertical span, and the natural spline answers that
        // with a large overshoot in the spans on either side - colour the user
        // never drew. A 0.02 floor is five table entries.
        var gap = 0.02
        var minX = i > 0 ? next[i - 1].x + gap : 0
        var maxX = i + 1 < next.length ? next[i + 1].x - gap : 1
        next[i] = {
            x: Math.max(0, Math.min(1, Math.max(minX, Math.min(maxX,
                            (px - root.grabDx) / width)))),
            y: Math.max(0, Math.min(1, 1 - (py - root.grabDy) / height))
        }
        root.editPoints = next
    }
    // Two point lists that agree to the precision the LUT can represent. Every
    // commit costs an undo entry and a timelineChanged(), and that re-seeks the
    // frame the monitor is paused on, so a gesture that moved nothing has to
    // stay silent instead of making the still image jump.
    function samePoints(a, b) {
        var an = a ? a.length : 0
        if (an === 0 || !b || b.length !== an)
            return false
        for (var i = 0; i < an; ++i) {
            if (Math.abs(Number(a[i].x) - Number(b[i].x)) > 0.001
                    || Math.abs(Number(a[i].y) - Number(b[i].y)) > 0.001)
                return false
        }
        return true
    }
    function commitPoints() {
        var next = root.clonePoints(root.editPoints)
        // clonePoints() on the published side too: an empty list means "default"
        // there, and a curve dragged back to the diagonal must compare equal.
        if (root.samePoints(next, root.clonePoints(root.points)))
            return
        root.pointsCommitted(next)
    }
    function endDrag() {
        var held = root.activePoint >= 0
        // Cleared first: the commit can come back through points synchronously,
        // and syncPoints() has to be allowed to take that value.
        root.activePoint = -1
        if (held)
            root.commitPoints()
    }
    function removePoint(index) {
        if (index < 0 || index >= root.editPoints.length
                || root.editPoints.length <= 2)
            return
        var next = root.clonePoints(root.editPoints)
        next.splice(index, 1)
        root.activePoint = -1
        root.editPoints = next
        root.commitPoints()
    }

    Rectangle {
        id: plot
        anchors.fill: parent
        color: root.plotColor
        // A spline can overshoot past 0 or 1 between points; the filter clamps
        // the table there, so the drawing is cut at the frame the same way.
        clip: true

        Repeater {
            model: root.gridColumns - 1
            delegate: Rectangle {
                required property int index
                x: Math.round(plot.width * (index + 1) / root.gridColumns)
                width: 1
                height: plot.height
                color: root.gridColor
            }
        }
        Repeater {
            model: root.gridRows - 1
            delegate: Rectangle {
                required property int index
                y: Math.round(plot.height * (index + 1) / root.gridRows)
                width: plot.width
                height: 1
                color: root.gridColor
            }
        }

        // One solid white stroke for every row, RGB and Hue/Saturation alike.
        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeColor: root.curveColor
                strokeWidth: 2
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                PathSvg { path: root.curvePath }
            }
        }

        // Below the point handles, so a press only lands here when it missed
        // every point - which is exactly when Premiere adds one, hence PenAdd.
        MouseArea {
            id: plotMouse
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            // The section is inside a ScrollView: without this the Flickable
            // takes the grab as soon as the drag turns vertical and the panel
            // scrolls instead of the point moving.
            preventStealing: true
            AppCursor.name: "PenAdd"
            onPressed: mouseEvent => root.beginDrag(
                           root.addPoint(mouseEvent.x, mouseEvent.y),
                           mouseEvent.x, mouseEvent.y)
            onPositionChanged: mouseEvent => {
                if (plotMouse.pressed)
                    root.dragTo(mouseEvent.x, mouseEvent.y)
            }
            onReleased: root.endDrag()
            onCanceled: root.endDrag()
            onDoubleClicked: {
                // The presses that opened the double click may have added a
                // point; the reset replaces it, and clearing activePoint keeps
                // the release that follows from publishing the discarded one.
                root.activePoint = -1
                root.editPoints = root.defaultPoints()
                root.commitPoints()
            }
        }
    }

    // Outside the clipping plot: a point sitting on 0 or 1 would otherwise be
    // sliced in half, and its hit area with it.
    Repeater {
        model: root.editPoints.length
        delegate: Item {
            id: handle
            required property int index
            readonly property var point: root.editPoints[index]
            readonly property bool held: root.activePoint === handle.index

            width: 15
            height: 15
            x: (point ? point.x : 0) * root.width - width / 2
            y: (1 - (point ? point.y : 0)) * root.height - height / 2

            Rectangle {
                anchors.centerIn: parent
                width: handle.held || grab.containsMouse ? 9 : 7
                height: width
                radius: width / 2
                color: handle.held ? "#ffffff" : root.curveColor
                border.width: 1
                border.color: "#0f0f0f"
            }

            // The targeted hit area. Because the press lands on the point itself,
            // two points a few pixels apart can still be told apart, and the drag
            // never has to re-run a nearest-point search per move.
            MouseArea {
                id: grab
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton
                // Keeps the drag on the point once it starts: the enclosing
                // ScrollView's Flickable would otherwise steal the grab the
                // moment the gesture went vertical.
                preventStealing: true
                AppCursor.name: "Pen"
                onPressed: mouseEvent => {
                    var p = grab.mapToItem(root, mouseEvent.x, mouseEvent.y)
                    if ((mouseEvent.modifiers & Qt.ControlModifier) !== 0
                            && root.editPoints.length > 2) {
                        // Deferred: this delegate is what the removal destroys.
                        Qt.callLater(root.removePoint, handle.index)
                        return
                    }
                    root.beginDrag(handle.index, p.x, p.y)
                }
                onPositionChanged: mouseEvent => {
                    if (!grab.pressed)
                        return
                    // Mapped every move because the handle travels with the
                    // point, so the local coordinates shift under the cursor.
                    var p = grab.mapToItem(root, mouseEvent.x, mouseEvent.y)
                    root.dragTo(p.x, p.y)
                }
                onReleased: root.endDrag()
                onCanceled: root.endDrag()
            }
        }
    }
}
