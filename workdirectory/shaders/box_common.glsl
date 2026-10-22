#include "all_common.glsl"

// per surface data
struct SurfaceData
{
	ivec4 styleIndex;
	vec2 boxSize;
	int textureIndex;
    float transformScale; // geometric mean of the content transform's X and Y scale factors
    vec4 blendFactors;
	int colorIndex; // per-element ColorStyle override (0 = use box style's background)
    vec2 axisScale;   // the transform's X and Y scale (UI units -> pixels)
};

// per fragment data
struct FragmentData
{
	vec2 UV;
}; 
