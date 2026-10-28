#include "all_common.glsl"

// per surface data
struct SurfaceData
{
	int textColorIndex; // VisualColorStyleIndex (1-based)
	int textureIndex;
    float scale;
    uint bpp;
    uint mode;
    float shadowBlur; // pixels; below 0 = the glyph itself, not a shadow
};

// The atlas' glyph distance field (SkribidiContext::kSdfDistanceScale): 128 on the edge,
// 8 levels per texel, higher outside. It reaches 16 texels each way, then stays flat.
const float kSdfEdge       = 128.0 / 255.0;
const float kSdfPerTexel   = 8.0 / 255.0;
const float kSdfReachTexels = 15.5;

// per fragment data
struct FragmentData
{
	vec2 UV;
}; 
