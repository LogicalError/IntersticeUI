#version 450
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require
#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require
#extension GL_GOOGLE_include_directive : require

// Depth is only tested here (occluders wrote it), so test it before shading.
layout(early_fragment_tests) in;

#include "circle_common.glsl"

layout(set = 0, binding = 0) uniform sampler2D allTextures[];

#include "sdf_color.glsl"

layout (location = 0) in flat SurfaceData inSurface;
layout (location = 12) in FragmentData inData;
layout (location = 16) in flat float inOpacity;

layout (location = 0) out vec4 outFragColor;

const float kTau = 6.28318530718;

void main()
{
    vec2  fromCenter = inData.UV - inSurface.center;
    float distance   = abs(length(fromCenter) - inSurface.radius);

    // As lines: the edge half a pixel past half the width, so a 1-px ring is one opaque
    // pixel with sub-pixel AA on each side.
    float lineWidth = max(0.0, inSurface.lineWidth);
    float opacity   = saturate(0.5 + (lineWidth * 0.5) - distance);

    // How far round, clockwise from the right (+x towards +y): 0 to 1.
    float around = atan(fromCenter.y, fromCenter.x) / kTau;
    if (around < 0.0)
        around += 1.0;

    float dashSize = inSurface.dashSize;
    if (dashSize > 1)
    {
        float pixel_smooth_range = 0.5;
        float along  = around * kTau * inSurface.radius;
        float offset = along + dashSize + inSurface.dashOffset;
        float pos    = mod(offset, (2 * dashSize));
        float m      = abs(pos - dashSize) - (0.5 * dashSize);
        opacity *= saturate((pixel_smooth_range + m) / (2 * pixel_smooth_range));
    }

    // The gradient runs once around, mapped the way lines map it along their length.
    float lineT    = inSurface.gradientOffset + around * inSurface.gradientScale;
    vec2  size     = inSurface.quadSize;
    vec2  cpos     = vec2(inData.UV.x - size.x * 0.5, (lineT - 0.5) * size.y);
    float depth    = saturate(1.0 - distance / max(lineWidth * 0.5, 0.5));
    vec4  color    = getSDFColor(inSurface.colorIndex, cpos, size * 0.5, depth, 0, size, vec4(0));

    color.a *= opacity;
    if (color.a <= (1.0 / 512))
        discard;

    color.a = clamp(color.a, 0, 1);
    color.rgb *= color.a;
    outFragColor = color * inOpacity;   // premultiplied: the whole color fades
}
