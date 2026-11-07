// Lumetri per-pixel chain, on the GPU.
//
// A straight port of LumetriPreview::apply()'s inner loop - same order, same
// constants - so the picture does not change when the work moves off the CPU.
// The CPU version still exists for the software scene graph, where ShaderEffect
// draws nothing; see Backend::gpuGrading.
//
// The two lookup tables arrive as textures rather than uniform arrays: curveTex
// is 256x1 with the three curve tables in R/G/B and eq's tone table in A, and
// cubeTex is the input .cube unrolled to a (n*n)x(n) strip of blue slices. Both
// are nearest-filtered and sampled at texel centres, so a fetch returns the
// exact table entry the CPU loop would have indexed.
#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    // x lutMode (0 off, 1 tetrahedral, 2 trilinear), y toneActive, z curvesActive,
    // w balanceActive
    vec4 gFlags;
    // x cube size, y saturation factor, z cos(hue), w sin(hue)
    vec4 gBasic;
    // x hueActive, y lightness, z hslActive, w hslWide
    vec4 gMix;
    vec4 gShadow;    // colorbalance offsets, rgb
    vec4 gMidtone;
    vec4 gHighlight;
    vec4 gHslKey;    // hue centre, hue width, sat min, sat max
    vec4 gHslRange;  // luma min, luma max, -, -
    vec4 gHslAdjust; // hue degrees, saturation, luma, -
};

layout(binding = 1) uniform sampler2D source;
layout(binding = 2) uniform sampler2D curveTex;
layout(binding = 3) uniform sampler2D cubeTex;

const float kPi = 3.14159265358979;

// Texel centre of entry i of a 256-wide table.
float lutU(float v) { return (clamp(v, 0.0, 1.0) * 255.0 + 0.5) / 256.0; }

vec3 cubeFetch(float n, float xi, float yi, float zi) {
    return textureLod(cubeTex,
                      vec2((zi * n + xi + 0.5) / (n * n), (yi + 0.5) / n),
                      0.0).rgb;
}

// vf_lut3d's two interpolation modes. Tetrahedral is the exporter's default and
// on a log-to-Rec709 LUT the difference from trilinear shows in the shadows, so
// both cases are mirrored rather than collapsed.
vec3 applyCube(vec3 c) {
    float n = gBasic.x;
    float last = n - 1.0;
    vec3 p = clamp(c, 0.0, 1.0) * last;
    // Held one cell short of the edge so the upper corner is always in range.
    vec3 i0 = min(floor(p), vec3(last - 1.0));
    vec3 f = p - i0;
    vec3 c000 = cubeFetch(n, i0.x,       i0.y,       i0.z);
    vec3 c100 = cubeFetch(n, i0.x + 1.0, i0.y,       i0.z);
    vec3 c010 = cubeFetch(n, i0.x,       i0.y + 1.0, i0.z);
    vec3 c110 = cubeFetch(n, i0.x + 1.0, i0.y + 1.0, i0.z);
    vec3 c001 = cubeFetch(n, i0.x,       i0.y,       i0.z + 1.0);
    vec3 c101 = cubeFetch(n, i0.x + 1.0, i0.y,       i0.z + 1.0);
    vec3 c011 = cubeFetch(n, i0.x,       i0.y + 1.0, i0.z + 1.0);
    vec3 c111 = cubeFetch(n, i0.x + 1.0, i0.y + 1.0, i0.z + 1.0);

    if (gFlags.x < 1.5) {
        if (f.x > f.y) {
            if (f.y > f.z)
                return clamp(c000 + (c100 - c000) * f.x + (c110 - c100) * f.y
                             + (c111 - c110) * f.z, 0.0, 1.0);
            if (f.x > f.z)
                return clamp(c000 + (c100 - c000) * f.x + (c111 - c101) * f.y
                             + (c101 - c100) * f.z, 0.0, 1.0);
            return clamp(c000 + (c101 - c001) * f.x + (c111 - c101) * f.y
                         + (c001 - c000) * f.z, 0.0, 1.0);
        }
        if (f.z > f.y)
            return clamp(c000 + (c111 - c011) * f.x + (c011 - c001) * f.y
                         + (c001 - c000) * f.z, 0.0, 1.0);
        if (f.z > f.x)
            return clamp(c000 + (c111 - c011) * f.x + (c010 - c000) * f.y
                         + (c011 - c010) * f.z, 0.0, 1.0);
        return clamp(c000 + (c110 - c010) * f.x + (c010 - c000) * f.y
                     + (c111 - c110) * f.z, 0.0, 1.0);
    }
    vec3 m0 = mix(mix(c000, c100, f.x), mix(c010, c110, f.x), f.y);
    vec3 m1 = mix(mix(c001, c101, f.x), mix(c011, c111, f.x), f.y);
    return clamp(mix(m0, m1, f.z), 0.0, 1.0);
}

// vf_colorbalance's get_component, weights and all: each offset is faded in by
// the pixel's own lightness, which is why a shadow push leaves highlights alone.
float balanceComponent(float v, float l, float s, float m, float h) {
    const float a = 4.0;
    const float b = 0.333;
    const float scale = 0.7;
    s *= clamp((b - l) * a + 0.5, 0.0, 1.0) * scale;
    m *= clamp((l - b) * a + 0.5, 0.0, 1.0)
         * clamp((1.0 - l - b) * a + 0.5, 0.0, 1.0) * scale;
    h *= clamp((l + b - 1.0) * a + 0.5, 0.0, 1.0) * scale;
    return clamp(v + s + m + h, 0.0, 1.0);
}

