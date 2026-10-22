#version 450
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require
#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require
#extension GL_GOOGLE_include_directive : require

#include "triangle_common.glsl"

layout (location = 0) in flat TriSurfaceData inSurface;
layout (location = 4) in TriFragmentData inData;

layout(set = 0, binding = 0) uniform sampler2D allTextures[];
layout (location = 0) out vec4 outFragColor;

float distanceToSegment(vec2 p, vec2 a, vec2 b)
{
    vec2 pa = p - a;
    vec2 ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h);
}

#include "sdf_color.glsl"

void main()
{
    Triangle triangle = PushConstants.triangleBuffer.triangles[inSurface.triangleIndex];

    vec2 p0 = vec2(triangle.p0);
    vec2 p1 = vec2(triangle.p1);
    vec2 p2 = vec2(triangle.p2);

    // Match the vertex shader: the interior test runs in transformed (screen)
    // space, since inData.position is the transformed quad position.
    mat3x3 transform = transpose(mat3x3(triangle.transform0.xyz, triangle.transform1.xyz, vec3(0,0,1)));
    vec3 p0_t = transform * vec3(p0, 1);
    vec3 p1_t = transform * vec3(p1, 1);
    vec3 p2_t = transform * vec3(p2, 1);
    p0 = p0_t.xy / p0_t.z;
    p1 = p1_t.xy / p1_t.z;
    p2 = p2_t.xy / p2_t.z;
    vec2 p = inData.position;

    float area = cross2(p1 - p0, p2 - p0);
    if (abs(area) < 0.0001)
        discard;

    float w0 = cross2(p1 - p0, p - p0) / area;
    float w1 = cross2(p2 - p1, p - p1) / area;
    float w2 = cross2(p0 - p2, p - p2) / area;

    bool inside = (w0 >= 0.0 && w1 >= 0.0 && w2 >= 0.0) ||
                  (w0 <= 0.0 && w1 <= 0.0 && w2 <= 0.0);

    float edgeDistance = min(distanceToSegment(p, p0, p1),
                         min(distanceToSegment(p, p1, p2),
                             distanceToSegment(p, p2, p0)));

    float signedDistance = inside ? -edgeDistance : edgeDistance;
    float opacity = saturate(1.0 - signedDistance);
    if (opacity <= (1.0 / 512.0))
        discard;

    vec2 triHalfSize = max(inSurface.triSize * 0.5, vec2(1.0, 1.0));
    vec2 localPos = (p - inSurface.triMin) - triHalfSize;


    vec2 boxSize = max(max(p0,p1),p2) - min(min(p0, p1), p2);

    vec4 weights = vec4(w0, w1, w2, (w0+w1+w2)/3);

    vec4 color = getSDFColor(int(triangle.colorIndex), localPos, triHalfSize, signedDistance, inSurface.textureIndex, boxSize, weights);

    color.a *= opacity;
    if (color.a <= (1.0 / 512.0))
        discard;
        
    color.a = clamp(color.a, 0, 1);

    // pre-multiply alpha
    color.rgb *= color.a;

    outFragColor = color;
}
