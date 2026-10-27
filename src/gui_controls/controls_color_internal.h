#pragma once

#include <gui_core/gui_base_types.h>                     // color4f
#include <gui_core/style/gui_style.h>                    // ColorStyle
#include <gui_core/style/gui_style_manager.h>            // StyleManager
#include <gui_core/graphics/gui_custom_draw_context.h>   // VisualColorStyleIndex

#include <algorithm>
#include <cmath>
#include <string_view>

// Helpers shared by the color picker (controls_color.cpp) and gradient editor (controls_gradient.cpp).

namespace tachyon
{
    // How a color shows in a swatch: the left half opaque, the right half at its alpha over
    // a checkerboard, and an alpha bar along the bottom (alphaBar 0 leaves it out).
    struct ColorSwatchLook
    {
        VisualColorStyleIndex checkerA, checkerB;
        VisualColorStyleIndex alphaTrack, alphaFill;
        int32_t               checkerSize = 8;
        int32_t               alphaBar    = 3;
        float                 radius      = 3.0f;   // of the drawn area (half its side draws a circle)
    };

    // What a dialog's header shows after its title: the edited field's name, unless that
    // only repeats the title (a field labelled "Color" in the Color dialog).
    inline std::string_view DialogContext(std::string_view context, std::string_view title)
    {
        auto lower = [](char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; };
        const bool same = context.size() == title.size()
            && std::equal(context.begin(), context.end(), title.begin(), [&](char a, char b) { return lower(a) == lower(b); });
        return same ? std::string_view{} : context;
    }

    // The color picker's hue strip in a color space, top (0) to bottom (1): around the
    // whole hue circle, back to where it started.
    Gradient HueStripGradient(ColorSpace space);

    // A transparency checkerboard over `area` of `rect` (pieces keep rect's rounded corners).
    inline void DrawCheckerboard(const CustomDrawContext& ctx, IntRect rect, IntRect area, const ColorSwatchLook& look)
    {
        const int32_t cs = std::max(1, look.checkerSize);
        for (int32_t y = area.y; y < area.y + area.h; y += cs)
            for (int32_t x = area.x; x < area.x + area.w; x += cs)
            {
                const IntRect square{ .position = { x, y },
                                      .size     = { std::min(cs, area.x + area.w - x), std::min(cs, area.y + area.h - y) } };
                const bool even = (((x - rect.x) / cs + (y - rect.y) / cs) % 2) == 0;
                ctx.RenderQuad(rect, square, even ? look.checkerA : look.checkerB, look.radius);
            }
    }

    // Draws a color into `rect` (the swatch's inside). Every piece is rect's rounded shape
    // clipped to its own area, so the corners stay round.
    inline void DrawColorSwatch(const CustomDrawContext& ctx, IntRect rect, const ColorSwatchLook& look,
                                VisualColorStyleIndex opaqueColor, VisualColorStyleIndex alphaColor, float alpha)
    {
        if (rect.w <= 0 || rect.h <= 0) return;
        const int32_t half = rect.w / 2;
        const IntRect left { .position = rect.position, .size = { half, rect.h } };
        const IntRect right{ .position = { rect.x + half, rect.y }, .size = { rect.w - half, rect.h } };
        DrawCheckerboard(ctx, rect, right, look);
        ctx.RenderQuad(rect, left, opaqueColor, look.radius);
        ctx.RenderQuad(rect, right, alphaColor, look.radius);
        if (look.alphaBar > 0)
        {
            const IntRect track{ .position = { rect.x, rect.y + rect.h - look.alphaBar }, .size = { rect.w, look.alphaBar } };
            const int32_t fillW = int32_t(std::lround(std::clamp(alpha, 0.0f, 1.0f) * float(rect.w)));
            ctx.RenderQuad(rect, track, look.alphaTrack, look.radius);
            if (fillW > 0)
                ctx.RenderQuad(rect, IntRect{ .position = track.position, .size = { fillW, look.alphaBar } }, look.alphaFill, look.radius);
        }
    }

    // Create or update a VisualColorStyleIndex with a solid color.
    inline void SetColorStyle(tachyon::StyleManager& sm, tachyon::VisualColorStyleIndex& idx, tachyon::color4f c)
    {
        tachyon::ColorStyle cs = tachyon::ColorStyle::Create(c);
        if (idx == tachyon::VisualColorStyleIndex::Invalid)
            idx = sm.CreateColorStyle(cs);
        else
            sm.UpdateColorStyle(idx, cs);
    }
}
