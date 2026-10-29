#pragma once

// The contexts passed to callbacks registered with ElementStorage: CustomDrawContext for
// drawing, PostLayoutContext for after layout.

#include <gui_core/gui_base_types.h>
#include <gui_core/elements/gui_element.h>            // ElementRef (member by value)
#include <gui_core/graphics/gui_image.h>              // ImageIndex (default args)

#include <span>

namespace tachyon
{
    /// @addtogroup graphics
    /// @{

    class GUIRenderer;
    class ElementStorage;
    class ITextSystem;
    enum class VisualBoxStyleIndex : uint16_t;
    enum class VisualColorStyleIndex : uint16_t;
    enum class ElementState : uint8_t;
    enum class TextID : uint32_t;
    struct LineStyle;

    /// The coordinate space a CustomDrawContext draws in.
    enum class DrawSpace : uint8_t
    {
        Local,  ///< The default: the registered element's content space, (0,0) at its padding box's
                ///< top-left. Its contentTransform (a node view's pan and zoom) and every
                ///< ancestor's apply when drawing.
        Screen, ///< Absolute layout pixels: the space of stored element rects and event.state.screenPos.
    };

    /// Passed to custom-draw callbacks (ElementStorage::AddCustomDraw / AddCustomDrawAfter):
    /// geometry lookups and drawing calls in a current space the callback switches with
    /// SetSpace or SetSpaceRaw, also between calls. Render* coordinates and the rects the
    /// accessors return are in the current space, so they pass straight through; convert
    /// other inputs, such as the cursor position, with ScreenToCurrent. In Screen space a
    /// rect matches the screen only for elements without a transformed ancestor.
    class CustomDrawContext
    {
    public:
        /// Built by the renderer; callbacks receive it.
        CustomDrawContext(GUIRenderer& renderer, ElementStorage& storage,
                          ElementRef elementRef,
                          IntRect elementRectLayout, IntRect innerRectLayout,
                          IntRect clipRectScreen,
                          float3x2 renderTransform, int2 windowOffset,
                          ITextSystem* textSystem = nullptr,
                          float4 ownerPixelRect = {}, IntRect ownerSnappedRect = {}) noexcept
            : m_renderer(&renderer), m_storage(&storage),
              m_elementRef(elementRef),
              m_elementRect(elementRectLayout), m_innerRect(innerRectLayout),
              m_clipRect(clipRectScreen),
              m_renderTransform(renderTransform),
              m_currentTransform(renderTransform),
              m_windowOffset(windowOffset),
              m_textSystem(textSystem),
              m_ownerPixelRect(ownerPixelRect), m_ownerSnappedRect(ownerSnappedRect) {}

        /// ElementRef this callback was registered on.
        inline ElementRef Ref() const noexcept { return m_elementRef; }

        /// Border-box of the registered element, in the CURRENT space.
        inline IntRect ElementRect() const noexcept { return ToCurrentSpace(m_elementRect); }

        /// Padding-box (inner content area) of the registered element, in the CURRENT space.
        inline IntRect ElementInnerRect() const noexcept { return ToCurrentSpace(m_innerRect); }

        /// Scissor region in SCREEN space. Applied implicitly by every Render* method.
        inline IntRect ClipRect() const noexcept { return m_clipRect; }

        /// Set the current coordinate space. Local (default) = the registered element's
        /// content space; Screen = absolute layout (same space as stored rects / screenPos).
        /// Re-points the renderer transform for subsequent Render* calls.
        void SetSpace(DrawSpace space);

        /// Set an arbitrary current space; spaceToWindowLocal maps current-space -> window-local.
        void SetSpaceRaw(float3x2 spaceToWindowLocal);

        /// The current space -> window-local transform.
        inline float3x2 CurrentTransform() const noexcept { return m_currentTransform; }

        /// Convert a SCREEN-space input (e.g. cursor position, in absolute layout) into the
        /// current space. For foreign inputs that are not themselves rendering values.
        int2 ScreenToCurrent(int2 screenPoint) const;

