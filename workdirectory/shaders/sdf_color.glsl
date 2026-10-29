// sdf_color.glsl
// Shared color resolution for SDF-based rendering.
// Requires: allTextures[], PushConstants.colorStyleBuffer (from the including shader).

vec4 getGradientColor(sampler2D tex, vec2 texSize, float offset, float gradientIndex)
{
    offset = clamp(offset * texSize.x, 0, texSize.x - 1) / texSize.x;
    return texture(tex, vec2(offset, gradientIndex));
}

vec4 getSolidColor(sampler2D tex, vec2 colorPos)
{
    return texture(tex, colorPos);
}

float cross2(vec2 a, vec2 b)
{ 
    return a.x * b.y - a.y * b.x;
}

// Resolve a ColorStyle index to a vec4 color.
// colorStyleIndex: 1-based index (0 = invalid/transparent).
// pos, size: position and half-size in local space (for gradient mapping).
// edgeDepth: how deep the point is inside its shape, 0 on the edge and 1 at the deepest
//   point (the middle of a box, a ring's stroke or a triangle's incircle); SDFGradient uses it.
// textureIndex: override texture index (1-based), or 0 to use ColorStyle's own texture.
// boxSize: original box pixel size (for RepeatedTexture UV).
vec4 getSDFColor(in int colorStyleIndex, vec2 pos, vec2 size, float edgeDepth, int textureIndex, vec2 boxSize, vec4 weights)
{
    if (colorStyleIndex < 1)
        return vec4(0); // default to transparent if no color style is set

	ColorStyle colorStyle = PushConstants.colorStyleBuffer.colorStyles[colorStyleIndex - 1];

    textureIndex = textureIndex >= 1 ? max(0, textureIndex - 1) : max(0, colorStyle.textureIndex - 1);

    mat3x3 uv_transformation = transpose(mat3x3(
        vec3(colorStyle.transformationX.x, colorStyle.transformationX.y, colorStyle.transformationX.z),
        vec3(colorStyle.transformationY.x, colorStyle.transformationY.y, colorStyle.transformationY.z),
        vec3(0,0,1)));
    u16vec2 colorPixelPos = colorStyle.colorUV_type.xy;
    uint colorType = colorStyle.colorUV_type.z; // type

    // colorUV_type.w holds the 0-based physical bindless slot of the gradient-atlas
    // page; every atlas fetch for this style uses it.
    uint gradientPageIdx = colorStyle.colorUV_type.w;

    vec2 defaultTextureSize = textureSize(allTextures[nonuniformEXT(gradientPageIdx)], 0);
    vec2 colorPos = vec2(colorPixelPos) / defaultTextureSize;

    vec4 color;

    vec2 animOffset = vec2(colorStyle.animSpeed) * PushConstants.elapsedTime;

    if (colorType == 1) // StretchedTexture
    {
        vec2 normalizedUV = fract((pos / (size * 2)) + 0.5 + animOffset);
        color = texture(allTextures[nonuniformEXT(textureIndex)], normalizedUV.xy);
    }

    else if (colorType == 2) // RepeatedTexture
    {
        vec2 displayTextureSize = textureSize(allTextures[nonuniformEXT(textureIndex)], 0);
        vec2 uv = ((pos / size) + 1.0) * 0.5 * boxSize;
        vec2 repeatedUV = (uv / displayTextureSize) + animOffset;
        color = texture(allTextures[nonuniformEXT(textureIndex)], repeatedUV.xy);
    }

    else if (colorType == 3) // LinearGradient
    {
        const vec2 gradient_vector = vec2(0.0,1.0);
        vec2 normalizedUV = (pos / (size * 2)) + 0.5;
        vec3 transformedUV = uv_transformation * vec3(normalizedUV,1);
        normalizedUV = transformedUV.xy / transformedUV.z;
        float gradient = min(abs(dot(gradient_vector, normalizedUV)), 1 - (1.0/defaultTextureSize.x));
        float animGradient = clamp(fract(gradient + animOffset.x),0,1);
        color = texture(allTextures[nonuniformEXT(gradientPageIdx)], vec2(animGradient, colorPos.y));
    }

    else if (colorType == 4) // CircularAngularGradient
    {
        vec3 transformedUV = uv_transformation * vec3(pos,1);
        pos = transformedUV.xy / transformedUV.z;
        float gradient = min(length(pos), 1 - (1.0/defaultTextureSize.x));
        float animGradient = fract(gradient + animOffset.x);
        color = texture(allTextures[nonuniformEXT(gradientPageIdx)], vec2(animGradient, colorPos.y));
    }

    else if (colorType == 5) // EllipsoidAngularGradient
    {
        vec2 normalizedUV = pos / size;
        vec3 transformedUV = uv_transformation * vec3(normalizedUV,1);
        normalizedUV = transformedUV.xy / transformedUV.z;
        float gradient = min(length(normalizedUV), 1 - (1.0/defaultTextureSize.x));
        float animGradient = fract(gradient + animOffset.x);
        color = texture(allTextures[nonuniformEXT(gradientPageIdx)], vec2(animGradient, colorPos.y));
    }

    else if (colorType == 6) // RadialGradient
    {
        vec3 transformedUV = uv_transformation * vec3(pos,1);
        pos = transformedUV.xy / transformedUV.z;
        vec2 from = normalize(pos);
        const vec2 to = vec2(0,1);

	    float angle = acos(clamp(dot(from, to), -1.0, 1.0)) / (2 * 3.1415926535897932384626433832795);
        float sign = (from.x * to.y) - (from.y * to.x);
	    float signed_angle = mod((((sign < 0) ? -1 : 1) * angle) + 1, 1);

        float gradient = min(abs(signed_angle), 1 - (1.0/defaultTextureSize.x));
        float animGradient = fract(gradient + animOffset.x);
        color = texture(allTextures[nonuniformEXT(gradientPageIdx)], vec2(animGradient, colorPos.y));
    }

    else if (colorType == 7) // InterpolatedTriangle
    {
        vec4 c0 = texelFetch(allTextures[nonuniformEXT(gradientPageIdx)], ivec2(colorPixelPos.x + 0, colorPixelPos.y), 0);
        vec4 c1 = texelFetch(allTextures[nonuniformEXT(gradientPageIdx)], ivec2(colorPixelPos.x + 1, colorPixelPos.y), 0);
        vec4 c2 = texelFetch(allTextures[nonuniformEXT(gradientPageIdx)], ivec2(colorPixelPos.x + 2, colorPixelPos.y), 0);

        color = (c0 * weights.x) +
                (c1 * weights.y) +
                (c2 * weights.z);
    }

    else if (colorType == 8) // InterpolatedBox
    {
        vec4 c00 = texelFetch(allTextures[nonuniformEXT(gradientPageIdx)], ivec2(colorPixelPos.x + 0, colorPixelPos.y), 0); // top-left
        vec4 c01 = texelFetch(allTextures[nonuniformEXT(gradientPageIdx)], ivec2(colorPixelPos.x + 1, colorPixelPos.y), 0); // top-right
        vec4 c11 = texelFetch(allTextures[nonuniformEXT(gradientPageIdx)], ivec2(colorPixelPos.x + 2, colorPixelPos.y), 0); // bottom-left
        vec4 c10 = texelFetch(allTextures[nonuniformEXT(gradientPageIdx)], ivec2(colorPixelPos.x + 3, colorPixelPos.y), 0); // bottom-right

        color = (c00 * weights.x) +
                (c01 * weights.y) +
                (c11 * weights.z) +
                (c10 * weights.w);
    }

    else if (colorType == 9) // SDFGradient: the ramp by edge depth, mapped by the first row
    {
        float t = (uv_transformation * vec3(edgeDepth, 0, 1)).x;
        color = getGradientColor(allTextures[nonuniformEXT(gradientPageIdx)], defaultTextureSize, t, colorPos.y);
    }

    else // SolidColor (default)
    {
        vec4 solidColor = getSolidColor(allTextures[nonuniformEXT(gradientPageIdx)], colorPos);
        color = solidColor;
    }
    return color;
}
