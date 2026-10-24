#include <gui_controls/gui_application.h>
#include <gui_controls/controls_color_internal.h>   // SetColorStyle (shared with controls_gradient.cpp)
#include <gui_core/style/gui_color_conversions.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace
{

    std::string ColorToHex(float r, float g, float b)
    {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "#%02X%02X%02X",
            std::clamp(int(r * 255.0f + 0.5f), 0, 255),
            std::clamp(int(g * 255.0f + 0.5f), 0, 255),
            std::clamp(int(b * 255.0f + 0.5f), 0, 255));
        return buf;
    }

    bool HexToColor(const std::string& hex, float& r, float& g, float& b)
    {
        const char* s = hex.c_str();
        if (*s == '#') s++;
        if (std::strlen(s) != 6) return false;
        auto hexDigit = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        int digits[6];
        for (int i = 0; i < 6; i++) {
            digits[i] = hexDigit(s[i]);
            if (digits[i] < 0) return false;
        }
        r = (digits[0] * 16 + digits[1]) / 255.0f;
        g = (digits[2] * 16 + digits[3]) / 255.0f;
        b = (digits[4] * 16 + digits[5]) / 255.0f;
        return true;
    }


    // Create a horizontal linear gradient ColorStyle from two endpoint colors.
    void SetGradientStyle2(interstice::StyleManager& sm, interstice::VisualColorStyleIndex& idx,
                           interstice::color4f c0, interstice::color4f c1)
    {
        interstice::ColorStyle cs = interstice::ColorStyle::Create(c0, c1, interstice::ColorType::LinearGradient);
        // Rotate 90° so gradient goes left→right instead of top→bottom
        cs.SetTransformation(interstice::float3(0, 1, 0), interstice::float3(1, 0, 0));
        if (idx == interstice::VisualColorStyleIndex::Invalid)
            idx = sm.CreateColorStyle(cs);
        else
            sm.UpdateColorStyle(idx, cs);
    }

    // Create a horizontal multi-stop gradient for the hue slider.
    void SetHueGradientStyleHorizontal(interstice::StyleManager& sm, interstice::VisualColorStyleIndex& idx)
    {
        interstice::Gradient g;
        g.type = interstice::GradientType::Linear;
        constexpr float hues[] = { 0.0f, 1.0f/6, 2.0f/6, 3.0f/6, 4.0f/6, 5.0f/6, 1.0f };
        for (int i = 0; i < 7; i++)
        {
            auto [r, gb, b] = interstice::ColorConvert::HSVtoRGB(hues[i], 1.0f, 1.0f);
            g.AddStop(interstice::color4f{r, gb, b, 1.0f}, hues[i]);
        }
        interstice::ColorStyle cs = interstice::ColorStyle::Create(g, interstice::ColorType::LinearGradient);
        cs.SetTransformation(interstice::float3(0, 1, 0), interstice::float3(1, 0, 0)); // horizontal
        if (idx == interstice::VisualColorStyleIndex::Invalid)
            idx = sm.CreateColorStyle(cs);
        else
            sm.UpdateColorStyle(idx, cs);
    }

    // Create a 4-corner InterpolatedBox color style.
    void SetQuadColorStyle(interstice::StyleManager& sm, interstice::VisualColorStyleIndex& idx,
                           interstice::color4f c0, interstice::color4f c1, interstice::color4f c2, interstice::color4f c3)
    {
        interstice::ColorStyle cs = interstice::ColorStyle::Create(c0, c1, c2, c3, interstice::ColorType::InterpolatedBox);
        if (idx == interstice::VisualColorStyleIndex::Invalid)
            idx = sm.CreateColorStyle(cs);
        else
            sm.UpdateColorStyle(idx, cs);
    }


    // Create a horizontal multi-stop gradient by sampling a lambda at N points.
    template <typename Fn>
    void SetSampledGradient(interstice::StyleManager& sm, interstice::VisualColorStyleIndex& idx,
                            Fn&& sampleFn, int numStops = 8)
    {
        interstice::Gradient g;
        g.type = interstice::GradientType::Linear;
        for (int i = 0; i < numStops; i++)
        {
            float t = float(i) / float(numStops - 1);
            g.AddStop(sampleFn(t), t);
        }
        interstice::ColorStyle cs = interstice::ColorStyle::Create(g, interstice::ColorType::LinearGradient);
        cs.SetTransformation(interstice::float3(0, 1, 0), interstice::float3(1, 0, 0)); // horizontal
        if (idx == interstice::VisualColorStyleIndex::Invalid)
            idx = sm.CreateColorStyle(cs);
        else
            sm.UpdateColorStyle(idx, cs);
    }
}


namespace interstice
{

    // ---------------------------------------------------------------------------
    // Color picker dialog: emitted while ColorPickerSubsystem's triggerID is open.
    //   left:  saturation/value square, hue strip, alpha strip
    //   right: new/original preview, color space + range, channel sliders, hex, HDR
    //   below: swatches and recent colors; footer with Cancel / OK
    // ---------------------------------------------------------------------------

    namespace
    {
        constexpr float kPi = 3.14159265f;

        // Hue position [0,1] → color, for the hue strip of each space.
        color4f SpaceHueColor(ColorSpace space, float hue)
        {
            switch (space)
            {
                case ColorSpace::Lab:
                {
                    const float angle = hue * 2.0f * kPi;
                    auto [r, g, b] = ColorConvert::LabToRGB(65.0f, 80.0f * std::cos(angle), 80.0f * std::sin(angle));
                    return { r, g, b, 1.0f };
                }
                case ColorSpace::Oklab:
                {
                    const float angle = hue * 2.0f * kPi;
                    auto [r, g, b] = ColorConvert::OklabToRGB(0.7f, 0.2f * std::cos(angle), 0.2f * std::sin(angle));
                    return { r, g, b, 1.0f };
                }
                default:
                {
                    auto [r, g, b] = ColorConvert::HSVtoRGB(hue, 1.0f, 1.0f);
                    return { r, g, b, 1.0f };
                }
            }
        }

        // A top-to-bottom gradient sampled at N points.
        template <typename Fn>
        void SetVerticalGradient(StyleManager& sm, VisualColorStyleIndex& idx, Fn&& sampleFn, int numStops)
        {
            Gradient g;
            g.type = GradientType::Linear;
            for (int i = 0; i < numStops; i++)
            {
                const float t = float(i) / float(numStops - 1);
                g.AddStop(sampleFn(t), t);
            }
            const ColorStyle cs = ColorStyle::Create(g, ColorType::LinearGradient);
            if (idx == VisualColorStyleIndex::Invalid) idx = sm.CreateColorStyle(cs);
            else                                       sm.UpdateColorStyle(idx, cs);
        }

