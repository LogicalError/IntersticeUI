#include "all_common.glsl"

struct QuadSurfaceData
{
    int   quadIndex;
    int   textureIndex;
    vec2  boxSize;
    float transformScale;
    float cornerRadius;
};

struct QuadFragmentData
{
    vec2 UV;
};
