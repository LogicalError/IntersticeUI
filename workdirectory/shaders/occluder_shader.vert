#version 450
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require
#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require
#extension GL_GOOGLE_include_directive : require

#include "all_common.glsl"

layout(buffer_reference, std430)
struct Occluder
{
    vec4       transform0;
    vec4       transform1;
    ivec2      clipMin;
    ivec2      clipMax;

    ivec2      pos;
    ivec2      size;
};

layout(buffer_reference, std430) readonly buffer Occluders { Occluder occluders[]; };

void main()
{
    int occluderIndex = gl_VertexIndex / 4;
    int vertexIndex   = gl_VertexIndex % 4; // the corner, from the index buffer

    // Occluders share the box slot of the push constants.
    Occluder occluder = Occluders(PushConstants.boxBuffer).occluders[occluderIndex];

    vec2 corner = vec2(     vertexIndex / 2,   // horizontal 0 or 1
                       1 - (vertexIndex % 2)); // vertical   0 or 1

    // The rect under the primitive's transform, kept inside its clip rect. Only a
    // pixel's center inside the rect is covered, so its edge pixels aren't.
    mat3x3 transform = transpose(mat3x3(occluder.transform0.xyz, occluder.transform1.xyz, vec3(0,0,1)));
    vec3 rectMin = transform * vec3(vec2(occluder.pos), 1);
    vec3 rectMax = transform * vec3(vec2(occluder.pos + occluder.size), 1);
    vec2 lo = min(rectMin.xy / rectMin.z, rectMax.xy / rectMax.z);
    vec2 hi = max(rectMin.xy / rectMin.z, rectMax.xy / rectMax.z);
    lo = max(lo, vec2(occluder.clipMin));
    hi = max(min(hi, vec2(occluder.clipMax)), lo);

    gl_Position = PrimitivePosition(mix(lo, hi, corner), occluder.transform0.w);
}
