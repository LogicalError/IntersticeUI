#version 450
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require
#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require
#extension GL_GOOGLE_include_directive : require

// Depth is only tested here (occluders wrote it), so test it before shading.
layout(early_fragment_tests) in;

#include "line_common.glsl"

layout(set = 0, binding = 0) uniform sampler2D allTextures[];

#include "sdf_color.glsl"

layout (location = 0) in flat SurfaceData inSurface;
layout (location = 12) in FragmentData inData;
layout (location = 16) in flat float inOpacity;

//output write
layout (location = 0) out vec4 outFragColor;

vec2 Bprime(vec2 P0, vec2 P1, vec2 P2, float t)
{
    return 2.0 * (1.0 - t) * (P1 - P0) + 2.0 * t * (P2 - P1);
}

float arcLengthEstimate(vec2 P0, vec2 P1, vec2 P2, float t)
{
    float t_half = t * 0.5;

    float len0 = length(Bprime(P0, P1, P2, 0.0));
    float len1 = length(Bprime(P0, P1, P2, t_half));
    float len2 = length(Bprime(P0, P1, P2, t));

    return (t / 6.0) * (len0 + 4.0 * len1 + len2);
}

void main()
{
    vec2 uv = inData.UV;

    vec2 P0 = inSurface.p0;
    vec2 P1 = inSurface.p1;
    vec2 P3 = inSurface.p2;
    vec2 P4 = inSurface.p3;

    float distance, t, totalLen;

    // Check if all four control points are collinear - covers both the degenerate
    // 2-point case (P1==P0, P3==P4) and any manually-placed straight-line bezier.
    vec2  v1    = P1 - P0;
    vec2  v2    = P4 - P0;
    float cross = abs(v1.x * v2.y - v1.y * v2.x);

    if (cross < 0.1)
    {
        // Straight line: exact perpendicular segment distance.
        vec2 dir = P4 - P0;
        totalLen = length(dir);
        if (totalLen < 0.001)
        {
            distance = length(uv - P0);
            t = 0.0;
        }
        else
        {
            vec2 dirN = dir / totalLen;
            float along = clamp(dot(uv - P0, dirN), 0.0, totalLen);
            distance = length((uv - P0) - dirN * along);
            t = along;
        }
    }
    else
    {
        // Curved line: split into two quadratic Bezier segments.
        vec2  P2 = (P1 + P3) * 0.5;
        float t1, t2;

        float d1 = abs(sdBezier(uv, P0, P1, P2, t1));
        float d2 = abs(sdBezier(uv, P2, P3, P4, t2));

        float totalLen1 = arcLengthEstimate(P0, P1, P2, 1.0);
        float totalLen2 = arcLengthEstimate(P2, P3, P4, 1.0);
        totalLen = totalLen1 + totalLen2;

        t = (d1 < d2) ?
                arcLengthEstimate(P0, P1, P2, t1) :
                (totalLen1 + arcLengthEstimate(P2, P3, P4, t2));
        t = clamp(t, 0.0, totalLen);

        distance = min(d1, d2);
    }

    // AA falloff convention: extend the SDF cutoff by half a pixel so a
    // 1-px line renders as one fully-opaque pixel with sub-pixel AA on
    // each side. lineWidth is in screen pixels (the SDF distance is
    // screen-space), so a 1-px line stays 1 screen px wide at any zoom.
    float lineWidth = max(0.0, inSurface.lineWidth);
    float opacity   = saturate(0.5 + (lineWidth * 0.5) - distance);

    // apply dash
    float dashSize = inSurface.dashSize;
    if (dashSize > 1)
    {
        float pixel_smooth_range = 0.5;

        float offset = t + dashSize + inSurface.dashOffset;

        float pos = mod(offset, (2 * dashSize));
        float m   = abs(pos - dashSize) - (0.5 * dashSize);
        opacity *= saturate((pixel_smooth_range + m) / (2 * pixel_smooth_range));
    }

    // Map arc-length fraction into the Y axis of the colour lookup so gradients
    // follow the curve rather than the bounding-box axes.  gradientOffset/Scale
    // let callers (e.g. RenderCircle) stitch multiple segments into one sweep.
    float lineT   = (totalLen > 0.0) ? (t / totalLen) : 0.0;
    lineT = inSurface.gradientOffset + lineT * inSurface.gradientScale;
    vec2 halfSize = inSurface.lineSize * 0.5;
    vec2 cpos     = vec2(uv.x - inSurface.lineSize.x * 0.5,
                         (lineT - 0.5) * inSurface.lineSize.y);
    float depth   = saturate(1.0 - distance / max(lineWidth * 0.5, 0.5));
    vec4 color    = getSDFColor(inSurface.colorIndex, cpos, halfSize, depth, 0, inSurface.boxSize, vec4(0));

    // make everything outside the line invisible
    color.a *= opacity;

    if (color.a <= (1.0 / 512))
        discard;

    color.a = clamp(color.a, 0, 1);

    // pre-multiply alpha
    color.rgb *= color.a;

    outFragColor = color * inOpacity;   // premultiplied: the whole color fades
}
