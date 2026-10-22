#include "all_common.glsl"

// per surface data
struct SurfaceData
{
	vec4 color;
	float dotSize;
}; 

// per fragment data
struct FragmentData
{
	vec2 UV;
}; 

  
const float kMinThickness = 1.5;