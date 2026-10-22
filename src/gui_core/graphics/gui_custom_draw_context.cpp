#include <gui_core/graphics/gui_custom_draw_context.h>

#include <gui_core/elements/gui_element_storage.h>
#include <gui_core/graphics/gui_pixel_snap.h>
#include <gui_core/graphics/gui_renderer.h>
#include <gui_core/text/gui_text_system.h>

#include <algorithm>
#include <cmath>

namespace GUI
{
    // subClip arrives in the current space; m_clipRect is in window-local SCREEN space.
    // Map the subClip corners through the current space → window-local transform and take
    // their AABB so the two are comparable before intersection.
    static IntRect MapSubClipToScreen(const float3x2& renderTransform, IntRect subClip) noexcept
    {
        const float2 c0 = renderTransform.TransformPoint({ (float)subClip.x,                (float)subClip.y                });
        const float2 c1 = renderTransform.TransformPoint({ (float)(subClip.x + subClip.w),  (float)subClip.y                });
        const float2 c2 = renderTransform.TransformPoint({ (float)subClip.x,                (float)(subClip.y + subClip.h)  });
        const float2 c3 = renderTransform.TransformPoint({ (float)(subClip.x + subClip.w),  (float)(subClip.y + subClip.h)  });
        const int minX = (int)std::min({ c0.x, c1.x, c2.x, c3.x });
        const int minY = (int)std::min({ c0.y, c1.y, c2.y, c3.y });
        const int maxX = (int)std::max({ c0.x, c1.x, c2.x, c3.x });
        const int maxY = (int)std::max({ c0.y, c1.y, c2.y, c3.y });
        return IntRect{ .position = { minX, minY }, .size = { maxX - minX, maxY - minY } };
    }

    IntRect CustomDrawContext::GetElementRect(const ElementRef& ref) const noexcept
    {
        const auto* er = m_storage->GetElementRectPtr(ref);
        return er ? ToCurrentSpace(er->rect) : IntRect{};
    }

    IntRect CustomDrawContext::GetInnerRect(const ElementRef& ref) const noexcept
    {
        const auto* er = m_storage->GetElementRectPtr(ref);
        return er ? ToCurrentSpace(er->innerRect) : IntRect{};
    }

    ElementRef CustomDrawContext::GetParentRef(const ElementRef& ref) const noexcept
    {
        return m_storage->GetParentRef(ref);
    }

    ElementRef CustomDrawContext::GetNextSiblingRef(const ElementRef& ref) const noexcept
    {
        return m_storage->GetNextSiblingRef(ref);
    }

    int2 CustomDrawContext::GetContentSize(const ElementRef& ref) const noexcept
    {
        const auto* rect = m_storage->GetElementRectPtr(ref);
        if (!rect) return int2{ 0, 0 };
        return rect->contentBounds.size;
    }

    int2 CustomDrawContext::GetInnerSize(const ElementRef& ref) const noexcept
    {
        const auto* rect = m_storage->GetElementRectPtr(ref);
        if (!rect) return int2{ 0, 0 };
        return rect->innerRect.size;
    }

    IntRect CustomDrawContext::SnappedPixelRect(IntRect rect) const
    {
        return SnapToParent(m_ownerPixelRect, m_ownerSnappedRect,
                            PixelBounds(m_currentTransform, rect));
    }

    void CustomDrawContext::RenderBox(IntRect rect, VisualBoxStyleIndex styleIndex, ElementState state,
                                      VisualColorStyleIndex colorStyleIndex, ImageIndex textureIndex) const
    {
        m_renderer->RenderBox(rect, m_clipRect, styleIndex, state, colorStyleIndex, textureIndex, SnappedPixelRect(rect));
    }

    void CustomDrawContext::RenderBox(IntRect rect, VisualBoxStyleIndex styleIndex,
                                      VisualColorStyleIndex colorStyleIndex, ImageIndex textureIndex) const
    {
        m_renderer->RenderBox(rect, m_clipRect, styleIndex, colorStyleIndex, textureIndex, SnappedPixelRect(rect));
    }

    void CustomDrawContext::RenderBox(IntRect rect, IntRect subClip,
                                      VisualBoxStyleIndex styleIndex, ElementState state,
                                      VisualColorStyleIndex colorStyleIndex, ImageIndex textureIndex) const
    {
        const IntRect clip = m_clipRect.Intersection(MapSubClipToScreen(m_currentTransform, subClip));
        m_renderer->RenderBox(rect, clip, styleIndex, state, colorStyleIndex, textureIndex, SnappedPixelRect(rect));
    }

    void CustomDrawContext::RenderBox(IntRect rect, IntRect subClip,
                                      VisualBoxStyleIndex styleIndex,
                                      VisualColorStyleIndex colorStyleIndex, ImageIndex textureIndex) const
    {
        const IntRect clip = m_clipRect.Intersection(MapSubClipToScreen(m_currentTransform, subClip));
        m_renderer->RenderBox(rect, clip, styleIndex, colorStyleIndex, textureIndex, SnappedPixelRect(rect));
    }

    void CustomDrawContext::RenderQuad(IntRect rect, VisualColorStyleIndex colorStyleIndex,
                                       float cornerRadius, ImageIndex textureIndex) const
    {
        m_renderer->RenderQuad(rect, m_clipRect, colorStyleIndex, cornerRadius, textureIndex);
    }

    void CustomDrawContext::RenderQuad(IntRect rect, IntRect subClip,
                                       VisualColorStyleIndex colorStyleIndex,
                                       float cornerRadius, ImageIndex textureIndex) const
    {
        const IntRect clip = m_clipRect.Intersection(MapSubClipToScreen(m_currentTransform, subClip));
        m_renderer->RenderQuad(rect, clip, colorStyleIndex, cornerRadius, textureIndex);
    }

