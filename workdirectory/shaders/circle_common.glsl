#include "all_common.glsl"

layout(buffer_reference, std430)
struct Circle
{
    vec4       transform0;
    vec4       transform1;
    ivec2      clipMin;
    ivec2      clipMax;

    vec2       center;
    float      radius;
    float      lineWidth;
    int        colorIndex;
    float      dashSize;
    float      dashOffset;
    float      gradientOffset;
    float      gradientScale;
};

layout(buffer_reference, std430) readonly buffer Circles { Circle circles[]; };

// per surface data
struct SurfaceData
{
    int   colorIndex;
    vec2  center;      // in the quad's own pixels, like FragmentData.UV
    vec2  quadSize;
    float radius;      // screen pixels
    float lineWidth;   // screen pixels
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