        // Rebuilds h/s/v from a hue position on the strip, keeping the space's other axes.
        void ApplyStripHue(ColorSpace space, float hue01, float& h, float& s, float& v)
        {
            switch (space)
            {
                default:
                    h = std::clamp(hue01, 0.0f, 1.0f);
                    break;
                case ColorSpace::HSL:
                {
                    auto [cr, cg, cb] = ColorConvert::HSVtoRGB(h, s, v);
                    float oldH, oldS, oldL;
                    ColorConvert::RGBtoHSL(cr, cg, cb, oldH, oldS, oldL);
                    auto [nr, ng, nb] = ColorConvert::HSLtoRGB(hue01, oldS, oldL);
                    ColorConvert::RGBtoHSV(nr, ng, nb, h, s, v);
                    break;
                }
                case ColorSpace::Lab:
                case ColorSpace::Oklab:
                {
                    auto [cr, cg, cb] = ColorConvert::HSVtoRGB(h, s, v);
                    float L, a, b;
                    if (space == ColorSpace::Lab) ColorConvert::RGBtoLab(cr, cg, cb, L, a, b);
                    else                          ColorConvert::RGBtoOklab(cr, cg, cb, L, a, b);
                    const float chroma = std::sqrt(a * a + b * b);
                    const float angle  = hue01 * 2.0f * kPi;
                    auto [nr, ng, nb] = (space == ColorSpace::Lab)
                        ? ColorConvert::LabToRGB(L, chroma * std::cos(angle), chroma * std::sin(angle))
                        : ColorConvert::OklabToRGB(L, chroma * std::cos(angle), chroma * std::sin(angle));
                    ColorConvert::RGBtoHSV(nr, ng, nb, h, s, v);
                    break;
                }
            }
        }

        // Rebuilds h/s/v from a point on the square (sx right, sy down), keeping the hue.
        void ApplySquare(ColorSpace space, float sx, float sy, float& h, float& s, float& v)
        {
            sx = std::clamp(sx, 0.0f, 1.0f);
            sy = std::clamp(sy, 0.0f, 1.0f);
            switch (space)
            {
                default:
                    s = sx;
                    v = 1.0f - sy;
                    break;
                case ColorSpace::HSL:
                {
                    auto [cr, cg, cb] = ColorConvert::HSVtoRGB(h, s, v);
                    float oldH, oldS, oldL;
                    ColorConvert::RGBtoHSL(cr, cg, cb, oldH, oldS, oldL);
                    auto [nr, ng, nb] = ColorConvert::HSLtoRGB(oldH, sx, 1.0f - sy);
                    ColorConvert::RGBtoHSV(nr, ng, nb, h, s, v);
                    break;
                }
                case ColorSpace::Lab:
                case ColorSpace::Oklab:
                {
                    auto [cr, cg, cb] = ColorConvert::HSVtoRGB(h, s, v);
                    float L, a, b;
                    if (space == ColorSpace::Lab) ColorConvert::RGBtoLab(cr, cg, cb, L, a, b);
                    else                          ColorConvert::RGBtoOklab(cr, cg, cb, L, a, b);
                    const float angle = std::atan2(b, a);
                    auto [nr, ng, nb] = (space == ColorSpace::Lab)
                        ? ColorConvert::LabToRGB((1.0f - sy) * 100.0f, sx * 128.0f * std::cos(angle), sx * 128.0f * std::sin(angle))
                        : ColorConvert::OklabToRGB(1.0f - sy, sx * 0.4f * std::cos(angle), sx * 0.4f * std::sin(angle));
                    ColorConvert::RGBtoHSV(nr, ng, nb, h, s, v);
                    break;
                }
            }
        }

        // A cursor ring with a thin dark outline, as on the square.
        void DrawRing(const CustomDrawContext& ctx, int2 center, float radius, float width,
                      VisualColorStyleIndex ring, VisualColorStyleIndex outline, float outlineWidth)
        {
            ctx.RenderCircle(center, radius + width * 0.5f + outlineWidth * 0.5f, LineStyle{ .colorStyleIndex = outline, .lineWidth = outlineWidth });
            ctx.RenderCircle(center, radius, LineStyle{ .colorStyleIndex = ring, .lineWidth = width });
        }

        // A filled disc (a circle stroked as wide as it is), which stays round at small sizes.
        void DrawDisc(const CustomDrawContext& ctx, int2 center, float radius, VisualColorStyleIndex color)
        {
            ctx.RenderCircle(center, radius * 0.5f, LineStyle{ .colorStyleIndex = color, .lineWidth = radius });
        }
    }

