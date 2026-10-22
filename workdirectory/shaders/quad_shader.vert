#version 450
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require
#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require
#extension GL_GOOGLE_include_directive : require

#include "quad_common.glsl"

layout (location = 0) out flat QuadSurfaceData outSurface;
layout (location = 5) out QuadFragmentData outData;

int quadIndices[6] = { 0,1,3,  0,3,2 };

void main()
{
    int quadIndex = gl_VertexIndex / 6;
    int vertexIndex = quadIndices[gl_VertexIndex % 6];

    // Quads are stored in the triangleBuffer slot (reused per render pass)
    Quad quad = Quads(PushConstants.triangleBuffer).quads[quadIndex];

    int index = vertexIndex % 4;
    vec2 uv = vec2(     index / 2,
                   1 - (index % 2));

    // Transform
    mat3x3 transform = transpose(mat3x3(quad.transform0.xyz, quad.transform1.xyz, vec3(0,0,1)));
    vec3 quadMin = vec3(quad.pos, 1);
    vec3 quadMax = quadMin + vec3(quad.size, 0);
    quadMin = transform * quadMin;
    quadMax = transform * quadMax;

    vec2 pos  = quadMin.xy / quadMin.z;
    vec2 size = (quadMax.xy / quadMax.z) - pos;

    // Clip
    vec2 clipMin = quad.clipMin;
    vec2 clipMax = quad.clipMax;
    vec2 clipped_min = clipMin - pos;
    vec2 clipped_max = clipMax - pos;
    vec2 clipped_uv  = clamp(uv * size, clipped_min, clipped_max);

    vec2 position = floor(pos + clipped_uv + 0.5) + 0.5; // snap to the pixel (half up, as boxes) + pixel-center alignment
    clipped_uv /= size;

    float scaleX = length(vec2(quad.transform0.x, quad.transform1.x));
    float scaleY = length(vec2(quad.transform0.y, quad.transform1.y));

    outData.UV = clipped_uv;
    outSurface.quadIndex = quadIndex;
    outSurface.boxSize = size;
    outSurface.textureIndex = quad.textureIndex;
    outSurface.transformScale = sqrt(scaleX * scaleY);
    outSurface.cornerRadius = quad.cornerRadius;

    gl_Position = PushConstants.worldMatrix * vec4(position, 0, 1.0);
}