        /// Lookups for OTHER elements (returns rects in the CURRENT space).
        IntRect    GetElementRect(const ElementRef& ref) const noexcept;
        /// Padding-box of another element, in the CURRENT space; empty when not found.
        IntRect    GetInnerRect(const ElementRef& ref) const noexcept;
        /// Tree navigation for other elements.
        ElementRef GetParentRef(const ElementRef& ref)      const noexcept;
        ElementRef GetNextSiblingRef(const ElementRef& ref) const noexcept;
        /// Size of the bounds of an element's children (can exceed its inner size when it scrolls), in pixels.
        int2       GetContentSize(const ElementRef& ref) const noexcept;
        /// Size of an element's padding-box, in pixels.
        int2       GetInnerSize(const ElementRef& ref) const noexcept;

        // Drawing primitives. Input coords are in the current space; clipped to ClipRect().

        /// A styled box (background, border, radius) like an element's. `state` picks the
        /// style's hover / pressed / ... variant; without it the base style is used.
        /// colorStyleIndex overrides the background; textureIndex fills it with an image.
        void RenderBox(IntRect rect, VisualBoxStyleIndex styleIndex, ElementState state,
                       VisualColorStyleIndex colorStyleIndex = {},
                       ImageIndex textureIndex = ImageIndex::Invalid) const;

        void RenderBox(IntRect rect, VisualBoxStyleIndex styleIndex,
                       VisualColorStyleIndex colorStyleIndex = {},
                       ImageIndex textureIndex = ImageIndex::Invalid) const;

        /// RenderBox variants with a sub-clip. Final scissor = ClipRect() & subClip.
        /// subClip is in the same space as `rect`.
        void RenderBox(IntRect rect, IntRect subClip,
                       VisualBoxStyleIndex styleIndex, ElementState state,
                       VisualColorStyleIndex colorStyleIndex = {},
                       ImageIndex textureIndex = ImageIndex::Invalid) const;

        void RenderBox(IntRect rect, IntRect subClip,
                       VisualBoxStyleIndex styleIndex,
                       VisualColorStyleIndex colorStyleIndex = {},
                       ImageIndex textureIndex = ImageIndex::Invalid) const;

        /// A plain filled rect (no border or element state), optionally rounded or textured.
        void RenderQuad(IntRect rect, VisualColorStyleIndex colorStyleIndex,
                        float cornerRadius = 0.0f,
                        ImageIndex textureIndex = ImageIndex::Invalid) const;

        /// RenderQuad with a sub-clip in the same space as `rect`.
        void RenderQuad(IntRect rect, IntRect subClip,
                        VisualColorStyleIndex colorStyleIndex,
                        float cornerRadius = 0.0f,
                        ImageIndex textureIndex = ImageIndex::Invalid) const;

        /// A straight line from p0 to p1.
        void RenderLine(int2 p0, int2 p1, const LineStyle& style) const;
        /// A curve from p0 to p3 shaped by the control points p1 and p2 (Bezier-like).
        void RenderLine(int2 p0, int2 p1, int2 p2, int2 p3, const LineStyle& style) const;

        /// A filled, anti-aliased dot centered on `pos`; `size` is its diameter.
        void RenderDot(float2 pos, float size, color4f color) const;

        /// A filled triangle.
        void RenderTriangle(float2 p0, float2 p1, float2 p2,
                            VisualColorStyleIndex colorStyleIndex,
                            ImageIndex textureIndex = ImageIndex::Invalid) const;

        /// A circle outline, stroked with `style`; see GUIRenderer::RenderCircle.
        void RenderCircle(float2 center, float radius, const LineStyle& style) const;
        void RenderCircle(int2 center, float radius, const LineStyle& style) const; ///< see above
        /// A filled, anti-aliased disc (a quad rounded to a circle).
        void RenderDisc(int2 center, float radius, VisualColorStyleIndex colorStyleIndex) const;

        /// Filled convex polygon via fan triangulation.
        void RenderFilledPolygon(std::span<const float2> points,
                                 VisualColorStyleIndex colorStyleIndex,
                                 ImageIndex textureIndex = ImageIndex::Invalid) const;

