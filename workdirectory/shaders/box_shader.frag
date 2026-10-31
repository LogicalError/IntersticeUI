#version 450
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require
#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require
#extension GL_GOOGLE_include_directive : require

// Depth is only tested here (occluders wrote it), so test it before shading.
layout(early_fragment_tests) in;

#include "box_common.glsl"

layout (location = 0) in flat SurfaceData inSurface;
layout (location = 9) in FragmentData inData;
layout (location = 16) in flat float inOpacity;

layout(set = 0, binding = 0) uniform sampler2D allTextures[];

//output write
layout (location = 0) out vec4 outFragColor;




float sdf_corner(vec2 p, vec2 b, float radius)
{
    vec2 q = (p - b) + radius;
    return length(max(q,0.0)) + min(max(q.x, q.y),0.0) - radius;
}

float sdf_corner_inner(vec2 p, vec2 b, float radius, float vert_width, float horz_width)
{
    float maxOffset = max(vert_width, horz_width);
    vec2 offset = vec2(max(0, maxOffset - horz_width), max(0, maxOffset - vert_width));
    return sdf_corner(p - offset, b, radius) + maxOffset;
}


float sdf_rounded_inner(vec2 p, vec2 b, vec4 cornerRadius, vec4 borderWidth)
{
    float tl = cornerRadius.x; // top-left corner radius
    float tr = cornerRadius.y; // top-right corner radius
    float br = cornerRadius.z; // bottom-right corner radius
    float bl = cornerRadius.w; // bottom-left corner radius
    
    float topBorderWidth    = borderWidth.x;
    float rightBorderWidth  = borderWidth.y;
    float bottomBorderWidth = borderWidth.z;
    float leftBorderWidth   = borderWidth.w;

    return max(
                max(
                    sdf_corner_inner( vec2(-p.x,  p.y), b, tl, topBorderWidth, leftBorderWidth),
                    sdf_corner_inner( vec2( p.x,  p.y), b, tr, topBorderWidth, rightBorderWidth)
                    ),
                max(
                    sdf_corner_inner( vec2(-p.x, -p.y), b, bl, bottomBorderWidth, leftBorderWidth),
                    sdf_corner_inner( vec2( p.x, -p.y), b, br, bottomBorderWidth, rightBorderWidth)
                ));
}

float sdf_rounded(vec2 p, vec2 b, vec4 cornerRadius)
{
    float tl = cornerRadius.x; // top-left corner radius
    float tr = cornerRadius.y; // top-right corner radius
    float br = cornerRadius.z; // bottom-right corner radius
    float bl = cornerRadius.w; // bottom-left corner radius
    return max(
                max(
                    sdf_corner( vec2( p.x,  p.y), b, tr),
                    sdf_corner( vec2(-p.x,  p.y), b, tl)
                    ),
                max(
                    sdf_corner( vec2( p.x, -p.y), b, br),
                    sdf_corner( vec2(-p.x, -p.y), b, bl)
                ));
}


float sdfSegment(vec2 p, vec2 a, vec2 b)
{
    float h = min(1.0, max(0.0, dot(p - a, b - a) / dot(b - a, b - a)));
    //dot(b - a, b - a) == b-a * b-a
    return length(p - (a + h * (b - a))); // return distance from point to line
}

#include "sdf_color.glsl"

struct CurrentStyle
{
    vec4 borderColor;
    vec4 backgroundColor;

    vec4 cornerRadius;
    vec4 borderWidth;

    float dashSize;
    float dashOffset;
};

void AddPreStyle(inout CurrentStyle currentStyle, float weight, int styleIndex)
{
    if (styleIndex < 0) return;
	BoxStyle boxStyle = PushConstants.boxStyleBuffer.boxStyles[styleIndex];

    currentStyle.cornerRadius += weight * boxStyle.cornerRadius;
    currentStyle.borderWidth  += weight * boxStyle.borderWidth;
    currentStyle.dashSize     += weight * float(boxStyle.borderDashSize);
    currentStyle.dashOffset   += weight * float(boxStyle.borderDashOffset);
}

