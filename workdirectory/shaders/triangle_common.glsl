#include "all_common.glsl"

struct TriSurfaceData
{
	int triangleIndex;
	int textureIndex;
    vec2 triMin;
    vec2 triSize;
};

struct TriFragmentData
{
	vec2 position;
};