    void GUIApplication::EmitColorPicker(color4f& outColor, bool allowHDR)
    {
        auto& cp = m_colorPickerSub->State();

        // ---- Resolve slider edits from last frame's input first ----
        // Channel handlers edit cp.comp[]; convert back to HSV before rebuilding the styles.
        if (cp.slidersInitialized
            && (cp.comp[0] != cp.snapComp[0] || cp.comp[1] != cp.snapComp[1] || cp.comp[2] != cp.snapComp[2]))
        {
            switch (cp.activeSpace)
            {
                case ColorSpace::HSV:
                    cp.h = std::clamp(cp.comp[0], 0.f, 1.f);
                    cp.s = std::clamp(cp.comp[1], 0.f, 1.f);
                    cp.v = std::clamp(cp.comp[2], 0.f, 1.f);
                    break;
                case ColorSpace::HSL:
                {
                    auto [r, g, b] = ColorConvert::HSLtoRGB(std::clamp(cp.comp[0], 0.f, 1.f), std::clamp(cp.comp[1], 0.f, 1.f), std::clamp(cp.comp[2], 0.f, 1.f));
                    ColorConvert::RGBtoHSV(r, g, b, cp.h, cp.s, cp.v);
                    break;
                }
                case ColorSpace::sRGB:
                    ColorConvert::RGBtoHSV(std::clamp(cp.comp[0], 0.f, 1.f), std::clamp(cp.comp[1], 0.f, 1.f), std::clamp(cp.comp[2], 0.f, 1.f),
                                           cp.h, cp.s, cp.v);
                    break;
                case ColorSpace::LinearRGB:
                    ColorConvert::RGBtoHSV(ColorConvert::LinearToSRGB(std::clamp(cp.comp[0], 0.f, 1.f)),
                                           ColorConvert::LinearToSRGB(std::clamp(cp.comp[1], 0.f, 1.f)),
                                           ColorConvert::LinearToSRGB(std::clamp(cp.comp[2], 0.f, 1.f)), cp.h, cp.s, cp.v);
                    break;
                case ColorSpace::Lab:
                {
                    auto [r, g, b] = ColorConvert::LabToRGB(cp.comp[0], cp.comp[1], cp.comp[2]);
                    ColorConvert::RGBtoHSV(r, g, b, cp.h, cp.s, cp.v);
                    break;
                }
                case ColorSpace::Oklab:
                {
                    auto [r, g, b] = ColorConvert::OklabToRGB(cp.comp[0], cp.comp[1], cp.comp[2]);
                    ColorConvert::RGBtoHSV(r, g, b, cp.h, cp.s, cp.v);
                    break;
                }
            }
        }

        // ---- The square's axes and the strip's hue in the active space ----
        //   stripHue [0,1] top to bottom; squareX [0,1] left to right; squareY [0,1] bottom to top.
        auto [curR, curG, curB] = ColorConvert::HSVtoRGB(cp.h, cp.s, cp.v);
        float stripHue = cp.h, squareX = cp.s, squareY = cp.v;
        switch (cp.activeSpace)
        {
            default: break;
            case ColorSpace::HSL:
            {
                float hh, ss, ll;
                ColorConvert::RGBtoHSL(curR, curG, curB, hh, ss, ll);
                stripHue = hh; squareX = ss; squareY = ll;
                break;
            }
            case ColorSpace::Lab:
            case ColorSpace::Oklab:
            {
                float L, a, b;
                if (cp.activeSpace == ColorSpace::Lab) ColorConvert::RGBtoLab(curR, curG, curB, L, a, b);
                else                                   ColorConvert::RGBtoOklab(curR, curG, curB, L, a, b);
                stripHue = std::atan2(b, a) / (2.0f * kPi);
                if (stripHue < 0) stripHue += 1.0f;
                const float chroma = std::sqrt(a * a + b * b);
                squareX = std::clamp(chroma / (cp.activeSpace == ColorSpace::Lab ? 128.0f : 0.4f), 0.0f, 1.0f);
                squareY = std::clamp(cp.activeSpace == ColorSpace::Lab ? L / 100.0f : L, 0.0f, 1.0f);
                break;
            }
        }

        // ---- Color styles: square corners, strips, preview, cursors ----
        {
            auto corner = [&](float sx, float sy) -> color4f {
                switch (cp.activeSpace)
                {
                    case ColorSpace::HSL:
                    {
                        auto [r, g, b] = ColorConvert::HSLtoRGB(stripHue, sx, 1.0f - sy);
                        return { r, g, b, 1.0f };
                    }
                    case ColorSpace::Lab:
                    {
                        const float angle = stripHue * 2.0f * kPi;
                        auto [r, g, b] = ColorConvert::LabToRGB((1.0f - sy) * 100.0f, sx * 128.0f * std::cos(angle), sx * 128.0f * std::sin(angle));
                        return { r, g, b, 1.0f };
                    }
                    case ColorSpace::Oklab:
                    {
                        const float angle = stripHue * 2.0f * kPi;
                        auto [r, g, b] = ColorConvert::OklabToRGB(1.0f - sy, sx * 0.4f * std::cos(angle), sx * 0.4f * std::sin(angle));
                        return { r, g, b, 1.0f };
                    }
                    default:
                    {
                        auto [r, g, b] = ColorConvert::HSVtoRGB(stripHue, sx, 1.0f - sy);
                        return { r, g, b, 1.0f };
                    }
                }
            };
            SetQuadColorStyle(styleManager, cp.svQuadColor, corner(0, 0), corner(1, 0), corner(1, 1), corner(0, 1));
        }
        const ColorSpace space = cp.activeSpace;
        SetVerticalGradient(styleManager, cp.hueStrip, [space](float t) { return SpaceHueColor(space, t); }, Gradient::kMaxStops);
        SetVerticalGradient(styleManager, cp.alphaStrip, [&](float t) { return color4f{ curR, curG, curB, 1.0f - t }; }, 2);
        SetColorStyle(styleManager, cp.hueColor, SpaceHueColor(space, stripHue));
        SetColorStyle(styleManager, cp.whiteColor, { 1.0f, 1.0f, 1.0f, 1.0f });
        SetColorStyle(styleManager, cp.outlineColor, { 0.0f, 0.0f, 0.0f, 0.5f });
        SetColorStyle(styleManager, cp.newColor, { curR, curG, curB, cp.a });
        SetColorStyle(styleManager, cp.originalStyle, { std::clamp(cp.originalColor.r, 0.f, 1.f), std::clamp(cp.originalColor.g, 0.f, 1.f),
                                                        std::clamp(cp.originalColor.b, 0.f, 1.f), cp.originalColor.a });

        const ColorSwatchLook look = SwatchLook();
        const auto requestClose = [this](ColorPickerState::CloseRequest request) {
            return [this, request]() { m_colorPickerSub->State().closeRequest = request; };
        };

        // ---- Header: title and close ----
        // The header is the dialog's title bar: it drags the window.
        BeginTitleBar(idManager.AllocateDynamicID("cp_header"), theme.dialogHeaderStyleRef, LayoutOverride{}.NoShrink());
        Text(idManager.AllocateDynamicID("cp_title"), "Color", theme.dialogTitleStyleRef);
        Text(idManager.AllocateDynamicID("cp_context"), cp.context, theme.dialogContextStyleRef, LayoutOverride{}.FlexGrow(1));
        Button(idManager.AllocateDynamicID("cp_close"), theme.tabCloseIcon, {}, requestClose(ColorPickerState::CloseRequest::Cancel),
               theme.dialogCloseStyleRef);
        SetTooltip("Close without changing the color");
        EndTitleBar();

        BeginHorizontal(idManager.AllocateDynamicID("cp_body"), theme.colorPickerBodyStyleRef, LayoutOverride{}.NoShrink());

        // ---- Left: the square and the strips ----
        BeginHorizontal(idManager.AllocateDynamicID("cp_planes"), theme.colorPickerPlanesStyleRef, LayoutOverride{}.NoShrink());
        {
            // One handler for the three: a press picks the target, the drag follows it.
            auto planeHandler = [this](ColorPickerState::DragTarget target, ID id) {
                return [this, target, id](ElementEvent&& event) -> bool {
                    auto& cp = m_colorPickerSub->State();
                    auto apply = [&]() {
                        if (!event.rect || event.rect->rect.w <= 0 || event.rect->rect.h <= 0) return;
                        const IntRect& r = event.rect->rect;
                        const int2 c = event.LocalCursor(elementStorage);
                        const float fx = float(c.x - r.x) / float(r.w);
                        const float fy = std::clamp(float(c.y - r.y) / float(r.h), 0.0f, 1.0f);
                        switch (cp.drag)
                        {
                            case ColorPickerState::DragTarget::Square: ApplySquare(cp.activeSpace, fx, fy, cp.h, cp.s, cp.v); break;
                            case ColorPickerState::DragTarget::Hue:    ApplyStripHue(cp.activeSpace, fy, cp.h, cp.s, cp.v);  break;
                            case ColorPickerState::DragTarget::Alpha:  cp.a = 1.0f - fy; break;
                            default: break;
                        }
                    };
                    switch (event.type)
                    {
                        case EventType::MouseDown:
                            if (!event.state.LeftPressed()) break;
                            cp.drag = target;
                            guiStateManager.Interaction().SetCaptureID(id);
                            apply();
                            return true;
                        case EventType::MouseMove:
                            if (cp.drag != target || !event.state.LeftDown()) break;
                            apply();
                            return true;
                        case EventType::MouseUp:
                            if (cp.drag != target) break;
                            cp.drag = ColorPickerState::DragTarget::None;
                            guiStateManager.Interaction().ReleaseCaptureID();
                            return true;
                        default: break;
                    }
                    return false;
                };
            };

            // Saturation/value (per space) square with its cursor ring.
            const ID squareID = idManager.AllocateDynamicID("cp_square");
            const ElementRef squareRef = RegisterControl(Element{ .id = squareID,
                .eventHandler = planeHandler(ColorPickerState::DragTarget::Square, squareID), .styleRef = theme.colorPickerSquareStyleRef },
                LayoutOverride{});
            VERIFY(elementStorage.AddCustomDraw(squareRef,
                [quad = cp.svQuadColor, white = cp.whiteColor, outline = cp.outlineColor, squareX, squareY,
                 radius = float(styleManager.Px(6)), ring = float(styleManager.Px(6)), width = float(styleManager.Px(2)), edge = float(styleManager.Px(1))]
                (CustomDrawContext& ctx)
                {
                    const IntRect r = ctx.ElementRect();
                    ctx.RenderQuad(r, quad, radius);
                    const int2 at{ r.x + int(std::lround(squareX * float(r.w))), r.y + int(std::lround((1.0f - squareY) * float(r.h))) };
                    DrawRing(ctx, at, ring, width, white, outline, edge);
                }));

            // The hue and alpha strips, each with a bar handle.
            auto strip = [&](const char* name, ColorPickerState::DragTarget target, VisualColorStyleIndex fill, bool checker,
                             float position, VisualColorStyleIndex handleFill)
            {
                const ID stripID = idManager.AllocateDynamicID(name);
                const ElementRef stripRef = RegisterControl(Element{ .id = stripID,
                    .eventHandler = planeHandler(target, stripID), .styleRef = theme.colorPickerStripStyleRef },
                    LayoutOverride{});
                VERIFY(elementStorage.AddCustomDraw(stripRef,
                    [fill, checker, position, handleFill, look, white = cp.whiteColor, outline = cp.outlineColor,
                     radius = float(styleManager.Px(4)), overhang = styleManager.Px(3), handleH = styleManager.Px(6),
                     border = styleManager.Px(2), edge = styleManager.Px(1)](CustomDrawContext& ctx)
                    {
                        const IntRect r = ctx.ElementRect();
                        if (checker)
                        {
                            ColorSwatchLook strip = look;
                            strip.radius = radius;
                            DrawCheckerboard(ctx, r, r, strip);
                        }
                        ctx.RenderQuad(r, fill, radius);
                        // The handle: a white-edged bar across the strip, outlined in dark.
                        const int y = r.y + int(std::lround(position * float(r.h)));
                        const IntRect bar{ .position = { r.x - overhang, y - handleH / 2 }, .size = { r.w + 2 * overhang, handleH } };
                        const IntRect outer{ .position = { bar.x - edge, bar.y - edge }, .size = { bar.w + 2 * edge, bar.h + 2 * edge } };
                        const IntRect inner{ .position = { bar.x + border, bar.y + border }, .size = { bar.w - 2 * border, std::max(0, bar.h - 2 * border) } };
                        ctx.RenderQuad(outer, outline, float(handleH / 2 + edge));
                        ctx.RenderQuad(bar, white, float(handleH / 2));
                        if (inner.h > 0) ctx.RenderQuad(inner, handleFill, 0.0f);
                    }));
            };
            strip("cp_hue", ColorPickerState::DragTarget::Hue, cp.hueStrip, false, stripHue, cp.hueColor);
            strip("cp_alpha", ColorPickerState::DragTarget::Alpha, cp.alphaStrip, true, 1.0f - cp.a, cp.newColor);
        }
        EndHorizontal();

        // ---- Right: preview, space, channels, hex, HDR ----
        BeginVertical(idManager.AllocateDynamicID("cp_controls"), theme.colorPickerControlsStyleRef, LayoutOverride{}.FlexGrow(1));
        {
            // New | Original, on the checkerboard.
            BeginVertical(idManager.AllocateDynamicID("cp_preview_group"), theme.colorPickerPreviewGroupStyleRef, LayoutOverride{}.NoShrink());
            {
                const ID previewID = idManager.AllocateDynamicID("cp_preview");
                Element preview{ .id = previewID, .styleRef = theme.colorPickerPreviewStyleRef, .flags = ElementFlags::NoInput };
                const ElementRef previewRef = RegisterElement(std::move(preview), LayoutOverride{});
                VERIFY(elementStorage.AddCustomDraw(previewRef,
                    [look, newColor = cp.newColor, original = cp.originalStyle, radius = float(styleManager.Px(5))](CustomDrawContext& ctx)
                    {
                        const IntRect r = ctx.ElementInnerRect();
                        ColorSwatchLook preview = look;
                        preview.radius = radius;
                        DrawCheckerboard(ctx, r, r, preview);
                        const int half = r.w / 2;
                        ctx.RenderQuad(r, IntRect{ .position = r.position, .size = { half, r.h } }, newColor, radius);
                        ctx.RenderQuad(r, IntRect{ .position = { r.x + half, r.y }, .size = { r.w - half, r.h } }, original, radius);
                    }));
                BeginHorizontal(idManager.AllocateDynamicID("cp_preview_captions"), theme.colorPickerCaptionsStyleRef, LayoutOverride{});
                Text(idManager.AllocateDynamicID("cp_new"), "New", theme.colorPickerCaptionStyleRef, LayoutOverride{}.FlexGrow(1));
                Text(idManager.AllocateDynamicID("cp_original"), "Original", theme.colorPickerCaptionStyleRef);
                EndHorizontal();
            }
            EndVertical();

            // Color space, Linear (for RGB) and the value range.
            BeginHorizontal(idManager.AllocateDynamicID("cp_mode_row"), theme.colorPickerModeRowStyleRef, LayoutOverride{}.NoShrink());
            {
                const bool rgb = (cp.activeSpace == ColorSpace::sRGB || cp.activeSpace == ColorSpace::LinearRGB);
                struct SpaceTab { ColorSpace space; std::string_view label; };
                constexpr SpaceTab tabs[] = {
                    { ColorSpace::sRGB, "RGB" }, { ColorSpace::HSV, "HSV" }, { ColorSpace::HSL, "HSL" },
                    { ColorSpace::Lab, "Lab" }, { ColorSpace::Oklab, "Oklab" },
                };
                BeginButtonGroup(LayoutOverride{}.NoGrow().NoShrink());
                for (const SpaceTab& tab : tabs)
                {
                    const bool active = (tab.space == ColorSpace::sRGB) ? rgb : (cp.activeSpace == tab.space);
                    if (active) guiStateManager.Construction().PushSelected();
                    const ColorSpace target = tab.space;
                    Button(tab.label, [this, target, rgb]() {
                        auto& cp = m_colorPickerSub->State();
                        if (target != ColorSpace::sRGB || !rgb)   // RGB keeps its Linear choice
                            cp.activeSpace = target;
                    });
                    if (active) guiStateManager.Construction().PopSelected();
                }
                EndButtonGroup();

                Element spacer{ .id = idManager.AllocateDynamicID("cp_mode_spacer"), .styleRef = theme.emptyHorzStyleRef, .flags = ElementFlags::NoInput };
                RegisterElement(std::move(spacer), LayoutOverride{}.FlexGrow(1));

                Button(idManager.AllocateDynamicID("cp_range"), IconID::Invalid, cp.byteRange ? "0\xE2\x80\x93" "255" : "0\xE2\x80\x93" "1",
                       [this]() { auto& cp = m_colorPickerSub->State(); cp.byteRange = !cp.byteRange; },
                       theme.colorPickerRangeStyleRef);
                SetTooltip("Show the values 0-255 or 0-1");
            }
            EndHorizontal();

            // Channel sliders: label, gradient track with a round thumb, value.
            auto& gs = cp.gradientStyles;
            std::string_view labels[3];
            float mins[3] = { 0, 0, 0 }, maxs[3] = { 1, 1, 1 };
            float scales[3] = { 1, 1, 1 };   // shown value = component * scale (byte range)
            const float byte = cp.byteRange ? 255.0f : 1.0f;
            const float percent = cp.byteRange ? 100.0f : 1.0f;
            switch (cp.activeSpace)
            {
                case ColorSpace::HSV:
                {
                    cp.comp[0] = cp.h; cp.comp[1] = cp.s; cp.comp[2] = cp.v;
                    labels[0] = "H"; labels[1] = "S"; labels[2] = "V";
                    scales[0] = cp.byteRange ? 360.0f : 1.0f; scales[1] = scales[2] = percent;
                    SetHueGradientStyleHorizontal(styleManager, gs[0]);
                    auto [s0r, s0g, s0b] = ColorConvert::HSVtoRGB(cp.h, 0.0f, cp.v);
                    auto [s1r, s1g, s1b] = ColorConvert::HSVtoRGB(cp.h, 1.0f, cp.v);
                    SetGradientStyle2(styleManager, gs[1], { s0r, s0g, s0b, 1 }, { s1r, s1g, s1b, 1 });
                    auto [v1r, v1g, v1b] = ColorConvert::HSVtoRGB(cp.h, cp.s, 1.0f);
                    SetGradientStyle2(styleManager, gs[2], { 0, 0, 0, 1 }, { v1r, v1g, v1b, 1 });
                    break;
                }
                case ColorSpace::HSL:
                {
                    float hh, ss, ll;
                    ColorConvert::RGBtoHSL(curR, curG, curB, hh, ss, ll);
                    cp.comp[0] = hh; cp.comp[1] = ss; cp.comp[2] = ll;
                    labels[0] = "H"; labels[1] = "S"; labels[2] = "L";
                    scales[0] = cp.byteRange ? 360.0f : 1.0f; scales[1] = scales[2] = percent;
                    SetHueGradientStyleHorizontal(styleManager, gs[0]);
                    auto [s0r, s0g, s0b] = ColorConvert::HSLtoRGB(hh, 0.0f, ll);
                    auto [s1r, s1g, s1b] = ColorConvert::HSLtoRGB(hh, 1.0f, ll);
                    SetGradientStyle2(styleManager, gs[1], { s0r, s0g, s0b, 1 }, { s1r, s1g, s1b, 1 });
                    SetSampledGradient(styleManager, gs[2], [hh, ss](float t) -> color4f {
                        auto [r, g, b] = ColorConvert::HSLtoRGB(hh, ss, t);
                        return { r, g, b, 1.0f };
                    });
                    break;
                }
                case ColorSpace::sRGB:
                    cp.comp[0] = curR; cp.comp[1] = curG; cp.comp[2] = curB;
                    labels[0] = "R"; labels[1] = "G"; labels[2] = "B";
                    scales[0] = scales[1] = scales[2] = byte;
                    SetGradientStyle2(styleManager, gs[0], { 0, curG, curB, 1 }, { 1, curG, curB, 1 });
                    SetGradientStyle2(styleManager, gs[1], { curR, 0, curB, 1 }, { curR, 1, curB, 1 });
                    SetGradientStyle2(styleManager, gs[2], { curR, curG, 0, 1 }, { curR, curG, 1, 1 });
                    break;
                case ColorSpace::LinearRGB:
                {
                    const float lr = ColorConvert::SRGBToLinear(curR), lg = ColorConvert::SRGBToLinear(curG), lb = ColorConvert::SRGBToLinear(curB);
                    cp.comp[0] = lr; cp.comp[1] = lg; cp.comp[2] = lb;
                    labels[0] = "R"; labels[1] = "G"; labels[2] = "B";
                    scales[0] = scales[1] = scales[2] = byte;
                    auto toS = [](float x) { return ColorConvert::LinearToSRGB(x); };
                    SetGradientStyle2(styleManager, gs[0], { toS(0), toS(lg), toS(lb), 1 }, { toS(1), toS(lg), toS(lb), 1 });
                    SetGradientStyle2(styleManager, gs[1], { toS(lr), toS(0), toS(lb), 1 }, { toS(lr), toS(1), toS(lb), 1 });
                    SetGradientStyle2(styleManager, gs[2], { toS(lr), toS(lg), toS(0), 1 }, { toS(lr), toS(lg), toS(1), 1 });
                    break;
                }
                case ColorSpace::Lab:
                {
                    float L, a, b;
                    ColorConvert::RGBtoLab(curR, curG, curB, L, a, b);
                    cp.comp[0] = L; cp.comp[1] = a; cp.comp[2] = b;
                    labels[0] = "L"; labels[1] = "a"; labels[2] = "b";
                    mins[0] = 0; maxs[0] = 100; mins[1] = -128; maxs[1] = 128; mins[2] = -128; maxs[2] = 128;
                    SetSampledGradient(styleManager, gs[0], [a, b](float t) -> color4f { auto [r, g, bb] = ColorConvert::LabToRGB(t * 100.0f, a, b); return { r, g, bb, 1.0f }; });
                    SetSampledGradient(styleManager, gs[1], [L, b](float t) -> color4f { auto [r, g, bb] = ColorConvert::LabToRGB(L, -128.0f + t * 256.0f, b); return { r, g, bb, 1.0f }; });
                    SetSampledGradient(styleManager, gs[2], [L, a](float t) -> color4f { auto [r, g, bb] = ColorConvert::LabToRGB(L, a, -128.0f + t * 256.0f); return { r, g, bb, 1.0f }; });
                    break;
                }
                case ColorSpace::Oklab:
                {
                    float L, a, b;
                    ColorConvert::RGBtoOklab(curR, curG, curB, L, a, b);
                    cp.comp[0] = L; cp.comp[1] = a; cp.comp[2] = b;
                    labels[0] = "L"; labels[1] = "a"; labels[2] = "b";
                    mins[0] = 0; maxs[0] = 1; mins[1] = -0.5f; maxs[1] = 0.5f; mins[2] = -0.5f; maxs[2] = 0.5f;
                    SetSampledGradient(styleManager, gs[0], [a, b](float t) -> color4f { auto [r, g, bb] = ColorConvert::OklabToRGB(t, a, b); return { r, g, bb, 1.0f }; });
                    SetSampledGradient(styleManager, gs[1], [L, b](float t) -> color4f { auto [r, g, bb] = ColorConvert::OklabToRGB(L, -0.5f + t, b); return { r, g, bb, 1.0f }; });
                    SetSampledGradient(styleManager, gs[2], [L, a](float t) -> color4f { auto [r, g, bb] = ColorConvert::OklabToRGB(L, a, -0.5f + t); return { r, g, bb, 1.0f }; });
                    break;
                }
            }
            SetGradientStyle2(styleManager, gs[3], { curR, curG, curB, 0 }, { curR, curG, curB, 1 });

            BeginVertical(idManager.AllocateDynamicID("cp_channels"), theme.colorPickerChannelsStyleRef, LayoutOverride{}.NoShrink());
            auto channel = [&](std::string_view label, float& value, float minVal, float maxVal, float scale,
                               VisualColorStyleIndex fill, bool checker)
            {
                BeginHorizontal(idManager.AllocateDynamicID("cp_channel"), theme.colorPickerChannelStyleRef, LayoutOverride{}.NoShrink());
                Text(idManager.AllocateDynamicID("cp_channel_label"), label, theme.colorPickerChannelLabelStyleRef);

                const ID trackID = idManager.AllocateDynamicID("cp_channel_track");
                const ElementRef trackRef = RegisterControl(Element{ .id = trackID,
                    .eventHandler = MakeSliderHandler(guiStateManager, elementStorage, &value, styleManager.Px(200), nullptr, minVal, maxVal, SliderPress::Jump),
                    .styleRef = theme.colorPickerTrackStyleRef }, LayoutOverride{}.FlexGrow(1));
                const float t = (maxVal > minVal) ? std::clamp((value - minVal) / (maxVal - minVal), 0.0f, 1.0f) : 0.0f;
                VERIFY(elementStorage.AddCustomDraw(trackRef,
                    [fill, checker, t, look, white = cp.whiteColor, outline = cp.outlineColor, trackH = styleManager.Px(8),
                     thumb = float(styleManager.Px(6)), edge = float(styleManager.Px(1))](CustomDrawContext& ctx)
                    {
                        const IntRect r = ctx.ElementInnerRect();
                        const IntRect track{ .position = { r.x, r.y + (r.h - trackH) / 2 }, .size = { r.w, trackH } };
                        const float radius = float(trackH) * 0.5f;
                        if (checker)
                        {
                            ColorSwatchLook strip = look;
                            strip.radius = radius;
                            strip.checkerSize = trackH / 2;
                            DrawCheckerboard(ctx, track, track, strip);
                        }
                        ctx.RenderQuad(track, fill, radius);
                        const int2 at{ track.x + int(std::lround(t * float(track.w))), track.y + trackH / 2 };
                        DrawDisc(ctx, at, thumb + edge, outline);
                        DrawDisc(ctx, at, thumb, white);
                    }));

                // The value, rounded to what the range shows.
                float shown = value * scale;
                shown = (scale > 1.0f) ? std::round(shown) : std::round(shown * 1000.0f) / 1000.0f;
                if (Field(idManager.AllocateDynamicID("cp_channel_value"), shown, TextEditBehavior::None, theme.colorPickerChannelValueStyleRef,
                          NumericFieldConfig<float>{ .minVal = minVal * scale, .maxVal = maxVal * scale }, LayoutOverride{}))
                    value = shown / scale;
                EndHorizontal();
            };
            for (int i = 0; i < 3; ++i)
                channel(labels[i], cp.comp[i], mins[i], maxs[i], scales[i], gs[i], false);
            channel("A", cp.a, 0.0f, 1.0f, byte, gs[3], true);
            EndVertical();

            cp.snapComp[0] = cp.comp[0];
            cp.snapComp[1] = cp.comp[1];
            cp.snapComp[2] = cp.comp[2];
            cp.slidersInitialized = true;

            // Hex.
            BeginHorizontal(idManager.AllocateDynamicID("cp_hex_row"), theme.colorPickerFieldRowStyleRef, LayoutOverride{}.NoShrink());
            {
                Text(idManager.AllocateDynamicID("cp_hex_label"), "Hex", theme.colorPickerRowLabelStyleRef);
                const ID hexID = idManager.AllocateDynamicID("color_picker_hex");
                if (guiStateManager.Interaction().GetFocusedID() != hexID)
                    cp.hexText = ColorToHex(curR, curG, curB);
                if (Field(hexID, cp.hexText, TextEditBehavior::None, theme.colorHexStyleRef, LayoutOverride{}.FlexGrow(1)))
                {
                    float nr, ng, nb;
                    if (HexToColor(cp.hexText, nr, ng, nb))
                        ColorConvert::RGBtoHSV(nr, ng, nb, cp.h, cp.s, cp.v);
                }
            }
            EndHorizontal();

            // HDR: the brightness multiplier over 1 (disabled for a field without HDR), and
            // whether the RGB channels are edited in linear light.
            {
                BeginHorizontal(idManager.AllocateDynamicID("cp_hdr_row"), theme.colorPickerFieldRowStyleRef, LayoutOverride{}.NoShrink());
                Text(idManager.AllocateDynamicID("cp_hdr_label"), "HDR", theme.colorPickerRowLabelStyleRef);
                if (!allowHDR) guiStateManager.Construction().PushDisabled();
                BeginHorizontal(idManager.AllocateDynamicID("cp_hdr_box"), theme.colorPickerHdrStyleRef, LayoutOverride{}.FlexGrow(1));
                Text(idManager.AllocateDynamicID("cp_hdr_caption"), "Intensity", theme.colorPickerCaptionStyleRef);
                Field(idManager.AllocateDynamicID("cp_hdr_value"), cp.hdrBrightness, TextEditBehavior::None, theme.colorPickerHdrValueStyleRef,
                      NumericFieldConfig<float>{ .minVal = 1.0f, .maxVal = 1000.0f }, LayoutOverride{}.FlexGrow(1));
                EndHorizontal();
                if (!allowHDR) guiStateManager.Construction().PopDisabled();
                SetTooltip(allowHDR ? "Brightness over 1" : "This color has no HDR range");
                const bool linear = (cp.activeSpace == ColorSpace::LinearRGB);
                Checkbox(idManager.AllocateDynamicID("cp_linear"), linear, "Linear", [this](bool on) {
                    m_colorPickerSub->State().activeSpace = on ? ColorSpace::LinearRGB : ColorSpace::sRGB;
                });
                SetTooltip("Edit the RGB channels in linear light");
                EndHorizontal();
            }
        }
        EndVertical();
        EndHorizontal();   // body

        // ---- Swatches and recent colors ----
        BeginVertical(idManager.AllocateDynamicID("cp_swatches"), theme.colorPickerSwatchesStyleRef, LayoutOverride{}.NoShrink());
        {
            const color4f current{ curR, curG, curB, cp.a };
            auto pick = [this](const color4f& c) {
                auto& cp = m_colorPickerSub->State();
                ColorConvert::RGBtoHSV(std::clamp(c.r, 0.f, 1.f), std::clamp(c.g, 0.f, 1.f), std::clamp(c.b, 0.f, 1.f), cp.h, cp.s, cp.v);
                cp.a = c.a;
            };
            auto same = [](const color4f& a, const color4f& b) {
                return std::abs(a.r - b.r) < 0.002f && std::abs(a.g - b.g) < 0.002f && std::abs(a.b - b.b) < 0.002f && std::abs(a.a - b.a) < 0.002f;
            };
            // One swatch: the color split opaque | with alpha, an accent ring when it's the current one.
            auto swatch = [&](ID id, const color4f& c, bool round, EventHandler handler)
            {
                NoteSwatchClaim(id);
                auto& alphaStyle = m_colorPickerSub->ClaimSwatchStyle(id);
                SetColorStyle(styleManager, alphaStyle, c);
                const ID opaqueID = idManager.AllocateDynamicID("cp_swatch_opaque");
                NoteSwatchClaim(opaqueID);
                auto& opaqueStyle = m_colorPickerSub->ClaimSwatchStyle(opaqueID);
                SetColorStyle(styleManager, opaqueStyle, color4f{ c.r, c.g, c.b, 1.0f });
                const bool selected = !round && same(c, current);
                const ElementRef ref = RegisterControl(Element{ .id = id, .eventHandler = std::move(handler),
                    .styleRef = round ? theme.colorPickerRecentStyleRef : theme.colorPickerSwatchStyleRef }, LayoutOverride{});
                ColorSwatchLook swLook = look;
                swLook.alphaBar = round ? 0 : styleManager.Px(2);
                VERIFY(elementStorage.AddCustomDraw(ref,
                    [swLook, round, selected, opaque = opaqueStyle, withAlpha = alphaStyle, alpha = c.a,
                     accent = theme.accentColor, panel = theme.panelColor, gap = styleManager.Px(2)](CustomDrawContext& ctx)
                    {
                        const IntRect outer = ctx.ElementRect();
                        IntRect inner = ctx.ElementInnerRect();
                        ColorSwatchLook l = swLook;
                        if (round) l.radius = float(inner.w) * 0.5f;
                        if (selected)
                        {
                            // A panel-colored gap, then a 2 px accent ring, outside the swatch.
                            const IntRect ring{ .position = { outer.x - 2 * gap, outer.y - 2 * gap }, .size = { outer.w + 4 * gap, outer.h + 4 * gap } };
                            const IntRect gapRect{ .position = { outer.x - gap, outer.y - gap }, .size = { outer.w + 2 * gap, outer.h + 2 * gap } };
                            ctx.RenderQuad(ring, accent, l.radius + float(2 * gap));
                            ctx.RenderQuad(gapRect, panel, l.radius + float(gap));
                            inner = outer;
                        }
                        DrawColorSwatch(ctx, inner, l, opaque, withAlpha, alpha);
                    }));
            };

            BeginHorizontal(idManager.AllocateDynamicID("cp_swatch_row"), theme.colorPickerSwatchRowStyleRef, LayoutOverride{}.NoShrink());
            Text(idManager.AllocateDynamicID("cp_swatches_label"), "Swatches", theme.colorPickerSectionLabelStyleRef);
            auto& palette = cp.palette;
            const ID swatchRange = palette.empty() ? ID::Invalid()
                : idManager.AllocateRangedID(static_cast<uint32_t>(palette.size()), "color_picker_swatch");
            for (int i = 0; i < int(palette.size()); i++)
            {
                // Left-click picks it, right-click removes it.
                swatch(idManager.GetSubID(swatchRange, uint32_t(i)), palette[size_t(i)].color, false,
                    [this, i, pick](ElementEvent&& event) -> bool {
                        if (event.type != EventType::MouseDown) return false;
                        auto& palette = m_colorPickerSub->State().palette;
                        if (i >= int(palette.size())) return false;
                        if (event.state.LeftPressed())  { pick(palette[size_t(i)].color); return true; }
                        if (event.state.RightPressed()) { palette.erase(palette.begin() + i); return true; }
                        return false;
                    });
                SetTooltip("Click to use, right-click to remove");
            }
            Button(idManager.AllocateDynamicID("color_picker_add_swatch"), theme.addIcon, {}, [this]() {
                auto& cp = m_colorPickerSub->State();
                auto [r, g, b] = ColorConvert::HSVtoRGB(cp.h, cp.s, cp.v);
                const color4f c{ r, g, b, cp.a };
                if (cp.palette.empty() || !(cp.palette.back().color == c))
                    cp.palette.push_back({ c });
            }, theme.colorPickerNewStyleRef);
            SetTooltip("Add the color to the swatches");
            EndHorizontal();

            {
                BeginHorizontal(idManager.AllocateDynamicID("cp_recent_row"), theme.colorPickerSwatchRowStyleRef, LayoutOverride{}.NoShrink());
                Text(idManager.AllocateDynamicID("cp_recent_label"), "Recent", theme.colorPickerSectionLabelStyleRef);
                const ID recentRange = idManager.AllocateRangedID(static_cast<uint32_t>(std::max<size_t>(1, cp.recent.size())), "color_picker_recent");
                for (int i = 0; i < int(cp.recent.size()); i++)
                {
                    const color4f c = cp.recent[size_t(i)];
                    swatch(idManager.GetSubID(recentRange, uint32_t(i)), c, true, MakeButtonHandler([pick, c]() { pick(c); }));
                }
                EndHorizontal();
            }
        }
        EndVertical();

        // ---- Footer: status, Cancel, OK ----
        BeginHorizontal(idManager.AllocateDynamicID("cp_footer"), theme.dialogFooterStyleRef, LayoutOverride{}.NoShrink());
        {
            Element dot{ .id = idManager.AllocateDynamicID("cp_status_dot"), .styleRef = theme.dialogStatusDotStyleRef, .flags = ElementFlags::NoInput };
            RegisterElement(std::move(dot), LayoutOverride{});
            Text(idManager.AllocateDynamicID("cp_status"), allowHDR ? "HDR \xC2\xB7 float" : "sRGB \xC2\xB7 8-bit", theme.dialogStatusStyleRef,
                 LayoutOverride{}.FlexGrow(1));
            Button(idManager.AllocateDynamicID("cp_cancel"), IconID::Invalid, "Cancel", requestClose(ColorPickerState::CloseRequest::Cancel),
                   theme.dialogButtonStyleRef);
            Button(idManager.AllocateDynamicID("cp_ok"), IconID::Invalid, "OK", requestClose(ColorPickerState::CloseRequest::Ok),
                   theme.dialogPrimaryButtonStyleRef);
        }
        EndHorizontal();

        // Write the working color back every frame while the picker is open.
        const float mult = (allowHDR && cp.hdrBrightness > 1.0f) ? cp.hdrBrightness : 1.0f;
        outColor = { curR * mult, curG * mult, curB * mult, cp.a };
    }

