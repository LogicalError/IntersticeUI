#version 450
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require
#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require
#extension GL_GOOGLE_include_directive : require

#include "line_common.glsl"

layout (location = 0) out flat SurfaceData outSurface;
layout (location = 12) out FragmentData outData;
layout (location = 16) out flat float outOpacity;



void main() 
{
	int lineIndex = gl_VertexIndex / 4;
	int vertexIndex = gl_VertexIndex % 4; // the corner, from the index buffer
					   
    Line line = PushConstants.lineBuffer.lines[lineIndex];

	// use index offsets to calculate line vertices
	int index = vertexIndex % 4;
	vec2 uv = vec2(     index / 2,   // horizontal 0 or 1
				   1 - (index % 2)); // vertical   0 or 1
				   
    float outerLineWidth = max(0, line.lineWidth);

	float scaleX = length(vec2(line.transform0.x, line.transform1.x));
	float scaleY = length(vec2(line.transform0.y, line.transform1.y));
	float scale  = sqrt(scaleX * scaleY);

	// Transform all four control points to screen space, then AABB.
	// AABB-after-transform is the correct screen bounding box under any affine
	// transform; transform-after-AABB would tilt for non-axis-aligned cases.
	mat3x3 transform = transpose(mat3x3(line.transform0.xyz, line.transform1.xyz, vec3(0,0,1)));
	vec3 p0_t = transform * vec3(line.p0, 1);
	vec3 p1_t = transform * vec3(line.p1, 1);
	vec3 p2_t = transform * vec3(line.p2, 1);
	vec3 p3_t = transform * vec3(line.p3, 1);
	vec2 p0s = p0_t.xy / p0_t.z;
	vec2 p1s = p1_t.xy / p1_t.z;
	vec2 p2s = p2_t.xy / p2_t.z;
	vec2 p3s = p3_t.xy / p3_t.z;

	// Snap axis-aligned (horizontal/vertical) lines to the perpendicular
	// pixel boundary. Half-pixel offset for odd effective widths (1, 3, 5 px)
	// centers the stroke on a pixel; integer for even widths puts the edges
	// on pixel boundaries. Bezier curves render unsnapped at sub-pixel.
	// Axis-aligned only when all four control points share the coordinate (2-point
	// lines arrive as p0,p0,p1,p1); symmetric curves can match on endpoints and
	// midpoints alone and must not be flattened.
	bool axisX = (line.p0.y == line.p1.y) && (line.p1.y == line.p2.y) && (line.p2.y == line.p3.y);
	bool axisY = (line.p0.x == line.p1.x) && (line.p1.x == line.p2.x) && (line.p2.x == line.p3.x);
	if (axisX || axisY)
	{
		float effW       = max(line.lineWidth * scale, 1.0);
		float snapOffset = (mod(floor(effW), 2.0) == 1.0) ? 0.5 : 0.0;
		if (axisX) {
			float sy = floor(p0s.y) + snapOffset;
			p0s.y = sy; p1s.y = sy; p2s.y = sy; p3s.y = sy;
		} else {
			float sx = floor(p0s.x) + snapOffset;
			p0s.x = sx; p1s.x = sx; p2s.x = sx; p3s.x = sx;
		}
	}

	vec2 lineMinS = min(min(p0s, p1s), min(p2s, p3s));
	vec2 lineMaxS = max(max(p0s, p1s), max(p2s, p3s));

	vec2 pos  = lineMinS - (outerLineWidth * 0.5);
	vec2 size = (lineMaxS - pos) + outerLineWidth;

	vec2 clipMin = line.clipMin;
	vec2 clipMax = line.clipMax;

	clipMin.x = max(clipMin.x, pos.x);
	clipMin.y = max(clipMin.y, pos.y);
	clipMax.x = min(clipMax.x, pos.x + size.x);
	clipMax.y = min(clipMax.y, pos.y + size.y);


	// clip the position in (0,1) space
	vec2 clipped_min = clipMin - pos;
	vec2 clipped_max = clipMax - pos;
	vec2 clipped_uv  = clamp(uv * size, clipped_min, clipped_max);

	vec2 position = (pos + clipped_uv) + 0.5; // pixel-center alignment


	outData.UV = clipped_uv;
	outSurface.lineSize = size;
	// Endpoints in quad-local screen space - same space as inData.UV - so the
	// fragment shader's curve/line distance math stays consistent.
	outSurface.p0 = p0s - pos;
	outSurface.p1 = p1s - pos;
	outSurface.p2 = p2s - pos;
	outSurface.p3 = p3s - pos;
	outSurface.boxSize = size;
	outSurface.dashSize = line.dashSize;

	outSurface.lineWidth      = line.lineWidth;
	outSurface.dashOffset     = line.dashOffset;
	outSurface.colorIndex     = line.colorIndex;
	outSurface.gradientOffset = line.gradientOffset;
	outSurface.gradientScale  = line.gradientScale;

	outData.UV = clipped_uv;

	gl_Position = PrimitivePosition(position, line.transform0.w);
	outOpacity  = PrimitiveOpacity(line.transform1);
}
