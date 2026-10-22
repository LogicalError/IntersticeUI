#include <gui_core/graphics/gui_renderer.h>

#include <gui_core/style/gui_style.h>
#include <gui_core/style/gui_style_ref.h>

namespace GUI
{
    void GUIRenderer::RenderBox(GUI::IntRect rect, GUI::IntRect clipRect, VisualBoxStyleIndex boxStyleIndex,
                                GUI::ElementState state, GUI::VisualColorStyleIndex colorStyleIndex, GUI::ImageIndex textureIndex,
                                GUI::IntRect snappedPx)
    {
        uint32_t styleIndexValue = 0;
        if (boxStyleIndex != GUI::VisualBoxStyleIndex::Invalid)
        {
            // Match StyleManager::GetVisualBoxStyle — collapse Disabled so the
            // GPU pool lookup stays inside this style's 33-slot block.
            state &= GUI::ElementState::VisualMask;
            if ((state & GUI::ElementState::Disabled) != GUI::ElementState::None)
                state = GUI::ElementState::Disabled;
            styleIndexValue = (((int)boxStyleIndex - 1) * GUI::Style::kStateCount) + (int)state;
        }

        RenderBox(rect, clipRect, styleIndexValue, styleIndexValue, 1.0f, colorStyleIndex, textureIndex, snappedPx);
    }

    void GUIRenderer::RenderBox(GUI::IntRect rect, GUI::IntRect clipRect, VisualBoxStyleIndex boxStyleIndex,
                                GUI::VisualColorStyleIndex colorStyleIndex, GUI::ImageIndex textureIndex,
                                GUI::IntRect snappedPx)
    {
        uint32_t styleIndexValue = 0;
        if (boxStyleIndex != GUI::VisualBoxStyleIndex::Invalid)
            styleIndexValue = (((int)boxStyleIndex - 1) * GUI::Style::kStateCount);

        RenderBox(rect, clipRect, styleIndexValue, styleIndexValue, 1.0f, colorStyleIndex, textureIndex, snappedPx);
    }

    void GUIRenderer::RenderCircle(GUI::int2 center, float radius, GUI::IntRect clipRect, const LineStyle& lineStyle)
    {
        static constexpr float k = 0.5522847498f - 0.125f;
        static constexpr float2 segments[4][4] =
        {
            { { 1,  0}, { 1,  k}, { k,  1}, { 0,  1} }, // Right -> Top
            { { 0,  1}, {-k,  1}, {-1,  k}, {-1,  0} }, // Top -> Left
            { {-1,  0}, {-1, -k}, {-k, -1}, { 0, -1} }, // Left -> Bottom
            { { 0, -1}, { k, -1}, { 1, -k}, { 1,  0} }  // Bottom -> Right
        };

        for (int i = 0; i < 4; i++)
        {
            const auto& segment = segments[i];
            const float2 f0 = (segment[0] * radius) + center, f1 = (segment[1] * radius) + center;
            const float2 f2 = (segment[2] * radius) + center, f3 = (segment[3] * radius) + center;
            LineStyle segStyle       = lineStyle;
            segStyle.gradientOffset  = lineStyle.gradientOffset + (float(i)       / 4.0f) * lineStyle.gradientScale;
            segStyle.gradientScale   =                            (1.0f           / 4.0f) * lineStyle.gradientScale;
            RenderLine(int2{ (int)f0.x, (int)f0.y }, int2{ (int)f1.x, (int)f1.y }, int2{ (int)f2.x, (int)f2.y }, int2{ (int)f3.x, (int)f3.y }, clipRect, segStyle);
        }
    }

    void GUIRenderer::RenderFilledPolygon(std::span<const GUI::float2> points, GUI::IntRect clipRect,
                                          VisualColorStyleIndex colorIndex, GUI::ImageIndex textureIndex)
    {
        if (points.size() < 3) return;
        for (size_t i = 1; i + 1 < points.size(); i++)
            RenderTriangle(points[0], points[i], points[i + 1], clipRect, colorIndex, textureIndex);
    }

    void GUIRenderer::RenderPolyline(std::span<const GUI::int2> points, bool closed,
                                     GUI::IntRect clipRect, const LineStyle& style)
    {
        if (points.size() < 2) return;
        for (size_t i = 0; i + 1 < points.size(); i++)
            RenderLine(points[i], points[i + 1], clipRect, style);
        if (closed)
            RenderLine(points.back(), points[0], clipRect, style);
    }

} // namespace GUI