        /// Outlined polyline. cornerRadius > 0 (open polylines) turns each bend into a short
        /// arc between the straight runs, up to half of either neighbouring run.
        void RenderPolyline(std::span<const int2> points, bool closed,
                            const LineStyle& style, float cornerRadius = 0.0f) const;

        /// Draw a previously-registered text (textSystem->RegisterText) within `rect`,
        /// expressed in the CURRENT space like every other Render* coord. The glyph
        /// raster scale is taken from the current transform (so it stays crisp under
        /// DPI/zoom); extraScale multiplies it. No-op if the context has no text system.
        void RenderText(TextID textId, IntRect rect, float extraScale = 1.0f) const;

    private:
        /// Express an absolute-layout rect in the current space. In Local space the
        /// registered element's inner-rect origin is removed (pan/zoom is applied at draw,
        /// not here); in Screen/Raw space the rect is returned in absolute layout.
        inline IntRect ToCurrentSpace(IntRect rectAbs) const noexcept
        {
            return m_currentSpaceIsLocal ? (rectAbs - m_innerRect.position) : rectAbs;
        }

        GUIRenderer*    m_renderer;
        ElementStorage* m_storage;
        ElementRef      m_elementRef;
        IntRect         m_elementRect;
        IntRect         m_innerRect;
        IntRect         m_clipRect;
        float3x2        m_renderTransform;   ///< immutable Local space -> window-local
        float3x2        m_currentTransform;  ///< current space -> window-local
        int2            m_windowOffset{};
        ITextSystem*    m_textSystem = nullptr;
        /// The registered element's border box in window pixels, unsnapped and
        /// snapped; boxes drawn here snap relative to it (gui_pixel_snap.h).
        float4          m_ownerPixelRect {};
        IntRect         m_ownerSnappedRect {};
        IntRect         SnappedPixelRect(IntRect rect) const;
        bool            m_currentSpaceIsLocal = true;
    };

    /// Context for post-layout callbacks (AddPostLayout). Fires once per registered
    /// element AFTER layout completes and BEFORE rendering starts. Read-only - no
    /// drawing primitives.
    ///
    /// Use this for state updates that depend on final geometry. For example:
    ///   * cache an inner-rect size for use by next frame's UI code,
    ///   * compute screen-space endpoints from layout rects for a custom subsystem,
    ///   * record a transformed origin for a separate AddCustomDraw to consume.
    ///
    /// All rects returned are in LAYOUT space (pre-transform, window-local). The
    /// element's contentTransform is exposed verbatim for callers that need to map
    /// points themselves.
    class PostLayoutContext
    {
    public:
        /// Built by the layout pass; callbacks receive it.
        PostLayoutContext(ElementStorage& storage, ElementRef ref,
                          IntRect elementRectLayout, IntRect innerRectLayout,
                          float3x2 contentTransform) noexcept
            : m_storage(&storage), m_elementRef(ref),
              m_elementRect(elementRectLayout), m_innerRect(innerRectLayout),
              m_contentTransform(contentTransform) {}

        /// The registered element, its border-box and padding-box, and its contentTransform.
        inline ElementRef Ref()              const noexcept { return m_elementRef; }
        inline IntRect    ElementRect()      const noexcept { return m_elementRect; }
        inline IntRect    ElementInnerRect() const noexcept { return m_innerRect; }
        inline float3x2   ContentTransform() const noexcept { return m_contentTransform; }

        /// Lookups for other elements, as on CustomDrawContext but with rects in layout space.
        IntRect    GetElementRect(const ElementRef& ref) const noexcept;
        IntRect    GetInnerRect(const ElementRef& ref) const noexcept;
        ElementRef GetParentRef(const ElementRef& ref)      const noexcept;
        ElementRef GetNextSiblingRef(const ElementRef& ref) const noexcept;
        int2       GetContentSize(const ElementRef& ref) const noexcept;
        int2       GetInnerSize(const ElementRef& ref) const noexcept;

    private:
        ElementStorage* m_storage;
        ElementRef      m_elementRef;
        IntRect         m_elementRect;
        IntRect         m_innerRect;
        float3x2        m_contentTransform;
    };

    /// @}
} // namespace tachyon
