#pragma once

#include <gui_core/gui_id.h>
#include <gui_core/gui_base_types.h>      // IntRect, int2, float2, color4b, color4f, float3x2
#include <gui_core/graphics/gui_image.h>  // ImageIndex (default args)

#include <stdint.h>
#include <span>

namespace tachyon
{
    /// @addtogroup graphics
    /// @{

    enum class VisualBoxStyleIndex : uint16_t;
    enum class VisualColorStyleIndex : uint16_t;
    enum class ElementState : uint8_t;

    struct LineStyle
    {
        /// Defaults to VisualColorStyleIndex::Invalid.
        VisualColorStyleIndex colorStyleIndex {};
        float lineWidth       = 0;
        float dashSize        = 0;
        float dashOffset      = 0;
        float gradientOffset  = 0.0f; ///< start of gradient range [0,1]
        float gradientScale   = 1.0f; ///< fraction of gradient to cover
    };

	class GUIRenderer
    {
    public:
        virtual ~GUIRenderer() {}

        virtual void Reset() = 0;


        virtual void SetTransform(tachyon::float3x2 transformation) = 0;

        /// Draws a styled box, blended from `fromStyleIndex` to `toStyleIndex` (flat GPU style
        /// indices) by `blendT` in [0, 1]. `colorStyleIndex` overrides the background (Invalid:
        /// none). `snappedPx` is the box in window pixels snapped relative to its parent
        /// (SnapToParent); empty snaps the transformed rect to the pixel grid.
        virtual void RenderBox(tachyon::IntRect rect, tachyon::IntRect clipRect,
                               uint32_t fromStyleIndex, uint32_t toStyleIndex, float blendT,
                               tachyon::VisualColorStyleIndex colorStyleIndex = {},
                               tachyon::ImageIndex textureIndex = tachyon::ImageIndex::Invalid,
                               tachyon::IntRect snappedPx = {}) = 0;

        void RenderBox(tachyon::IntRect rect, tachyon::IntRect clipRect, VisualBoxStyleIndex boxStyleIndex,
                       tachyon::ElementState state, tachyon::VisualColorStyleIndex colorStyleIndex = {},
                       tachyon::ImageIndex textureIndex = tachyon::ImageIndex::Invalid, tachyon::IntRect snappedPx = {});

        void RenderBox(tachyon::IntRect rect, tachyon::IntRect clipRect, VisualBoxStyleIndex boxStyleIndex,
                       tachyon::VisualColorStyleIndex colorStyleIndex = {},
                       tachyon::ImageIndex textureIndex = tachyon::ImageIndex::Invalid, tachyon::IntRect snappedPx = {});

        virtual void RenderLine(tachyon::int2 p0, tachyon::int2 p1, tachyon::int2 p2, tachyon::int2 p3, tachyon::IntRect clipRect, const LineStyle& lineStyle) = 0;
        void         RenderLine(tachyon::int2 p0, tachyon::int2 p1, tachyon::IntRect clipRect, const LineStyle& lineStyle) { RenderLine(p0, p0, p1, p1, clipRect, lineStyle); }

        /// A circle outline `lineStyle.lineWidth` wide, exactly round. A gradient and dashes run
        /// clockwise from the right (+x towards +y); gradientOffset / gradientScale pick the
        /// part of the gradient that goes once around.
        virtual void RenderCircle(tachyon::float2 center, float radius, tachyon::IntRect clipRect, const LineStyle& lineStyle) = 0;

        virtual void RenderDot(tachyon::float2 pos, tachyon::IntRect clipRect, float size, tachyon::color4f color) = 0;
        virtual void RenderTriangle(tachyon::float2 p0, tachyon::float2 p1, tachyon::float2 p2, tachyon::IntRect clipRect, tachyon::VisualColorStyleIndex colorStyleIndex, tachyon::ImageIndex textureIndex = tachyon::ImageIndex::Invalid) = 0;

        /// Draws a filled, optionally rounded and textured rect: a box without borders, style
        /// blending or element state.
        virtual void RenderQuad(tachyon::IntRect rect, tachyon::IntRect clipRect, tachyon::VisualColorStyleIndex colorStyleIndex, float cornerRadius = 0.0f, tachyon::ImageIndex textureIndex = tachyon::ImageIndex::Invalid) = 0;
        /// A box shadow: `rect` is the shadow's shape, which fades out over `blur` pixels
        /// on either side of its edge (Gaussian, sigma = blur / 2, as CSS).
        virtual void RenderShadow(tachyon::IntRect rect, tachyon::IntRect clipRect, tachyon::VisualColorStyleIndex colorStyleIndex, float cornerRadius, int blur) = 0;
        /// Draws nothing: marks `rect` as covered by opaque content drawn after this call, so
        /// what was drawn before it there is skipped on the GPU. Only for a rect the later
        /// content covers completely, with no anti-aliased edge.
        virtual void RenderOccluder(tachyon::IntRect /*rect*/, tachyon::IntRect /*clipRect*/) {}

        /// A glyph from the text atlas. `scale` is atlas texels per pixel. With a `shadowBlur`
        /// of 0 or more it is a text shadow instead: the glyph's shape in `textColorIndex`
        /// alone (a color glyph's too), blurred by that many pixels (Gaussian,
        /// sigma = blur / 2, as CSS) as far as the atlas' distance field reaches.
        virtual void RenderGlyph(tachyon::float2 pos, tachyon::float2 size, tachyon::float2 uvMin, tachyon::float2 uvMax, float scale, int bpp, int mode, VisualColorStyleIndex textColorIndex, tachyon::IntRect clipRect, tachyon::ImageIndex textureIndex = tachyon::ImageIndex::Invalid, float shadowBlur = -1.0f) = 0;

        /// Convex polygon, fan-triangulated from points[0]; needs at least 3 points.
        void RenderFilledPolygon(std::span<const tachyon::float2> points, tachyon::IntRect clipRect,
                                 VisualColorStyleIndex colorIndex,
                                 tachyon::ImageIndex textureIndex = tachyon::ImageIndex::Invalid);

        /// Outlined polyline; `closed` adds a segment from the last point back to the first.
        void RenderPolyline(std::span<const tachyon::int2> points, bool closed,
                            tachyon::IntRect clipRect, const LineStyle& style);

    public:
        virtual bool StartResizing(ID id) = 0;
        virtual void StopResizing() = 0;

        virtual bool BeginWindow(ID id) = 0;
        virtual void EndWindow() = 0;

        // Draw caching: a subtree's primitives are captured once and replayed translated.
        // Called between BeginWindow and EndWindow; the defaults do nothing.

        /// Starts recording primitives under `scopeId`, replacing its previous capture.
        /// Captures don't nest.
        virtual void BeginScopeCapture(ID /*scopeId*/) {}
        /// Ends the capture of `scopeId` and draws it into the current window.
        virtual void EndScopeCapture(ID /*scopeId*/) {}
        /// Draws the capture of `scopeId` again, moved by `delta` pixels; false if there is none.
        virtual bool ReplayScopeCapture(ID /*scopeId*/, tachyon::int2 /*delta*/) { return false; }
        /// True when `scopeId` has a non-empty capture.
        virtual bool HasScopeCapture(ID /*scopeId*/) const { return false; }
        /// Drops the capture of `scopeId`.
        virtual void InvalidateScopeCapture(ID /*scopeId*/) {}
        /// Drops every capture.
        virtual void ClearScopeCaptures() {}
    };

    /// @}
}