void AddPostStyle(inout CurrentStyle currentStyle, vec2 uv, vec2 sdfUv, vec2 uvsize, float edgeDepth, float weight, int styleIndex, vec4 weights)
{
    if (styleIndex < 0) return;
	BoxStyle boxStyle = PushConstants.boxStyleBuffer.boxStyles[styleIndex];

    // Select border color based on which edge the fragment is closest to.
    // Regions are divided by diagonals from the center (same convention as CSS):
    //   uv.y > 0 = top, uv.y < 0 = bottom, uv.x > 0 = right, uv.x < 0 = left.
    int16_t edgeColorIndex;
    vec2 side = sdfUv / uvsize; // -1..1 from the center, y up
    if (abs(side.y) >= abs(side.x))
        edgeColorIndex = (side.y >= 0.0) ? boxStyle.borderColorIndex.x   // top
                                         : boxStyle.borderColorIndex.z;  // bottom
    else
        edgeColorIndex = (side.x > 0.0)  ? boxStyle.borderColorIndex.y   // right
                                         : boxStyle.borderColorIndex.w;  // left

    // Premultiplied accumulation so a transparent endpoint adds no RGB (a fade from
    // transparent doesn't pass through its black RGB). Divided back out after the loop.
    vec4 borderC = getSDFColor(int(edgeColorIndex), uv, uvsize, edgeDepth, inSurface.textureIndex, inSurface.boxSize, weights);
    currentStyle.borderColor.rgb += weight * borderC.a * borderC.rgb;
    currentStyle.borderColor.a   += weight * borderC.a;

    vec4 bgC = getSDFColor(int(boxStyle.backgroundColorIndex), uv, uvsize, edgeDepth, inSurface.textureIndex, inSurface.boxSize, weights);
    currentStyle.backgroundColor.rgb += weight * bgC.a * bgC.rgb;
    currentStyle.backgroundColor.a   += weight * bgC.a;
}

