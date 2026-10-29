#version 450
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require
#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require
#extension GL_GOOGLE_include_directive : require

#include "dot_common.glsl"

layout (location = 0) out SurfaceData outSurface;
layout (location = 7) out FragmentData outData;

// An equilateral triangle around a circle of radius 1: its corners are twice the
// radius out, at the top and lower right and left.
const vec2 kCorners[3] = { vec2(0.0, -2.0), vec2(1.7320508, 1.0), vec2(-1.7320508, 1.0) };

void main()
{
	int dotIndex = gl_VertexIndex / 3;
	Dot dot = PushConstants.dotsBuffer.dots[dotIndex];

	// Apply the renderer's content transform to position and size, like the line shader.
	mat3x3 transform = transpose(mat3x3(dot.transform0.xyz, dot.transform1.xyz, vec3(0,0,1)));
	vec3 dotmin3 = transform * vec3(dot.pos - vec2(dot.size * 0.5), 1.0);
	vec3 dotmax3 = transform * vec3(dot.pos + vec2(dot.size * 0.5), 1.0);
	vec2 dotmin  = dotmin3.xy / dotmin3.z;
	vec2 dotmax  = dotmax3.xy / dotmax3.z;
	vec2 center  = (dotmin + dotmax) * 0.5;

	// On-screen size, so the fragment shader's AA edge matches the transformed radius.
	float dotSize = dotmax.x - dotmin.x;
	// The triangle holds the dot and its anti-aliased edge.
	float radius  = max(kMinThickness, dotSize) * 0.5 + 1.0;

	// Corners aren't clamped to the clip rect, which would bend the triangle; the
	// fragment shader clips instead.
	vec2 corner = center + kCorners[gl_VertexIndex % 3] * radius;

	outData.UV        = corner - center;
	outData.windowPos = corner;
	outSurface.dotSize = dotSize;
	outSurface.color   = dot.color;
	outSurface.clip    = vec4(dot.clipMin, dot.clipMax);

	gl_Position = PrimitivePosition(corner + 0.5, dot.transform0.w);
}
