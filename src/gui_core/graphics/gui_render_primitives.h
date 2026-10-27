#pragma once

// GPU primitive formats for the GUI renderer. The structs are std430 buffer
// layouts shared verbatim with the shaders, so they are graphics-API-neutral:
// any backend (Vulkan today; D3D12/Metal/WebGPU later) consumes the same
// bytes. The per-kind spec table drives pipeline creation and dispatch -
// adding a primitive kind is one enum value, one table row, one shader pair
// and one emit method.

#include <gui_core/gui_base_types.h>      // int2, float2, short2, color4f, float4x4
#include <gui_core/graphics/gui_image.h>  // ImageIndex

#include <stdint.h>
#include <stddef.h>

namespace tachyon
{
    /// @addtogroup graphics
    /// @{

    struct alignas(16) glsl_float4 { float x, y, z, w; };

    struct alignas(16) Box
    {
        glsl_float4     transform0 = { 1, 0, 0 };
        glsl_float4     transform1 = { 0, 1, 0 };
        tachyon::int2       clipMin = { 0,0 };
        tachyon::int2       clipMax = { 0,0 };

        tachyon::int2       pos = { 0,0 };
        tachyon::int2       size = { 0,0 };

        tachyon::ImageIndex textureIndex  = tachyon::ImageIndex::Invalid;
        uint32_t        styleIndex    = 0; ///< "from" state GPU style index
        int16_t         colorIndex    = 0; ///< color style overriding the background (gradients too); 0: none
        int16_t         _pad3         = 0;
        uint32_t        styleIndexTo  = 0; ///< "to" state GPU style index
        float           blendT        = 1.0f; ///< 0 = fully "from", 1 = fully "to"
        uint32_t        _pad0         = 0;
        /// The box in window pixels, snapped relative to its parent (SnapToParent);
        /// { x0, y0, x1, y1 }, all zero = snap absolutely.
        int16_t         snappedPx[4]  = { 0, 0, 0, 0 };
    };
    static_assert(sizeof(Box) == 96, "Box struct size must be 96 bytes to match GLSL std430 layout");
    static_assert(offsetof(Box, snappedPx) == 88, "Box::snappedPx offset mismatch with GLSL");
    static_assert(sizeof(Box::textureIndex) == sizeof(uint32_t), "Box::textureIndex must be uint32_t for packing reasons");

    struct alignas(16) Glyph
    {
        glsl_float4     transform0 = { 1, 0, 0 };
        glsl_float4     transform1 = { 0, 1, 0 };
        tachyon::int2       clipMin = { 0,0 };
        tachyon::int2       clipMax = { 0,0 };

        tachyon::float2     pos = { 0,0 };
        tachyon::float2     size = { 0,0 };

        tachyon::float2     uvMin = { 0,0 };
        tachyon::float2     uvMax = { 0,0 };

        float           scale = 0;
        uint16_t        bpp = 0;
        uint16_t        mode = 0;

        tachyon::ImageIndex textureIndex = tachyon::ImageIndex::Invalid;
        int16_t         textColorIndex = 0;
        int16_t         _pad = 0;
    };
    static_assert(sizeof(Glyph::textureIndex) == sizeof(uint32_t), "Glyph::textureIndex must be uint32_t for packing reasons");

    struct alignas(16) Dot
    {
        glsl_float4     transform0 = { 1, 0, 0 };
        glsl_float4     transform1 = { 0, 1, 0 };
        tachyon::int2       clipMin = { 0,0 };
        tachyon::int2       clipMax = { 0,0 };

        tachyon::color4f    color;
        float           size = 0;
        /// std430 aligns vec2 to 8 bytes: pos must start at offset 72, not 68.
        float           _pad_size_to_pos = 0;
        /// Float so positions far from origin or under a pan/zoom transform don't
        /// wrap. The shader applies transform0/1 to both size and position.
        float           pos[2] = { 0, 0 };
    };

    struct alignas(16) Line
    {
        glsl_float4     transform0 = { 1, 0, 0 };
        glsl_float4     transform1 = { 0, 1, 0 };
        tachyon::int2       clipMin = { 0,0 };
        tachyon::int2       clipMax = { 0,0 };

        int             colorIndex     = 0;
        float           lineWidth      = 0;
        float           dashSize       = 0;
        float           dashOffset     = 0;
        float           gradientOffset = 0;
        float           gradientScale  = 1;

