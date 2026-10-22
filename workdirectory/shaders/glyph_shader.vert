#version 450
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require
#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require
#extension GL_GOOGLE_include_directive : require

#include "glyph_common.glsl"

layout (location = 0) out SurfaceData outSurface;
layout (location = 7) out FragmentData outData;


int boxIndices[6] = { 0,1,3,  0,3,2 };

void main() 
{
	int glyphIndex = gl_VertexIndex / 6;
	int vertexIndex = boxIndices[gl_VertexIndex % 6];
		
	Glyph glyph = PushConstants.glyphBuffer.glyphs[glyphIndex];

	// use index offsets to calculate box vertices
	int index = vertexIndex % 4;
	vec2 offset = vec2(     index / 2,   // horizontal 0 or 1
				       1 - (index % 2)); // vertical   0 or 1
	
	// transformed box
	mat3x3 transform = transpose(mat3x3(glyph.transform0.xyz, glyph.transform1.xyz, vec3(0,0,1))); 
	vec3 glyphMin = vec3(glyph.pos, 1);
	vec3 glyphMax = glyphMin + vec3(glyph.size, 0);
	glyphMin = transform * glyphMin;
	glyphMax = transform * glyphMax;


	vec2 pos  = (glyphMin.xy / glyphMin.z);
	vec2 size = (glyphMax.xy / glyphMax.z) - pos;


	vec2 clipMin = glyph.clipMin;
	vec2 clipMax = glyph.clipMax;

	clipMin.x = max(clipMin.x, clipMin.x);
	clipMin.y = max(clipMin.y, clipMin.y);
	clipMax.x = min(clipMax.x, clipMax.x);
	clipMax.y = min(clipMax.y, clipMax.y);


	// clip the position in (0,1) space
    vec2 clipped_min = clipMin - pos;
	vec2 clipped_max = clipMax - pos;
	vec2 clipped_offset = clamp(offset * size, clipped_min, clipped_max);
	

	// The +0.5 on position pairs with the +0.5 texel on the UV below so each pixel
	// centre samples a texel centre; drop either and bilinear filtering blurs.
	// Pixel snapping happens at the origin upstream, not per glyph, which would
	// jitter the shared baseline.
	vec2 position = (pos + clipped_offset) + 0.5;
	vec2 atlasPerScreen = abs(glyph.uvMax - glyph.uvMin) / max(size, vec2(1.0));
	vec2 uv       = mix(glyph.uvMin, glyph.uvMax, clipped_offset / size) + 0.5 * atlasPerScreen;
	

	outData.UV = uv;
	outSurface.textureIndex = max(0,glyph.textureIndex - 1);
	outSurface.scale = glyph.scale;
	outSurface.bpp = glyph.glyph_type.x;
	outSurface.mode = glyph.glyph_type.y;
	outSurface.textColorIndex = int(glyph.textColorIndex);

	gl_Position = PushConstants.worldMatrix * vec4(position, 0, 1.0f);
}
