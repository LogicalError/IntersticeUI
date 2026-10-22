#include "all_common.glsl"

// per surface data
struct SurfaceData
{
	int  colorIndex;
	vec2 p0, p1, p2, p3;
    vec2 boxSize;
    vec2 lineSize;
    float lineWidth;
	float dashSize;
	float dashOffset;
    float gradientOffset;
    float gradientScale;
};

// per fragment data
struct FragmentData
{
	vec2 UV;
}; 