    void CustomDrawContext::RenderLine(int2 p0, int2 p1, const LineStyle& style) const
    {
        m_renderer->RenderLine(p0, p1, m_clipRect, style);
    }

    void CustomDrawContext::RenderLine(int2 p0, int2 p1, int2 p2, int2 p3, const LineStyle& style) const
    {
        m_renderer->RenderLine(p0, p1, p2, p3, m_clipRect, style);
    }

    void CustomDrawContext::RenderDot(float2 pos, float size, color4f color) const
    {
        m_renderer->RenderDot(pos, m_clipRect, size, color);
    }

    void CustomDrawContext::RenderTriangle(float2 p0, float2 p1, float2 p2,
                                           VisualColorStyleIndex colorStyleIndex,
                                           ImageIndex textureIndex) const
    {
        m_renderer->RenderTriangle(p0, p1, p2, m_clipRect, colorStyleIndex, textureIndex);
    }

    void CustomDrawContext::RenderCircle(int2 center, float radius, const LineStyle& style) const
    {
        m_renderer->RenderCircle(center, radius, m_clipRect, style);
    }

    void CustomDrawContext::RenderFilledPolygon(std::span<const float2> points,
                                                VisualColorStyleIndex colorStyleIndex,
                                                ImageIndex textureIndex) const
    {
        if (points.size() < 3) return;
        for (size_t i = 1; i + 1 < points.size(); i++)
            m_renderer->RenderTriangle(points[0], points[i], points[i + 1],
                                       m_clipRect, colorStyleIndex, textureIndex);
    }

    void CustomDrawContext::RenderPolyline(std::span<const int2> points, bool closed,
                                           const LineStyle& style) const
    {
        if (points.size() < 2) return;
        for (size_t i = 0; i + 1 < points.size(); i++)
            m_renderer->RenderLine(points[i], points[i + 1], m_clipRect, style);
        if (closed)
            m_renderer->RenderLine(points.back(), points[0], m_clipRect, style);
    }

    void CustomDrawContext::RenderText(TextID textId, IntRect rect, float extraScale) const
    {
        if (!m_textSystem || textId == TextID::Invalid) return;
        // The text system positions glyphs at rect.position then draws through the
        // renderer's current transform — make sure that matches our current space.
        m_renderer->SetTransform(m_currentTransform);
        const float scale = std::sqrt(m_currentTransform.r0.x * m_currentTransform.r0.x +
                                      m_currentTransform.r0.y * m_currentTransform.r0.y);
        // Editors hit-test screen positions in this space: undo the window offset,
        // then the current space's transform (as ScreenToCurrent does).
        const float3x2 screenToCurrent = m_currentTransform.Inverse()
                                       * float3x2::Translate(int2{ -m_windowOffset.x, -m_windowOffset.y });
        m_textSystem->RenderText(textId, rect, rect, m_clipRect, scale * extraScale, screenToCurrent);
    }

    void CustomDrawContext::SetSpace(DrawSpace space)
    {
        switch (space)
        {
            case DrawSpace::Local:
                m_currentTransform    = m_renderTransform;
                m_currentSpaceIsLocal = true;
                break;
            case DrawSpace::Screen:
                m_currentTransform    = float3x2::Translate(int2{ -m_windowOffset.x, -m_windowOffset.y });
                m_currentSpaceIsLocal = false;
                break;
        }
        m_renderer->SetTransform(m_currentTransform);
    }

    void CustomDrawContext::SetSpaceRaw(float3x2 spaceToWindowLocal)
    {
        m_currentTransform    = spaceToWindowLocal;
        m_currentSpaceIsLocal = false;
        m_renderer->SetTransform(m_currentTransform);
    }

    int2 CustomDrawContext::ScreenToCurrent(int2 screenPoint) const
    {
        // screenPoint is absolute layout (window offset included); m_currentTransform maps
        // current-space → window-local. Strip the offset to reach window-local, then invert.
        const float2 wl{ (float)(screenPoint.x - m_windowOffset.x),
                         (float)(screenPoint.y - m_windowOffset.y) };
        const float2 cp = m_currentTransform.Inverse().TransformPoint(wl);
        return int2{ (int)cp.x, (int)cp.y };
    }


    IntRect PostLayoutContext::GetElementRect(const ElementRef& ref) const noexcept
    {
        const auto* er = m_storage->GetElementRectPtr(ref);
        return er ? er->rect : IntRect{};
    }

    IntRect PostLayoutContext::GetInnerRect(const ElementRef& ref) const noexcept
    {
        const auto* er = m_storage->GetElementRectPtr(ref);
        return er ? er->innerRect : IntRect{};
    }

    ElementRef PostLayoutContext::GetParentRef(const ElementRef& ref) const noexcept
    {
        return m_storage->GetParentRef(ref);
    }

    ElementRef PostLayoutContext::GetNextSiblingRef(const ElementRef& ref) const noexcept
    {
        return m_storage->GetNextSiblingRef(ref);
    }

    int2 PostLayoutContext::GetContentSize(const ElementRef& ref) const noexcept
    {
        const auto* rect = m_storage->GetElementRectPtr(ref);
        if (!rect) return int2{ 0, 0 };
        return rect->contentBounds.size;
    }

    int2 PostLayoutContext::GetInnerSize(const ElementRef& ref) const noexcept
    {
        const auto* rect = m_storage->GetElementRectPtr(ref);
        if (!rect) return int2{ 0, 0 };
        return rect->innerRect.size;
    }

} // namespace GUI
