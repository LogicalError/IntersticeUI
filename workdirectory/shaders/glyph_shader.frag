#version 450
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require
#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require
#extension GL_GOOGLE_include_directive : require

#include "glyph_common.glsl"

layout (location = 0) in flat SurfaceData inSurface;
layout (location = 7) in FragmentData inData;

layout(set = 0, binding = 0) uniform sampler2D allTextures[];

//output write
layout (location = 0) out vec4 outFragColor;

#include "sdf_color.glsl"

void main()
{
    vec2 defaultTextureSize = textureSize(allTextures[nonuniformEXT(inSurface.textureIndex)], 0);
    vec2 uv = (inData.UV / defaultTextureSize.xy);

    uint bpp = inSurface.bpp;
    uint mode = inSurface.mode;

    vec4 glyphSample;
    if (mode == 0)
    {
        if (bpp == 4)
        {
            glyphSample = texture(allTextures[nonuniformEXT(inSurface.textureIndex)], uv.xy);
        } else
        {
            glyphSample = vec4(1, 1, 1, texture(allTextures[nonuniformEXT(inSurface.textureIndex)], uv.xy).r);
        }
        glyphSample = srgbToLinear(glyphSample);
    } else
    {
        vec4 _sample;
        if (bpp == 4)
        {
            _sample = texture(allTextures[nonuniformEXT(inSurface.textureIndex)], uv.xy);
        } else
        {
            _sample = vec4(1, 1, 1, texture(allTextures[nonuniformEXT(inSurface.textureIndex)], uv.xy).r);
        }

        _sample = srgbToLinear(_sample);

        float w = inSurface.scale * 0.8;
	    float d = (_sample.a - (128./255.)) / (32./255.);
	    float a = 1.f - clamp((d + 0.5 * w - 0.1) / w, 0.0, 1.0);

        glyphSample = vec4(_sample.rgb, a);
    }

    vec4 color;
    if (bpp == 4)
    {
        // Colored glyph (emoji): use the glyph's own RGBA colors unchanged.
        color = glyphSample;
    }
    else
    {
        // SDF / grayscale glyph: tint with the ColorStyle text color.
        // glyphSample is white * alpha mask; multiply by resolved text color.
        vec4 textColor = getSDFColor(inSurface.textColorIndex, vec2(0), vec2(1), 0.0, 0, vec2(1), vec4(0));
        color = glyphSample * srgbToLinear(textColor);
    }

    if (color.a <= (1.0 / 512))
        discard;

    // convert color back to srgb
    color.rgb = linearToSrgb(color.rgb);
    color.a = clamp(color.a, 0, 1);

    // pre-multiply alpha
    color.rgb *= color.a;

    outFragColor = color;
}