    // ---------------------------------------------------------------------------
    // ColorField — swatch + optional picker popup
    // ---------------------------------------------------------------------------

    namespace
    {
        // "#RRGGBB" of the color, clamped to 0..1.
        std::string ColorHex(const color4f& c)
        {
            auto byte = [](float v) { return int(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
            char buf[8];
            std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", byte(c.r), byte(c.g), byte(c.b));
            return buf;
        }

        // Reads "#RRGGBB" or "RRGGBB" into rgb; anything else is rejected.
        bool ParseColorHex(std::string_view text, color4f& c)
        {
            if (!text.empty() && text.front() == '#') text.remove_prefix(1);
            if (text.size() != 6) return false;
            unsigned rgb = 0;
            for (char ch : text)
            {
                const int digit = (ch >= '0' && ch <= '9') ? ch - '0'
                                : (ch >= 'a' && ch <= 'f') ? ch - 'a' + 10
                                : (ch >= 'A' && ch <= 'F') ? ch - 'A' + 10 : -1;
                if (digit < 0) return false;
                rgb = (rgb << 4) | unsigned(digit);
            }
            c.r = float((rgb >> 16) & 0xFF) / 255.0f;
            c.g = float((rgb >> 8) & 0xFF) / 255.0f;
            c.b = float(rgb & 0xFF) / 255.0f;
            return true;
        }
    }

    ColorSwatchLook GUIApplication::SwatchLook() const
    {
        return ColorSwatchLook{
            .checkerA    = theme.checkerAColor,  .checkerB  = theme.checkerBColor,
            .alphaTrack  = theme.alphaTrackColor, .alphaFill = theme.alphaFillColor,
            .checkerSize = styleManager.Px(8),
            .alphaBar    = styleManager.Px(3),
            .radius      = float(styleManager.Px(3)),   // --radius-md inside the 1 px border
        };
    }

    bool GUIApplication::ColorField(color4f& value, bool allowHDR, LayoutOverride&& constraints)
    {
        const color4f before = value;

        // A swatch (opens the picker), the color as hex, and its alpha in percent.
        BeginHorizontal(idManager.AllocateDynamicID("color_field"), theme.colorFieldStyleRef, std::move(constraints));

        // ---- Swatch button ----
        ID swatchID = idManager.AllocateDynamicID("color_swatch");
        const std::string context = PendingLabelText();
        auto& cp = m_colorPickerSub->State();

        // OK keeps the color and remembers it; Cancel and the close button restore the original.
        if (cp.triggerID == swatchID && cp.closeRequest != ColorPickerState::CloseRequest::None)
        {
            if (cp.closeRequest == ColorPickerState::CloseRequest::Cancel)
                value = cp.originalColor;
            else
            {
                auto& recent = cp.recent;
                recent.erase(std::remove(recent.begin(), recent.end(), value), recent.end());
                recent.insert(recent.begin(), value);
                if (recent.size() > 8) recent.resize(8);
            }
            cp.closeRequest = ColorPickerState::CloseRequest::None;
            cp.triggerID = ID::Invalid();
        }
        const bool pickerIsOpen = (cp.triggerID == swatchID); // this field's picker, not just any
        NoteSwatchClaim(swatchID);
        auto& swatchStyle = m_colorPickerSub->ClaimSwatchStyle(swatchID);
        SetColorStyle(styleManager, swatchStyle, value);
        const ID opaqueID = idManager.AllocateDynamicID("color_swatch_opaque");
        NoteSwatchClaim(opaqueID);
        auto& opaqueStyle = m_colorPickerSub->ClaimSwatchStyle(opaqueID);
        SetColorStyle(styleManager, opaqueStyle, color4f{ value.r, value.g, value.b, 1.0f });
        const ElementRef swatchRef = RegisterControl(
            interstice::Element
            {
                .id = swatchID,
                // Captures a copy: handlers run after this returns, and `value` may be a temporary.
                .eventHandler = MakeButtonHandler([this, value, swatchID, allowHDR, pickerIsOpen, context]()
                {
                    auto& cp = m_colorPickerSub->State();
                    if (pickerIsOpen)
                    {
                        cp.triggerID = ID::Invalid();
                    } else
                    {
                        cp.triggerID = swatchID;
                        if (cp.windowID == ID::Invalid())
                            cp.windowID = idManager.CreateStaticID();
                        // HDR: the over-range part becomes the brightness multiplier, so
                        // HSV (of the colour divided by it) times brightness gives it back.
                        cp.hdrBrightness = allowHDR ? std::max({value.r, value.g, value.b, 1.0f}) : 1.0f;
                        float r = std::clamp(value.r / cp.hdrBrightness, 0.0f, 1.0f);
                        float g = std::clamp(value.g / cp.hdrBrightness, 0.0f, 1.0f);
                        float b = std::clamp(value.b / cp.hdrBrightness, 0.0f, 1.0f);
                        ColorConvert::RGBtoHSV(r, g, b, cp.h, cp.s, cp.v);
                        cp.a = value.a;
                        cp.originalColor = value;
                        cp.allowHDR = allowHDR;
                        cp.context = context;
                        // The color it had counts as a recent one.
                        auto& recent = cp.recent;
                        recent.erase(std::remove(recent.begin(), recent.end(), value), recent.end());
                        recent.insert(recent.begin(), value);
                        if (recent.size() > 8) recent.resize(8);
                    }
                }),
                .styleRef        = theme.colorSwatchStyleRef
            },
            LayoutOverride{});
        VERIFY(elementStorage.AddCustomDraw(swatchRef,
            [look = SwatchLook(), opaque = opaqueStyle, withAlpha = swatchStyle, alpha = value.a](CustomDrawContext& ctx)
            {
                DrawColorSwatch(ctx, ctx.ElementInnerRect(), look, opaque, withAlpha, alpha);
            }));

        std::string hex = ColorHex(value);
        if (Field(idManager.AllocateDynamicID("color_hex"), hex, TextEditBehavior::None, theme.colorHexStyleRef,
                  LayoutOverride{}.FlexGrow(1)))
            ParseColorHex(hex, value);

        BeginHorizontal(idManager.AllocateDynamicID("color_alpha"), theme.colorAlphaStyleRef, LayoutOverride{});
        Text(idManager.AllocateDynamicID("color_alpha_caption"), "A", theme.colorAlphaCaptionStyleRef);
        int alphaPercent = int(std::lround(std::clamp(value.a, 0.0f, 1.0f) * 100.0f));
        if (Field(idManager.AllocateDynamicID("color_alpha_value"), alphaPercent, TextEditBehavior::ContinuousUpdate,
                  theme.colorAlphaInputStyleRef, NumericFieldConfig<int>{ .minVal = 0, .maxVal = 100 },
                  LayoutOverride{}.FlexGrow(1)))
            value.a = float(alphaPercent) / 100.0f;
        Text(idManager.AllocateDynamicID("color_alpha_unit"), "%", theme.colorAlphaUnitStyleRef);
        EndHorizontal();

        EndHorizontal();

        // ---- Picker popup ----
        if (pickerIsOpen && cp.windowID != ID::Invalid())
        {
            auto&& popupPos = LayoutOverride{}.Anchored(swatchID,
                AnchorPlacement::OutsideBottom | AnchorPlacement::InsideLeft,
                {0, int16_t(styleManager.Px(4))}, AutoFlipMode::FlipAvoidTarget);

            if (BeginWindow(cp.windowID, "Color Picker", std::move(popupPos), theme.colorPickerStyleRef,
                            ElementFlags::WindowCanMove | ElementFlags::WindowNoCaption | ElementFlags::Shadows))
            {
                EmitColorPicker(value, allowHDR);
                EndWindow();
            } else
            {
                // Window was closed (click outside / focus lost)
                cp.triggerID = ID::Invalid();
            }
        }

        return value != before;
    }

    bool GUIApplication::ColorField(color4b& value, LayoutOverride&& constraints)
    {
        color4f f {
            value.r / 255.0f,
            value.g / 255.0f,
            value.b / 255.0f,
            value.a / 255.0f
        };
        bool changed = ColorField(f, false, std::move(constraints));
        if (changed)
        {
            value.r = static_cast<byte>(std::clamp(int(f.r * 255.0f + 0.5f), 0, 255));
            value.g = static_cast<byte>(std::clamp(int(f.g * 255.0f + 0.5f), 0, 255));
            value.b = static_cast<byte>(std::clamp(int(f.b * 255.0f + 0.5f), 0, 255));
            value.a = static_cast<byte>(std::clamp(int(f.a * 255.0f + 0.5f), 0, 255));
        }
        return changed;
    }

}