// BT.601, the matrix vf_hue rotates in.
vec3 rotateHue(float cosH, float sinH, vec3 c) {
    float y = dot(c, vec3(0.299, 0.587, 0.114));
    float u = dot(c, vec3(-0.14713, -0.28886, 0.436));
    float v = dot(c, vec3(0.615, -0.51499, -0.10001));
    float u2 = cosH * u - sinH * v;
    float v2 = sinH * u + cosH * v;
    return clamp(vec3(y + 1.13983 * v2,
                      y - 0.39465 * u2 - 0.58060 * v2,
                      y + 2.03211 * u2), 0.0, 1.0);
}

// GIMP-style lightness, the shape vf_huesaturation's intensity takes.
vec3 applyLightness(float amount, vec3 c) {
    return clamp(amount > 0.0 ? c + (1.0 - c) * amount : c + c * amount,
                 0.0, 1.0);
}

vec3 applySaturation(float factor, vec3 c) {
    float y = dot(c, vec3(0.299, 0.587, 0.114));
    return clamp(vec3(y) + (c - vec3(y)) * factor, 0.0, 1.0);
}

// Hue in degrees plus HSL saturation and lightness, for the secondary key.
vec3 hueSatLuma(vec3 c) {
    float mx = max(c.r, max(c.g, c.b));
    float mn = min(c.r, min(c.g, c.b));
    float l = (mx + mn) * 0.5;
    float span = mx - mn;
    if (span < 1e-6)
        return vec3(0.0, 0.0, l);
    float denom = 1.0 - abs(2.0 * l - 1.0);
    float sat = denom > 1e-6 ? clamp(span / denom, 0.0, 1.0) : 0.0;
    float h;
    if (mx == c.r)      h = (c.g - c.b) / span;
    else if (mx == c.g) h = 2.0 + (c.b - c.r) / span;
    else                h = 4.0 + (c.r - c.g) / span;
    h *= 60.0;
    if (h < 0.0)
        h += 360.0;
    return vec3(h, sat, l);
}

// Soft-edged window, so a keyed correction fades out instead of tearing along
// the selection boundary.
float windowWeight(float value, float low, float high, float feather) {
    if (value < low)
        return low - value >= feather ? 0.0 : 1.0 - (low - value) / feather;
    if (value > high)
        return value - high >= feather ? 0.0 : 1.0 - (value - high) / feather;
    return 1.0;
}

void main() {
    vec4 src = texture(source, qt_TexCoord0);
    // The letterbox around a PreserveAspectFit picture is transparent, and
    // grading it would turn the bars whatever colour a lift offset produces.
    if (src.a <= 0.0) {
        fragColor = vec4(0.0);
        return;
    }
    // Scene graph textures are premultiplied; the chain below is not.
    vec3 c = clamp(src.rgb / src.a, 0.0, 1.0);

    if (gFlags.x > 0.5)
        c = applyCube(c);
    if (gFlags.y > 0.5)
        c = vec3(textureLod(curveTex, vec2(lutU(c.r), 0.5), 0.0).a,
                 textureLod(curveTex, vec2(lutU(c.g), 0.5), 0.0).a,
                 textureLod(curveTex, vec2(lutU(c.b), 0.5), 0.0).a);
    if (abs(gBasic.y - 1.0) > 0.0001)
        c = applySaturation(gBasic.y, c);
    if (gFlags.w > 0.5) {
        float l = (max(c.r, max(c.g, c.b)) + min(c.r, min(c.g, c.b))) * 0.5;
        c = vec3(balanceComponent(c.r, l, gShadow.r, gMidtone.r, gHighlight.r),
                 balanceComponent(c.g, l, gShadow.g, gMidtone.g, gHighlight.g),
                 balanceComponent(c.b, l, gShadow.b, gMidtone.b, gHighlight.b));
    }
    if (gMix.x > 0.5)
        c = rotateHue(gBasic.z, gBasic.w, c);
    if (abs(gMix.y) > 0.0001)
        c = applyLightness(gMix.y, c);
    if (gFlags.z > 0.5)
        c = vec3(textureLod(curveTex, vec2(lutU(c.r), 0.5), 0.0).r,
                 textureLod(curveTex, vec2(lutU(c.g), 0.5), 0.0).g,
                 textureLod(curveTex, vec2(lutU(c.b), 0.5), 0.0).b);

    if (gMix.z > 0.5) {
        vec3 hsl = hueSatLuma(c);
        float mask = 1.0;
        if (gMix.w < 0.5) {
            // colorsForHue() collapses a wide range to "all colours" in the
            // export chain, so anything narrower keys on the hue distance here.
            float distance = abs(hsl.x - gHslKey.x);
            if (distance > 180.0)
                distance = 360.0 - distance;
            float t = distance / max(1.0, gHslKey.y * 0.5);
            mask = t >= 1.0 ? 0.0 : 1.0 - t * t;
        }
        if (mask > 0.0) {
            mask *= windowWeight(hsl.y, gHslKey.z, gHslKey.w, 0.08);
            mask *= windowWeight(hsl.z, gHslRange.x, gHslRange.y, 0.08);
        }
        if (mask > 0.001) {
            if (abs(gHslAdjust.x) > 0.0001) {
                float angle = gHslAdjust.x * mask * kPi / 180.0;
                c = rotateHue(cos(angle), sin(angle), c);
            }
            if (abs(gHslAdjust.y) > 0.0001)
                c = applySaturation(1.0 + gHslAdjust.y * mask, c);
            if (abs(gHslAdjust.z) > 0.0001)
                c = applyLightness(gHslAdjust.z * mask, c);
        }
    }

    fragColor = vec4(c * src.a, src.a) * qt_Opacity;
}
