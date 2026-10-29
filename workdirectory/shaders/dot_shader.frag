#version 450
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require
#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require
#extension GL_GOOGLE_include_directive : require

// Depth is only tested here (occluders wrote it), so test it before shading.
layout(early_fragment_tests) in;

#include "dot_common.glsl"

layout (location = 0) in flat SurfaceData inSurface;
layout (location = 7) in FragmentData inData;

//output write
layout (location = 0) out vec4 outFragColor;



void main() 
{
    // Clipped here, not by moving the triangle's corners; in window pixels before the world
    // matrix, as the clip rect is.
    vec2 pixel = floor(inData.windowPos + 0.5);
    if (any(lessThan(pixel, inSurface.clip.xy)) || any(greaterThanEqual(pixel, inSurface.clip.zw)))
        discard;

    vec2 uv = inData.UV;
    
    // sdf distance for dot    
    float distance = length(uv);
    float dotSize = inSurface.dotSize;
    float thickness = max(kMinThickness, dotSize);
    float opacity  = clamp((thickness * 0.5) - distance, 0, 1);
    
    float thickness_leftover = clamp(thickness - dotSize, 0, kMinThickness);
    float thickness_alpha    = 1 - (thickness_leftover / kMinThickness);

    opacity *= thickness_alpha;
    
    vec4 color = inSurface.color;
    
    // make everything outside the dot invisible
    color.a *= opacity;

    if (opacity <= (1.0 / 512))
        discard;
            
    color.a = clamp(color.a,0,1);

    // pre-multiply alpha
    color.rgb *= color.a;

    outFragColor = color;
}