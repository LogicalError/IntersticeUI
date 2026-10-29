#include "all_common.glsl"

// per surface data
struct SurfaceData
{
	vec4  color;
	vec4  clip;     // the clip rect in window pixels: min x, min y, max x, max y
	float dotSize;
};

// per fragment data
struct FragmentData
{
	vec2 UV;
	vec2 windowPos; // window pixels before the world matrix, which moves cached draws
};


const float kMinThickness = 1.5;