        tachyon::short2     p0 = { 0,0 };
        tachyon::short2     p1 = { 0,0 };
        tachyon::short2     p2 = { 0,0 };
        tachyon::short2     p3 = { 0,0 };
    };

    static_assert(sizeof(Line) == 96,                    "Line struct size mismatch with GLSL std430 layout");
    static_assert(offsetof(Line, colorIndex)     == 48,  "Line::colorIndex offset mismatch");
    static_assert(offsetof(Line, lineWidth)      == 52,  "Line::lineWidth offset mismatch");
    static_assert(offsetof(Line, gradientOffset) == 64,  "Line::gradientOffset offset mismatch");
    static_assert(offsetof(Line, p0)             == 72,  "Line::p0 offset mismatch");

    struct alignas(16) Triangle
    {
        glsl_float4     transform0 = { 1, 0, 0 };
        glsl_float4     transform1 = { 0, 1, 0 };
        tachyon::int2       clipMin = { 0,0 };
        tachyon::int2       clipMax = { 0,0 };

        tachyon::float2     p0 = { 0,0 };
        tachyon::float2     p1 = { 0,0 };
        tachyon::float2     p2 = { 0,0 };

        tachyon::ImageIndex textureIndex = tachyon::ImageIndex::Invalid;

        int16_t         colorIndex = 0;

        int16_t         _pad1 = 0;
    };

    struct alignas(16) Quad
    {
        glsl_float4     transform0 = { 1, 0, 0 };
        glsl_float4     transform1 = { 0, 1, 0 };
        tachyon::int2       clipMin = { 0,0 };
        tachyon::int2       clipMax = { 0,0 };

        tachyon::int2       pos = { 0,0 };
        tachyon::int2       size = { 0,0 };

        tachyon::ImageIndex textureIndex = tachyon::ImageIndex::Invalid;

        int16_t         colorIndex = 0;
        int16_t         blur = 0;           ///< shadow blur, pixels; pos/size include it on every side
        float           cornerRadius = 0.0f;
    };

    /// Shader-visible buffer address. uint64_t on every modern API
    /// (VkDeviceAddress / D3D12_GPU_VIRTUAL_ADDRESS / MTLGPUAddress).
    using DeviceAddress = uint64_t;

    enum class PrimitiveKind : uint8_t
    {
        Box = 0,
        Glyph,
        Dot,
        Line,
        Triangle,
        Quad,
        Count
    };
    inline constexpr size_t kPrimitiveKindCount = size_t(PrimitiveKind::Count);

    struct PrimitiveSpec
    {
        const char* shaderBaseName; ///< backend composes path + stage suffix + format
        uint32_t    stride;         ///< per-primitive byte size (std430)
        uint8_t     pushSlot;       ///< index into GPUDrawPushConstants::primitiveAddresses
    };

    /// Push-constant slot order matches the GLSL push block (box, glyph, dot,
    /// line, triangle). The block is exactly 128 bytes, with no room for a sixth
    /// address, so Quad shares the Triangle slot: each pass writes its own buffer
    /// address into its slot before drawing, and a draw call holds one kind.
    inline constexpr PrimitiveSpec kPrimitiveSpecs[kPrimitiveKindCount] = {
        /* Box      */ { "box_shader",      sizeof(Box),      0 },
        /* Glyph    */ { "glyph_shader",    sizeof(Glyph),    1 },
        /* Dot      */ { "dot_shader",      sizeof(Dot),      2 },
        /* Line     */ { "line_shader",     sizeof(Line),     3 },
        /* Triangle */ { "triangle_shader", sizeof(Triangle), 4 },
        /* Quad     */ { "quad_shader",     sizeof(Quad),     4 },
    };

    struct GPUDrawPushConstants
    {
        tachyon::float4x4 worldMatrix = {};

        /// Indexed by PrimitiveSpec::pushSlot (slot order is shader contract).
        DeviceAddress primitiveAddresses[5] = {};
        DeviceAddress boxStyleBuffer = {};
        DeviceAddress colorStyleBuffer = {};

        float elapsedTime = 0.0f; ///< frame clock seconds, for shader animation
        float _timePad    = 0.0f; ///< pad to 128 bytes (Vulkan minimum)
    };
    static_assert(sizeof(GPUDrawPushConstants) == 128, "GPUDrawPushConstants must be 128 bytes");
    /// @}
}
