#pragma once

#include <gui_core/gui_id.h>
#include <gui_core/gui_base_types.h>      // IntRect, int2, float2, color4b, color4f, float3x2
#include <gui_core/graphics/gui_image.h>  // ImageIndex (default args)

#include <stdint.h>
#include <span>

namespace GUI
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


        virtual void SetTransform(GUI::float3x2 transformation) = 0;

        // from/toStyleIndex are flat GPU style indices; blendT 0 = fully "from", 1 = fully "to".
        // colorStyleIndex overrides the box style's background (Invalid = no override).
        // snappedPx: the box in window pixels snapped relative to its parent
        // (gui_pixel_snap.h); empty = snap the transformed rect to the pixel grid.
        virtual void RenderBox(GUI::IntRect rect, GUI::IntRect clipRect,
                               uint32_t fromStyleIndex, uint32_t toStyleIndex, float blendT,
                               GUI::VisualColorStyleIndex colorStyleIndex = {},
                               GUI::ImageIndex textureIndex = GUI::ImageIndex::Invalid,
                               GUI::IntRect snappedPx = {}) = 0;

        void RenderBox(GUI::IntRect rect, GUI::IntRect clipRect, VisualBoxStyleIndex boxStyleIndex,
                       GUI::ElementState state, GUI::VisualColorStyleIndex colorStyleIndex = {},
                       GUI::ImageIndex textureIndex = GUI::ImageIndex::Invalid, GUI::IntRect snappedPx = {});

        void RenderBox(GUI::IntRect rect, GUI::IntRect clipRect, VisualBoxStyleIndex boxStyleIndex,
                       GUI::VisualColorStyleIndex colorStyleIndex = {},
                       GUI::ImageIndex textureIndex = GUI::ImageIndex::Invalid, GUI::IntRect snappedPx = {});

        virtual void RenderLine(GUI::int2 p0, GUI::int2 p1, GUI::int2 p2, GUI::int2 p3, GUI::IntRect clipRect, const LineStyle& lineStyle) = 0;
        void         RenderLine(GUI::int2 p0, GUI::int2 p1, GUI::IntRect clipRect, const LineStyle& lineStyle) { RenderLine(p0, p0, p1, p1, clipRect, lineStyle); }

        void RenderCircle(GUI::int2 center, float radius, GUI::IntRect clipRect, const LineStyle& lineStyle);

        virtual void RenderDot(GUI::float2 pos, GUI::IntRect clipRect, float size, GUI::color4f color) = 0;
        virtual void RenderTriangle(GUI::float2 p0, GUI::float2 p1, GUI::float2 p2, GUI::IntRect clipRect, GUI::VisualColorStyleIndex colorStyleIndex, GUI::ImageIndex textureIndex = GUI::ImageIndex::Invalid) = 0;

        // A Box without SDF borders, style blending or element state.
        virtual void RenderQuad(GUI::IntRect rect, GUI::IntRect clipRect, GUI::VisualColorStyleIndex colorStyleIndex, float cornerRadius = 0.0f, GUI::ImageIndex textureIndex = GUI::ImageIndex::Invalid) = 0;

        virtual void RenderGlyph(GUI::float2 pos, GUI::float2 size, GUI::float2 uvMin, GUI::float2 uvMax, float scale, int bpp, int mode, VisualColorStyleIndex textColorIndex, GUI::IntRect clipRect, GUI::ImageIndex textureIndex = GUI::ImageIndex::Invalid) = 0;

        // Convex polygon, fan-triangulated from points[0]; needs at least 3 points.
        void RenderFilledPolygon(std::span<const GUI::float2> points, GUI::IntRect clipRect,
                                 VisualColorStyleIndex colorIndex,
                                 GUI::ImageIndex textureIndex = GUI::ImageIndex::Invalid);

        // Outlined polyline. closed=true adds a closing segment from last→first point.
        void RenderPolyline(std::span<const GUI::int2> points, bool closed,
                            GUI::IntRect clipRect, const LineStyle& style);

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
        virtual bool ReplayScopeCapture(ID /*scopeId*/, GUI::int2 /*delta*/) { return false; }
        virtual bool HasScopeCapture(ID /*scopeId*/) const { return false; }
        virtual void InvalidateScopeCapture(ID /*scopeId*/) {}
        virtual void ClearScopeCaptures() {}
    };

}
