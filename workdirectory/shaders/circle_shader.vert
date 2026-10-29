#version 450
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require
#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require
#extension GL_GOOGLE_include_directive : require

#include "circle_common.glsl"

layout (location = 0) out flat SurfaceData outSurface;
layout (location = 12) out FragmentData outData;

void main()
{
    int circleIndex = gl_VertexIndex / 4;
    int vertexIndex = gl_VertexIndex % 4; // the corner, from the index buffer

    // Circles share the line slot of the push constants.
    Circle circle = Circles(PushConstants.lineBuffer).circles[circleIndex];

    int index = vertexIndex % 4;
    vec2 corner = vec2(     index / 2,   // horizontal 0 or 1
                       1 - (index % 2)); // vertical   0 or 1

    // The center and radius in screen pixels; the radius follows the transform's
    // uniform scale, the line width stays in screen pixels, as for lines.
    mat3x3 transform = transpose(mat3x3(circle.transform0.xyz, circle.transform1.xyz, vec3(0,0,1)));
    vec3  center_t = transform * vec3(circle.center, 1);
    vec2  center   = center_t.xy / center_t.z;
    float scaleX   = length(vec2(circle.transform0.x, circle.transform1.x));
    float scaleY   = length(vec2(circle.transform0.y, circle.transform1.y));
    float radius   = circle.radius * sqrt(scaleX * scaleY);

    // The square around the ring, a pixel wider for its anti-aliased edge.
    float reach = radius + max(0.0, circle.lineWidth) * 0.5 + 1.0;
    vec2  pos   = center - reach;
    vec2  size  = vec2(reach * 2.0);

    vec2 clipMin = max(vec2(circle.clipMin), pos);
    vec2 clipMax = min(vec2(circle.clipMax), pos + size);

    vec2 clipped_uv = clamp(corner * size, clipMin - pos, clipMax - pos);
    vec2 position   = (pos + clipped_uv) + 0.5; // pixel-center alignment

    outData.UV = clipped_uv;
    outSurface.colorIndex     = circle.colorIndex;
    outSurface.center         = center - pos;
    outSurface.quadSize       = size;
    outSurface.radius         = radius;
    outSurface.lineWidth      = circle.lineWidth;
    outSurface.dashSize       = circle.dashSize;
    outSurface.dashOffset     = circle.dashOffset;
    outSurface.gradientOffset = circle.gradientOffset;
    outSurface.gradientScale  = circle.gradientScale;

    gl_Position = PrimitivePosition(position, circle.transform0.w);
}
