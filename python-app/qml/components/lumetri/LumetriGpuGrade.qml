// The monitor's Lumetri chain, in the fragment stage.
//
// Whatever is declared inside this item is the picture: it gets rendered to a
// texture and the grade runs over it, so the caller keeps owning its Image or
// VideoOutput and only loses the cost of grading it on the GUI thread.
// Backend.lumetriUniforms carries the whole state - rebuildLumetriUniforms()
// flattens the same LumetriPreview::Correction the CPU path applies per pixel.
//
// One nested layer per stage, in the order the export filtergraph applies them:
// grade -> unsharp -> denoise -> blur H -> blur V -> vignette. A stage whose
// slider sits at zero leaves its layer disabled, so it costs no render target
// and no draw call, and an ungraded monitor draws what it did before.
import QtQuick
import QtQuick.Window
import CutPro 1.0

Item {
    id: root

    // Where the picture lands inside this item, normalised. The monitor fits the
    // frame with PreserveAspectFit, so the rest is transparent bars - which the
    // neighbourhood passes must not average in and the grade must not tint.
    property rect picture: Qt.rect(0.0, 0.0, 1.0, 1.0)
    // The picture in pixels. Only the aspect is read: it is what makes the
    // vignette's falloff reach the frame's corner instead of the item's.
    property size pictureSize: Qt.size(width, height)

    default property alias content: pictureHost.data

    // False under the software scene graph, where ShaderEffect draws nothing and
    // Backend hands out CPU-graded frames instead. Every stage reads through it,
    // so the whole chain collapses to a plain pass-through item.
    property bool gradeEnabled: true

    readonly property var uni: Backend.lumetriUniforms
    readonly property vector4d vZero: Qt.vector4d(0.0, 0.0, 0.0, 0.0)
    // The map is empty until the first refreshPreviewGrade(), and a clip with no
    // Lumetri never fills it, so every stage has to read as off when absent.
    function v4(name, fallback) {
        return uni[name] !== undefined ? uni[name] : fallback
    }
    readonly property bool colorOn: gradeEnabled && uni.colorActive === true
    readonly property real sharpenAmount: gradeEnabled && uni.sharpen !== undefined
                                          ? uni.sharpen : 0.0
    readonly property real denoiseAmount: gradeEnabled && uni.denoise !== undefined
                                          ? uni.denoise : 0.0
    readonly property real blurSigma: gradeEnabled && uni.blurSigma !== undefined
                                      ? uni.blurSigma : 0.0
    readonly property bool vignetteOn: gradeEnabled && uni.vignetteActive === true

    readonly property vector4d gPicture: Qt.vector4d(picture.x, picture.y,
                                                     picture.width, picture.height)
    readonly property vector4d gGeom: Qt.vector4d(Math.max(1, pictureSize.width),
                                                  Math.max(1, pictureSize.height),
                                                  0.0, 0.0)
    // Layer textures are allocated in device pixels, so one texel is
    // 1/(size * dpr). That puts the sharpen and blur radii in screen pixels
    // rather than the decoded frame's pixels, which is the one place this chain
    // cannot match LumetriPreview::apply(): matching it would mean grading at
    // full decode size and then scaling, i.e. paying for pixels never shown.
    readonly property real dpr: Screen.devicePixelRatio > 0 ? Screen.devicePixelRatio : 1.0
    readonly property real texelW: width > 0 ? 1.0 / (width * dpr) : 0.0
    readonly property real texelH: height > 0 ? 1.0 / (height * dpr) : 0.0

    // Outermost first, so the picture ends up innermost and each layer feeds the
    // one above it. Every level fills root, which keeps one picture rect and one
    // texel size correct for the whole chain.
    Item {
        anchors.fill: parent
        layer.enabled: root.vignetteOn
        layer.effect: vignetteEffect

        Item {
            anchors.fill: parent
            layer.enabled: root.blurSigma > 0.0001
            layer.effect: blurVerticalEffect

            Item {
                anchors.fill: parent
                layer.enabled: root.blurSigma > 0.0001
                layer.effect: blurHorizontalEffect

                Item {
                    anchors.fill: parent
                    layer.enabled: root.denoiseAmount > 0.0001
                    layer.effect: denoiseEffect

                    Item {
                        anchors.fill: parent
                        layer.enabled: root.sharpenAmount > 0.0001
                        layer.effect: sharpenEffect

                        Item {
                            id: pictureHost
                            anchors.fill: parent
                            layer.enabled: root.colorOn
                            layer.effect: gradeEffect
                        }
                    }
                }
            }
        }
    }

    // The lookup tables. They have to be drawn items: an Image only refreshes
    // the texture it hands a ShaderEffect while it is being rendered, and the
    // URLs change on every grade generation. A 1x1 clipping window keeps them in
    // the render pass with everything but a single scissored pixel cut away.
    Item {
        width: 1
        height: 1
        clip: true

        Image {
            id: curveImage
            // Outside the clip rect: rendered, never seen.
            x: 2
            y: 2
            source: Backend.lumetriCurveUrl
            width: Math.max(1, sourceSize.width)
            height: Math.max(1, sourceSize.height)
            // Nearest, unfiltered, uncached: every texel is a table entry, and a
            // smoothed or stale one would quietly rewrite the grade.
            smooth: false
            mipmap: false
            cache: false
            asynchronous: false
        }

        Image {
            id: cubeImage
            x: 2
            y: 2
            source: Backend.lumetriCubeUrl
            width: Math.max(1, sourceSize.width)
            height: Math.max(1, sourceSize.height)
            smooth: false
            mipmap: false
            cache: false
            asynchronous: false
        }
    }

    Component {
        id: gradeEffect
        ShaderEffect {
            property var source
            property vector4d gFlags: root.v4("gFlags", root.vZero)
            property vector4d gBasic: root.v4("gBasic", Qt.vector4d(2.0, 1.0, 1.0, 0.0))
            property vector4d gMix: root.v4("gMix", root.vZero)
            property vector4d gShadow: root.v4("gShadow", root.vZero)
            property vector4d gMidtone: root.v4("gMidtone", root.vZero)
            property vector4d gHighlight: root.v4("gHighlight", root.vZero)
            property vector4d gHslKey: root.v4("gHslKey", root.vZero)
            property vector4d gHslRange: root.v4("gHslRange", root.vZero)
            property vector4d gHslAdjust: root.v4("gHslAdjust", root.vZero)
            property var curveTex: curveImage
            // A sampler still needs a texture on the branch that never reads it,
            // so the curve strip stands in when the clip has no input LUT.
            property var cubeTex: cubeImage.status === Image.Ready ? cubeImage : curveImage
            fragmentShader: "qrc:/shaders/lumetri_grade.frag.qsb"
        }
    }

    // unsharp=5:5:amount, and then hqdn3d's spatial half, from the same program.
    Component {
        id: sharpenEffect
        ShaderEffect {
            property var source
            property vector4d gSpatial: Qt.vector4d(0.0, root.sharpenAmount,
                                                    root.texelW, root.texelH)
            property vector4d gPicture: root.gPicture
            fragmentShader: "qrc:/shaders/lumetri_spatial.frag.qsb"
        }
    }

    Component {
        id: denoiseEffect
        ShaderEffect {
            property var source
            property vector4d gSpatial: Qt.vector4d(1.0, root.denoiseAmount,
                                                    root.texelW, root.texelH)
            property vector4d gPicture: root.gPicture
            fragmentShader: "qrc:/shaders/lumetri_spatial.frag.qsb"
        }
    }

    // Separable, so the two passes together cost 2n taps instead of n*n.
    Component {
        id: blurHorizontalEffect
        ShaderEffect {
            property var source
            property vector4d gBlur: Qt.vector4d(root.blurSigma, root.texelW, 0.0, 0.0)
            property vector4d gPicture: root.gPicture
            fragmentShader: "qrc:/shaders/lumetri_blur.frag.qsb"
        }
    }

    Component {
        id: blurVerticalEffect
        ShaderEffect {
            property var source
            property vector4d gBlur: Qt.vector4d(root.blurSigma, 0.0, root.texelH, 0.0)
            property vector4d gPicture: root.gPicture
            fragmentShader: "qrc:/shaders/lumetri_blur.frag.qsb"
        }
    }

    Component {
        id: vignetteEffect
        ShaderEffect {
            property var source
            property vector4d gVignette: root.v4("gVignette", root.vZero)
            property vector4d gPicture: root.gPicture
            property vector4d gGeom: root.gGeom
            fragmentShader: "qrc:/shaders/lumetri_vignette.frag.qsb"
        }
    }
}
