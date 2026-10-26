#pragma once

#include <gui_core/gui_id.h>
#include <gui_core/gui_base_types.h>      // IntRect, int2, float2, color4b, color4f, float3x2
#include <gui_core/graphics/gui_image.h>  // ImageIndex (default args)

#include <stdint.h>
#include <span>

namespace interstice
{
    enum class VisualBoxStyleIndex : uint16_t;
    enum class VisualColorStyleIndex : uint16_t;
    enum class ElementState : uint8_t;

    struct LineStyle
    {
        // Defaults to VisualColorStyleIndex::Invalid.
        VisualColorStyleIndex colorStyleIndex {};
        float lineWidth       = 0;
        float dashSize        = 0;
        float dashOffset      = 0;
        float gradientOffset  = 0.0f; // start of gradient range [0,1]
        float gradientScale   = 1.0f; // fraction of gradient to cover
    };

	class GUIRenderer
    {
    public:
        virtual ~GUIRenderer() {}

        virtual void Reset() = 0;


        virtual void SetTransform(interstice::float3x2 transformation) = 0;

        // from/toStyleIndex are flat GPU style indices; blendT 0 = fully "from", 1 = fully "to".
        // colorStyleIndex overrides the box style's background (Invalid = no override).
        // snappedPx: the box in window pixels snapped relative to its parent
        // (gui_pixel_snap.h); empty = snap the transformed rect to the pixel grid.
        virtual void RenderBox(interstice::IntRect rect, interstice::IntRect clipRect,
                               uint32_t fromStyleIndex, uint32_t toStyleIndex, float blendT,
                               interstice::VisualColorStyleIndex colorStyleIndex = {},
                               interstice::ImageIndex textureIndex = interstice::ImageIndex::Invalid,
                               interstice::IntRect snappedPx = {}) = 0;

        void RenderBox(interstice::IntRect rect, interstice::IntRect clipRect, VisualBoxStyleIndex boxStyleIndex,
                       interstice::ElementState state, interstice::VisualColorStyleIndex colorStyleIndex = {},
                       interstice::ImageIndex textureIndex = interstice::ImageIndex::Invalid, interstice::IntRect snappedPx = {});

        void RenderBox(interstice::IntRect rect, interstice::IntRect clipRect, VisualBoxStyleIndex boxStyleIndex,
                       interstice::VisualColorStyleIndex colorStyleIndex = {},
                       interstice::ImageIndex textureIndex = interstice::ImageIndex::Invalid, interstice::IntRect snappedPx = {});

        virtual void RenderLine(interstice::int2 p0, interstice::int2 p1, interstice::int2 p2, interstice::int2 p3, interstice::IntRect clipRect, const LineStyle& lineStyle) = 0;
        void         RenderLine(interstice::int2 p0, interstice::int2 p1, interstice::IntRect clipRect, const LineStyle& lineStyle) { RenderLine(p0, p0, p1, p1, clipRect, lineStyle); }

        void RenderCircle(interstice::int2 center, float radius, interstice::IntRect clipRect, const LineStyle& lineStyle);

        virtual void RenderDot(interstice::float2 pos, interstice::IntRect clipRect, float size, interstice::color4f color) = 0;
        virtual void RenderTriangle(interstice::float2 p0, interstice::float2 p1, interstice::float2 p2, interstice::IntRect clipRect, interstice::VisualColorStyleIndex colorStyleIndex, interstice::ImageIndex textureIndex = interstice::ImageIndex::Invalid) = 0;

        // A Box without SDF borders, style blending or element state.
        virtual void RenderQuad(interstice::IntRect rect, interstice::IntRect clipRect, interstice::VisualColorStyleIndex colorStyleIndex, float cornerRadius = 0.0f, interstice::ImageIndex textureIndex = interstice::ImageIndex::Invalid) = 0;
        // A box shadow: `rect` is the shadow's shape, which fades out over `blur` pixels
        // on either side of its edge (Gaussian, sigma = blur / 2, as CSS).
        virtual void RenderShadow(interstice::IntRect rect, interstice::IntRect clipRect, interstice::VisualColorStyleIndex colorStyleIndex, float cornerRadius, int blur) = 0;

        virtual void RenderGlyph(interstice::float2 pos, interstice::float2 size, interstice::float2 uvMin, interstice::float2 uvMax, float scale, int bpp, int mode, VisualColorStyleIndex textColorIndex, interstice::IntRect clipRect, interstice::ImageIndex textureIndex = interstice::ImageIndex::Invalid) = 0;

        // Convex polygon, fan-triangulated from points[0]; needs at least 3 points.
        void RenderFilledPolygon(std::span<const interstice::float2> points, interstice::IntRect clipRect,
                                 VisualColorStyleIndex colorIndex,
                                 interstice::ImageIndex textureIndex = interstice::ImageIndex::Invalid);

        // Outlined polyline. closed=true adds a closing segment from last->first point.
        void RenderPolyline(std::span<const interstice::int2> points, bool closed,
                            interstice::IntRect clipRect, const LineStyle& style);

    public:
        virtual bool StartResizing(ID id) = 0;
        virtual void StopResizing() = 0;

        virtual bool BeginWindow(ID id) = 0;
        virtual void EndWindow() = 0;

        // Draw caching: capture a subtree's primitives once and replay them translated.
        // Called inside BeginWindow/EndWindow; default no-ops.
        //   Begin/EndScopeCapture: primitives are recorded under scopeId and also
        //   emitted into the current window.
        //   ReplayScopeCapture: re-emits a captured subtree moved by `delta`;
        //   false if nothing is cached for scopeId.
        virtual void BeginScopeCapture(ID /*scopeId*/) {}
        virtual void EndScopeCapture(ID /*scopeId*/) {}
        virtual bool ReplayScopeCapture(ID /*scopeId*/, interstice::int2 /*delta*/) { return false; }
        virtual bool HasScopeCapture(ID /*scopeId*/) const { return false; }
        virtual void InvalidateScopeCapture(ID /*scopeId*/) {}
        virtual void ClearScopeCaptures() {}
    };

}
