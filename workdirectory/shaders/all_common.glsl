layout(buffer_reference, std430)
struct ColorStyle
{
    f16vec4 transformationX;
    f16vec4 transformationY;

    u16vec4 colorUV_type;

    int     textureIndex;
    f16vec2 animSpeed;      // x = U/gradient scroll speed, y = V scroll speed (textures only)
};

layout(buffer_reference, std430)
struct BoxStyle // aka 'StyleState'
{
	u16vec4  borderWidth;
	u16vec4  cornerRadius;

    i16vec4  borderColorIndex;    // per-edge: x=top, y=right, z=bottom, w=left
    int16_t  backgroundColorIndex;
    uint16_t borderDashSize;      // pixels per dash (0 = solid)
    uint16_t borderDashOffset;    // pixel start offset into dash pattern
    uint16_t shadowIndex;         // CPU only
};

layout(buffer_reference, std430)
struct Box
{
    vec4       transform0;
    vec4       transform1;
    ivec2      clipMin;
    ivec2      clipMax;

    ivec2      pos;
    ivec2      size;

    int        textureIndex;
    int        styleIndex;   // "from" state GPU style index
    int16_t    colorIndex;   // Per-element ColorStyle override (0 = use box style's background); supports gradients.
    int16_t    _pad3;
    int        styleIndexTo; // "to" state GPU style index
    float      blendT;       // 0 = fully "from", 1 = fully "to"
    int        _pad0;
    i16vec4    snappedPx;    // window pixels {x0,y0,x1,y1} snapped to the parent; all 0 = snap here
};

layout(buffer_reference, std430)
struct Glyph
{
    vec4       transform0;
    vec4       transform1;
    ivec2      clipMin;
    ivec2      clipMax;
               
    vec2       pos;
    vec2       size;
     
    vec2       uvMin;
    vec2       uvMax;
               
    float      scale;
    u16vec2    glyph_type;
               
    int        textureIndex; // Glyph atlas, not to be confused with ColorStyle texture index
    int16_t    textColorIndex; // ColorStyle index for text color (1-based, 0 = invalid)
    int16_t    shadowBlur;     // -1: the glyph itself; 0 and up: a text shadow blurred by this many 1/8 pixels
}; 

layout(buffer_reference, std430)
struct Dot
{
    vec4       transform0;
    vec4       transform1;
    ivec2      clipMin;
    ivec2      clipMax;

	vec4       color;
    float      size;
    // vec2 (not u16vec2): allows negative / >65535 positions and
    // sub-pixel precision, matching the C++ Dot struct.
    vec2       pos;
};

layout(buffer_reference, std430)
struct Line
{
    vec4       transform0;
    vec4       transform1;
    ivec2      clipMin;
    ivec2      clipMax;
    
    int        colorIndex;
    float      lineWidth;
    float      dashSize;
    float      dashOffset;
    float      gradientOffset;
    float      gradientScale;

    i16vec2    p0, p1, p2, p3;
};

layout(buffer_reference, std430)
struct Triangle
{
    vec4       transform0;
    vec4       transform1;               
    ivec2      clipMin;
    ivec2      clipMax;

    vec2       p0;
    vec2       p1;
    vec2       p2;
    
    int        textureIndex;

    int16_t    colorIndex;
    int16_t    _pad1;
};

layout(buffer_reference, std430)
struct Quad
{
    vec4       transform0;
    vec4       transform1;
    ivec2      clipMin;
    ivec2      clipMax;

    ivec2      pos;
    ivec2      size;

    int        textureIndex;
    int16_t    colorIndex;
    int16_t    blur;         // pixels; a shadow: the rect includes the blur on every side
    float      cornerRadius;
};

layout(buffer_reference, std430) readonly buffer Boxes  { Box   boxes []; };
layout(buffer_reference, std430) readonly buffer Dots   { Dot   dots  []; };
layout(buffer_reference, std430) readonly buffer Lines  { Line  lines []; };
layout(buffer_reference, std430) readonly buffer Triangles { Triangle triangles []; };
layout(buffer_reference, std430) readonly buffer Quads  { Quad  quads []; };
layout(buffer_reference, std430) readonly buffer Glyphs { Glyph glyphs[]; };
layout(buffer_reference, std430) readonly buffer BoxStyles { BoxStyle boxStyles[]; };
layout(buffer_reference, std430) readonly buffer ColorStyles { ColorStyle colorStyles[]; };

layout( push_constant ) uniform constants
{	
	mat4   worldMatrix;
	Boxes  boxBuffer;
	Glyphs glyphBuffer;
	Dots   dotsBuffer;
	Lines  lineBuffer;
	Triangles triangleBuffer;

	BoxStyles boxStyleBuffer;
	ColorStyles colorStyleBuffer;
	float elapsedTime;  // seconds since app start
	float _timePad;     // pad to 128 bytes
} PushConstants;



float srgbToLinear(float cs) {
    return (cs <= 0.04045f) ? (cs / 12.92f) : pow((cs + 0.055f) / 1.055f, 2.4f);
}

float linearToSrgb(float cl) {
    return (cl <= 0.0031308f) ? (12.92f * cl) : (1.055f * pow(cl, 1.0f / 2.4f) - 0.055f);
}

vec3 srgbToLinear(vec3 srgb) {
    return mix(srgb / 12.92, pow((srgb + 0.055) / 1.055, vec3(2.4)), greaterThan(srgb, vec3(0.04045)));
}

vec3 linearToSrgb(vec3 linear) {
    return mix(12.92 * linear, 1.055 * pow(linear, vec3(1.0 / 2.4)) - 0.055, greaterThan(linear, vec3(0.0031308)));
}

 // does this make sense to use same curve for alpha? 
 // it seems to look better...

vec4 srgbToLinear(vec4 srgb) {
    return vec4(srgbToLinear(srgb.rgb), srgb.a);
}

vec4 linearToSrgb(vec4 linear) {
    return vec4(linearToSrgb(linear.rgb), linear.a);
}

float saturate(float val) { return clamp(val,0.0,1.0); }

// A vertex at window pixel `position` of the primitive drawn `order`-th (its transform0.w):
// the world matrix maps the order to depth, later nearer, and past the depth range all
// stay at the nearest depth.
vec4 PrimitivePosition(vec2 position, float order)
{
    vec4 p = PushConstants.worldMatrix * vec4(position, order, 1.0);
    p.z = min(p.z, p.w);
    return p;
}

// How opaque the primitive is, from its transform1.w (how transparent it is, so 0 is opaque).
float PrimitiveOpacity(vec4 transform1)
{
    return 1.0 - transform1.w;
}


#include "bezier_sdf.glsl"