void main() 
{
    float pixelrange = 1.0 / min(inSurface.boxSize.x, inSurface.boxSize.y);
    float half_pixelrange = pixelrange * 0.5;
    vec2 uvsize = inSurface.boxSize * half_pixelrange;
    vec2 uv     = (((inData.UV - 0.5) * 2) * uvsize) + half_pixelrange; // y down, as on screen
    vec2 sdfUv  = vec2(uv.x, -uv.y); // the SDF helpers use y up (top = +y)

    float transformed_pixelrange = inSurface.transformScale * pixelrange;
    
    CurrentStyle currentStyle;
    currentStyle.borderColor     = vec4(0);
    currentStyle.backgroundColor = vec4(0);
    currentStyle.cornerRadius    = vec4(0);
    currentStyle.borderWidth     = vec4(0);
    currentStyle.dashSize        = 0.0;
    currentStyle.dashOffset      = 0.0;
    
    // Blend geometry (cornerRadius, borderWidth) across "from" and "to" states.
    // When not transitioning: blendFactors[0]==0 and blendFactors[1]==1, so only slot 1 runs.
    for (int i = 0; i < 2; i++)
        if (inSurface.blendFactors[i] > 0.0)
            AddPreStyle(currentStyle, inSurface.blendFactors[i], inSurface.styleIndex[i]);
    

    // Corner radii scale with the transform. Border widths are whole pixels, the
    // same everywhere: children snap relative to their parent's snapped edges by
    // the same rounding, so content against a border meets its inner edge.
    currentStyle.cornerRadius *= transformed_pixelrange;
    {
        vec4 bw = currentStyle.borderWidth; // top, right, bottom, left in UI units
        vec2 s  = inSurface.axisScale;
        vec4 px = floor(bw * vec4(s.y, s.x, s.y, s.x) + 0.5);
        px = mix(vec4(0.0), max(px, vec4(1.0)), greaterThan(bw, vec4(0.0)));
        currentStyle.borderWidth = px * pixelrange;
    }


    // sdf distance for rounded box
    
    float outer_distance, inner_distance, outer_opacity; 
    
    outer_distance = sdf_rounded(sdfUv, uvsize, currentStyle.cornerRadius);

	outer_opacity = clamp(1 - (outer_distance / pixelrange), 0, 1);
    if (outer_opacity <= (1.0 / 512))
        discard;

    // Shift the inner-SDF edge in by half a pixel only for edges with a border, so a
    // 1-pixel border is one solid pixel. Zero-width edges must not shift, or they show
    // a phantom half-opacity border.
    vec4 borderSnap = mix(vec4(0.0), vec4(half_pixelrange),
                          greaterThan(currentStyle.borderWidth, vec4(0.0)));
    inner_distance  = sdf_rounded_inner(sdfUv, uvsize, currentStyle.cornerRadius,
                                        currentStyle.borderWidth + borderSnap);

    float inner_factor  = clamp(1 - (inner_distance / pixelrange), 0, 1);
    float inner_opacity = min(outer_opacity, inner_factor);

    // Border share of the fragment's covered area. (1 - inner_opacity) would also
    // count the outer AA falloff and fringe edges with border color.
    float border_opacity = max(outer_opacity - inner_opacity, 0.0);
    float border_factor  = (outer_opacity > 1e-5) ? (border_opacity / outer_opacity) : 0.0;

    // Dashed border: modulate border_factor by a dash pattern along the box perimeter.
    if (currentStyle.dashSize > 0.5) {
        // Determine axis-aligned edge and compute pixel position along that edge.
        vec2 side = abs(sdfUv / uvsize); // 0..1 from center on each axis
        float edgePos = (side.y >= side.x)
            ? inData.UV.x * inSurface.boxSize.x   // top or bottom: horizontal position
            : inData.UV.y * inSurface.boxSize.y;  // left or right: vertical position
        float dashPos  = mod(edgePos + currentStyle.dashOffset, 2.0 * currentStyle.dashSize);
        float dashMask = abs(dashPos - currentStyle.dashSize) - 0.5 * currentStyle.dashSize;
        const float dashSmooth = 0.5;
        border_factor *= saturate((dashMask + dashSmooth) / (2.0 * dashSmooth));
    }
    
    
    float u = inData.UV.x;
    float v = inData.UV.y;
    vec4 weights = vec4(
        (1.0 - u) * (1.0 - v),   // TL (pixel 0)
        u * (1.0 - v),           // TR (pixel 1)
        u * v,                   // BR (pixel 2)
        (1.0 - u) * v            // BL (pixel 3)
    );

    
    // Distances are in units of the box's smaller side, so its middle is half a unit deep.
    float edgeDepth = saturate(-outer_distance * 2.0);

    // Blend colors across "from" and "to" states.
    for (int i = 0; i < 2; i++)
        if (inSurface.blendFactors[i] > 0.0)
            AddPostStyle(currentStyle, uv, sdfUv, uvsize, edgeDepth, inSurface.blendFactors[i], inSurface.styleIndex[i], weights);

    // Per-element ColorStyle override: replaces the box style's background entirely so
    // a swatch element can paint a solid color or a gradient via colorIndex.
    if (inSurface.colorIndex != 0)
        currentStyle.backgroundColor = getSDFColor(inSurface.colorIndex, uv, uvsize, edgeDepth,
                                                   inSurface.textureIndex, inSurface.boxSize, weights);

    // Un-premultiply RGB now that all weighted contributions are summed.
    if (currentStyle.backgroundColor.a > 1e-5)
        currentStyle.backgroundColor.rgb /= currentStyle.backgroundColor.a;
    if (currentStyle.borderColor.a > 1e-5)
        currentStyle.borderColor.rgb /= currentStyle.borderColor.a;

    // Mix border and background. Opaque colors mix in linear light; a translucent one
    // mixes premultiplied in sRGB, the space the result composites in, so where it meets
    // the border (rounded corners) it never shows more color than either side.
    vec4 color;
    if (currentStyle.backgroundColor.a >= 1.0 && currentStyle.borderColor.a >= 1.0)
    {
        color = mix(srgbToLinear(currentStyle.backgroundColor), srgbToLinear(currentStyle.borderColor), border_factor);
        color.rgb = linearToSrgb(color.rgb);
    }
    else
    {
        const vec4 background_color = vec4(currentStyle.backgroundColor.rgb * currentStyle.backgroundColor.a, currentStyle.backgroundColor.a);
        const vec4 border_color     = vec4(currentStyle.borderColor.rgb * currentStyle.borderColor.a, currentStyle.borderColor.a);
        color = mix(background_color, border_color, border_factor);
        if (color.a > 1e-5)
            color.rgb /= color.a;
    }

    // make everything outside the rounded box invisible
    color.a *= outer_opacity;

    if (color.a <= 1e-5)
        discard;

    color.a = clamp(color.a, 0, 1);

    // pre-multiply alpha
    color.rgb *= color.a;

    outFragColor = color * inOpacity;   // premultiplied: the whole color fades
}