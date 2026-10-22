#include "all_common.glsl"

// per surface data
struct SurfaceData
{
	int textColorIndex; // VisualColorStyleIndex (1-based)
	int textureIndex;
    float scale;
    uint bpp;
    uint mode;
};

// per fragment data
struct FragmentData
{
	vec2 UV;
}; 
