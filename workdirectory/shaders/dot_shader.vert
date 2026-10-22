#version 450
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require
#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require
#extension GL_GOOGLE_include_directive : require

#include "dot_common.glsl"

layout (location = 0) out SurfaceData outSurface;
layout (location = 7) out FragmentData outData;


int dotIndices[6] = { 0,1,3,  0,3,2 };

void main()
{
	int dotIndex = gl_VertexIndex / 6;
	int vertexIndex = dotIndices[gl_VertexIndex % 6];

    Dot dot = PushConstants.dotsBuffer.dots[dotIndex];

	// use index offsets to calculate dot vertices
	int index = vertexIndex % 4;
	vec2 uv = vec2(     index / 2,   // horizontal 0 or 1
				   1 - (index % 2)); // vertical   0 or 1

	// Apply the renderer's content transform to position and size, like the line shader.
	mat3x3 transform = transpose(mat3x3(dot.transform0.xyz, dot.transform1.xyz, vec3(0,0,1)));

	vec3 dotmin3 = transform * vec3(dot.pos - vec2(dot.size * 0.5), 1.0);
	vec3 dotmax3 = transform * vec3(dot.pos + vec2(dot.size * 0.5), 1.0);
	vec2 dotmin = dotmin3.xy / dotmin3.z;
	vec2 dotmax = dotmax3.xy / dotmax3.z;

	vec2 pos  = dotmin;
	vec2 size = dotmax - pos;

	vec2 clipMin = dot.clipMin;
	vec2 clipMax = dot.clipMax;

	clipMin.x = max(clipMin.x, dotmin.x);
	clipMin.y = max(clipMin.y, dotmin.y);
	clipMax.x = min(clipMax.x, dotmax.x);
	clipMax.y = min(clipMax.y, dotmax.y);

	vec2 clipped_min = dot.clipMin - pos;
	vec2 clipped_max = dot.clipMax - pos;
	vec2 clipped_uv  = clamp(uv * size, clipped_min, clipped_max);

	vec2 position = (pos + clipped_uv) + 0.5;
	clipped_uv -= (size * 0.5);

	outData.UV = clipped_uv;
	// On-screen size, so the fragment shader's AA edge matches the transformed radius.
	outSurface.dotSize = (dotmax.x - dotmin.x);
	outSurface.color = dot.color;

	gl_Position = PushConstants.worldMatrix * vec4(position, 0, 1.0f);
}
