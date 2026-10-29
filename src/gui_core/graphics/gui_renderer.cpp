#include <gui_core/graphics/gui_renderer.h>

#include <gui_core/style/gui_style.h>
#include <gui_core/style/gui_style_ref.h>

namespace tachyon
{
    void GUIRenderer::RenderBox(tachyon::IntRect rect, tachyon::IntRect clipRect, VisualBoxStyleIndex boxStyleIndex,
                                tachyon::ElementState state, tachyon::VisualColorStyleIndex colorStyleIndex, tachyon::ImageIndex textureIndex,
                                tachyon::IntRect snappedPx)
    {
        uint32_t styleIndexValue = 0;
        if (boxStyleIndex != tachyon::VisualBoxStyleIndex::Invalid)
        {
            // Match StyleManager::GetVisualBoxStyle - collapse Disabled so the
            // GPU pool lookup stays inside this style's 33-slot block.
            state &= tachyon::ElementState::VisualMask;
            if ((state & tachyon::ElementState::Disabled) != tachyon::ElementState::None)
                state = tachyon::ElementState::Disabled;
            styleIndexValue = (((int)boxStyleIndex - 1) * tachyon::Style::kStateCount) + (int)state;
        }

        RenderBox(rect, clipRect, styleIndexValue, styleIndexValue, 1.0f, colorStyleIndex, textureIndex, snappedPx);
    }

    void GUIRenderer::RenderBox(tachyon::IntRect rect, tachyon::IntRect clipRect, VisualBoxStyleIndex boxStyleIndex,
                                tachyon::VisualColorStyleIndex colorStyleIndex, tachyon::ImageIndex textureIndex,
                                tachyon::IntRect snappedPx)
    {
        uint32_t styleIndexValue = 0;
        if (boxStyleIndex != tachyon::VisualBoxStyleIndex::Invalid)
            styleIndexValue = (((int)boxStyleIndex - 1) * tachyon::Style::kStateCount);

        RenderBox(rect, clipRect, styleIndexValue, styleIndexValue, 1.0f, colorStyleIndex, textureIndex, snappedPx);
    }

    void GUIRenderer::RenderFilledPolygon(std::span<const tachyon::float2> points, tachyon::IntRect clipRect,
                                          VisualColorStyleIndex colorIndex, tachyon::ImageIndex textureIndex)
    {
        if (points.size() < 3) return;
        for (size_t i = 1; i + 1 < points.size(); i++)
            RenderTriangle(points[0], points[i], points[i + 1], clipRect, colorIndex, textureIndex);
    }

    void GUIRenderer::RenderPolyline(std::span<const tachyon::int2> points, bool closed,
                                     tachyon::IntRect clipRect, const LineStyle& style)
    {
        if (points.size() < 2) return;
        for (size_t i = 0; i + 1 < points.size(); i++)
            RenderLine(points[i], points[i + 1], clipRect, style);
        if (closed)
            RenderLine(points.back(), points[0], clipRect, style);
    }

} // namespace tachyon
