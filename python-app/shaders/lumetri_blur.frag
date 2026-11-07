// gblur, on the GPU: one axis per pass, so a sigma of 5 costs 2 * 31 taps instead
// of the 961 a 2D kernel would need. Run twice, horizontal then vertical.
//
// This is a real Gaussian rather than the three box passes LumetriPreview::apply()
// falls back to on the CPU, so it lands closer to what vf_gblur writes than the
// CPU preview does.
#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    // x sigma, y step x, z step y, w unused
    vec4 gBlur;
    vec4 gPicture; // drawn picture inside the pass texture, normalised
};

layout(binding = 1) uniform sampler2D source;

void main() {
    vec4 src = texture(source, qt_TexCoord0);
    if (src.a <= 0.0) {
        fragColor = vec4(0.0);
        return;
    }
    float sigma = max(gBlur.x, 0.0001);
    // 3 sigma covers 99.7% of the kernel; the cap is what keeps the loop bounded
    // for a slider that can ask for more blur than is worth sampling.
    float radius = min(ceil(sigma * 3.0), 24.0);
    vec2 dir = vec2(gBlur.y, gBlur.z);
    vec2 halfTexel = abs(dir) * 0.5;
    vec2 lo = gPicture.xy + halfTexel;
    vec2 hi = gPicture.xy + gPicture.zw - halfTexel;

    vec3 sum = vec3(0.0);
    float weightSum = 0.0;
    for (int i = -24; i <= 24; ++i) {
        float offset = float(i);
        if (abs(offset) > radius)
            continue;
        float weight = exp(-0.5 * (offset / sigma) * (offset / sigma));
        vec4 t = texture(source, clamp(qt_TexCoord0 + dir * offset, lo, hi));
        // Border-replicated and unpremultiplied, matching the CPU blur's edge
        // handling; a transparent tap would otherwise darken the picture edge.
        sum += (t.a > 0.0 ? clamp(t.rgb / t.a, 0.0, 1.0) : vec3(0.0)) * weight;
        weightSum += weight;
    }

    vec3 c = clamp(sum / weightSum, 0.0, 1.0);
    fragColor = vec4(c * src.a, src.a) * qt_Opacity;
}
