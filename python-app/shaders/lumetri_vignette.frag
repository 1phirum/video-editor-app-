// vignette, on the GPU. cos(angle * dnorm)^4, and backward divides where forward
// multiplies, exactly as vf_vignette does; the strength lives in the angle, which
// LumetriPreview::build() derives from the amount/midpoint/feather sliders.
//
// Its own pass so it stays last in the chain, after sharpen and blur, which is the
// order the export filtergraph applies it in.
#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    // x angle, y aspect, z backward (1 = divide), w unused
    vec4 gVignette;
    vec4 gPicture; // drawn picture inside the pass texture, normalised
    // The picture in pixels. Only the ratio matters, but the falloff is measured
    // from the frame's centre to its corner, so it has to be the picture's own
    // shape and not the letterboxed item's.
    vec4 gGeom;
};

layout(binding = 1) uniform sampler2D source;

void main() {
    vec4 src = texture(source, qt_TexCoord0);
    if (src.a <= 0.0) {
        fragColor = vec4(0.0);
        return;
    }
    vec3 c = clamp(src.rgb / src.a, 0.0, 1.0);

    vec2 rel = (qt_TexCoord0 - gPicture.xy) / max(gPicture.zw, vec2(1e-6));
    float aspect = gVignette.y > 0.0 ? gVignette.y : 1.0;
    vec2 d = vec2((rel.x - 0.5) * gGeom.x, (rel.y - 0.5) * gGeom.y * aspect);
    float dmax = length(vec2(gGeom.x * 0.5, gGeom.y * 0.5 * aspect));
    float dnorm = dmax > 0.0 ? length(d) / dmax : 0.0;

    float angle = gVignette.x * dnorm;
    float factor = 0.0;
    if (angle < 1.5707963) {
        float k = cos(angle);
        factor = (k * k) * (k * k);
    }
    c = clamp(gVignette.z > 0.5 ? c / max(factor, 0.05) : c * factor, 0.0, 1.0);

    fragColor = vec4(c * src.a, src.a) * qt_Opacity;
}
