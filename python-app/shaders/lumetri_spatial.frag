// unsharp and hqdn3d, on the GPU. One program, two modes, because both are a box
// average of the neighbourhood recombined with the centre pixel and the only
// differences are the radius and the recombination.
//
//   mode 0 - unsharp=5:5:amount, i.e. p + amount * (p - box(2))
//   mode 1 - hqdn3d, approximated as mix(p, box(1), strength). The filter also
//            averages across time, which a single still cannot do; this is the
//            same spatial stand-in LumetriPreview::apply() uses.
//
// Run as its own pass rather than folded into lumetri_grade so the neighbourhood
// it averages is the graded one, which is the order the CPU loop and the export
// filtergraph both use.
#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    // x mode, y amount, z texel width, w texel height
    vec4 gSpatial;
    // The drawn picture inside the pass texture, normalised. Taps are held
    // inside it so the transparent letterbox never bleeds into the picture -
    // the CPU version replicates the frame's border for the same reason.
    vec4 gPicture;
};

layout(binding = 1) uniform sampler2D source;

// Unpremultiplied, border-replicated tap.
vec3 tap(vec2 uv) {
    vec2 halfTexel = vec2(gSpatial.z, gSpatial.w) * 0.5;
    vec2 lo = gPicture.xy + halfTexel;
    vec2 hi = gPicture.xy + gPicture.zw - halfTexel;
    vec4 t = texture(source, clamp(uv, lo, hi));
    return t.a > 0.0 ? clamp(t.rgb / t.a, 0.0, 1.0) : vec3(0.0);
}

vec3 boxAverage(int radius) {
    vec2 texel = vec2(gSpatial.z, gSpatial.w);
    vec3 sum = vec3(0.0);
    float count = 0.0;
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            if (abs(x) > radius || abs(y) > radius)
                continue;
            sum += tap(qt_TexCoord0 + vec2(float(x), float(y)) * texel);
            count += 1.0;
        }
    }
    return sum / count;
}

void main() {
    vec4 src = texture(source, qt_TexCoord0);
    if (src.a <= 0.0) {
        fragColor = vec4(0.0);
        return;
    }
    vec3 c = clamp(src.rgb / src.a, 0.0, 1.0);

    if (gSpatial.x < 0.5) {
        vec3 soft = boxAverage(2);
        c = clamp(c + gSpatial.y * (c - soft), 0.0, 1.0);
    } else {
        c = clamp(mix(c, boxAverage(1), clamp(gSpatial.y, 0.0, 1.0)), 0.0, 1.0);
    }

    fragColor = vec4(c * src.a, src.a) * qt_Opacity;
}
