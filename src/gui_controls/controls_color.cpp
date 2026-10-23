#include <gui_controls/gui_application.h>
#include <gui_controls/controls_color_internal.h>   // SetColorStyle (shared with controls_gradient.cpp)
#include <gui_core/style/gui_color_conversions.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

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

    // Create a ring gradient for RenderCircle. sampleFn maps a hue angle [0,1)
    // to an sRGB color. RenderCircle maps t=0→1 starting at 3 o'clock going CW.
    // Our convention is hue=0 at 12 o'clock, so we offset by 0.25.
    template <typename Fn>
    void SetRingGradient(interstice::StyleManager& sm, interstice::VisualColorStyleIndex& idx, Fn&& sampleFn, int kStops = 7)
    {
        interstice::Gradient g;
        g.type = interstice::GradientType::Linear;
        for (int i = 0; i < kStops; i++)
        {
            float t = float(i) / float(kStops - 1);
            float hue = std::fmod(t + 0.25f, 1.0f);
            g.AddStop(sampleFn(hue), t);
        }
        interstice::ColorStyle cs = interstice::ColorStyle::Create(g, interstice::ColorType::LinearGradient);
        if (idx == interstice::VisualColorStyleIndex::Invalid)
            idx = sm.CreateColorStyle(cs);
        else
            sm.UpdateColorStyle(idx, cs);
    }

    // HSV hue ring
    void SetHueRingGradient(interstice::StyleManager& sm, interstice::VisualColorStyleIndex& idx)
    {
        SetRingGradient(sm, idx, [](float hue) -> interstice::color4f {
            auto [r, g, b] = interstice::ColorConvert::HSVtoRGB(hue, 1.0f, 1.0f);
            return { r, g, b, 1.0f };
        });
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
    // Color picker popup — emitted when ColorPickerSubsystem state's triggerID matches
    // ---------------------------------------------------------------------------

    // ---- Color wheel geometry (shared with event handler), at the UI scale ----
    struct WheelGeometry
    {
        int padding, size, outerRadius, innerRadius, squareSide, squareHalf, center;
    };
    static WheelGeometry MakeWheelGeometry(const StyleManager& sm)
    {
        WheelGeometry g{};
        g.padding     = sm.Px(4);
        g.size        = sm.Px(220);
        g.outerRadius = g.size / 2;
        g.innerRadius = g.outerRadius - sm.Px(18);   // ring thickness
        // SV square inscribed in the inner circle (side = r*sqrt(2)), a hair under the limit.
        g.squareSide  = sm.Px(116);
        g.squareHalf  = g.squareSide / 2;
        g.center      = g.size / 2 + g.padding;
        return g;
    }

    void GUIApplication::EmitColorPicker(color4f& outColor, bool allowHDR)
    {
        auto& cp = m_colorPickerSub->State();

        // ---- Resolve pending slider modifications FIRST ----
        // Slider handlers edit cp.comp[] after the previous frame; convert back to HSV
        // before updating gradient styles to avoid a 1-frame lag.
        if (cp.slidersInitialized)
        {
            bool compModified = (cp.comp[0] != cp.snapComp[0] ||
                                    cp.comp[1] != cp.snapComp[1] ||
                                    cp.comp[2] != cp.snapComp[2]);
            if (compModified)
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
                        auto [r, g, b] = ColorConvert::HSLtoRGB(std::clamp(cp.comp[0], 0.f, 1.f),
                                                    std::clamp(cp.comp[1], 0.f, 1.f),
                                                    std::clamp(cp.comp[2], 0.f, 1.f));
                        ColorConvert::RGBtoHSV(r, g, b, cp.h, cp.s, cp.v);
                        break;
                    }
                    case ColorSpace::sRGB:
                        ColorConvert::RGBtoHSV(std::clamp(cp.comp[0], 0.f, 1.f),
                                    std::clamp(cp.comp[1], 0.f, 1.f),
                                    std::clamp(cp.comp[2], 0.f, 1.f),
                                    cp.h, cp.s, cp.v);
                        break;
                    case ColorSpace::LinearRGB:
                    {
                        float sr = ColorConvert::LinearToSRGB(std::clamp(cp.comp[0], 0.f, 1.f));
                        float sg = ColorConvert::LinearToSRGB(std::clamp(cp.comp[1], 0.f, 1.f));
                        float sb = ColorConvert::LinearToSRGB(std::clamp(cp.comp[2], 0.f, 1.f));
                        ColorConvert::RGBtoHSV(sr, sg, sb, cp.h, cp.s, cp.v);
                        break;
                    }
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
        }


        // ---- Compute per-space wheel state ----
        // wheelHue [0,1]: angular position on the ring (0 = top / 12 o'clock, CW)
        // squareX  [0,1]: horizontal axis of the inner square
        // squareY  [0,1]: vertical axis (0 = top, 1 = bottom; display inverted where needed)
        constexpr float kPi = 3.14159265f;
        auto [curR_w, curG_w, curB_w] = ColorConvert::HSVtoRGB(cp.h, cp.s, cp.v);
        float wheelHue = cp.h;
        float squareX  = cp.s;
        float squareY  = cp.v;   // 1 = top of square visually (bright), 0 = bottom (dark)

        switch (cp.activeSpace)
        {
            default:
            case ColorSpace::HSV:
            case ColorSpace::sRGB:
            case ColorSpace::LinearRGB:
                wheelHue = cp.h;
                squareX  = cp.s;
                squareY  = cp.v;
                break;
            case ColorSpace::HSL:
            {
                float hsl_h, hsl_s, hsl_l;
                ColorConvert::RGBtoHSL(curR_w, curG_w, curB_w, hsl_h, hsl_s, hsl_l);
                wheelHue = hsl_h;
                squareX  = hsl_s;
                squareY  = hsl_l;     // 1 = white (top), 0 = black (bottom)
                break;
            }
            case ColorSpace::Lab:
            {
                float L, a, b;
                ColorConvert::RGBtoLab(curR_w, curG_w, curB_w, L, a, b);
                wheelHue = std::atan2(b, a) / (2.0f * kPi);
                if (wheelHue < 0) wheelHue += 1.0f;
                float chroma = std::sqrt(a * a + b * b);
                squareX = std::clamp(chroma / 128.0f, 0.0f, 1.0f);  // normalized chroma
                squareY = std::clamp(L / 100.0f, 0.0f, 1.0f);       // normalized L
                break;
            }
            case ColorSpace::Oklab:
            {
                float L, a, b;
                ColorConvert::RGBtoOklab(curR_w, curG_w, curB_w, L, a, b);
                wheelHue = std::atan2(b, a) / (2.0f * kPi);
                if (wheelHue < 0) wheelHue += 1.0f;
                float chroma = std::sqrt(a * a + b * b);
                squareX = std::clamp(chroma / 0.4f, 0.0f, 1.0f);    // normalized chroma
                squareY = std::clamp(L, 0.0f, 1.0f);                 // L is already 0-1
                break;
            }
        }

        // ---- Update inner square color styles (4-corner InterpolatedBox) ----
        {
            // TL=c0, TR=c1, BR=c2, BL=c3.  X axis = squareX, Y axis = squareY (top→bottom).
            auto cornerColor = [&](float sx, float sy) -> color4f {
                switch (cp.activeSpace)
                {
                    default:
                    case ColorSpace::HSV:
                    case ColorSpace::sRGB:
                    case ColorSpace::LinearRGB:
                    {
                        auto [r, g, b] = ColorConvert::HSVtoRGB(wheelHue, sx, 1.0f - sy); // sy=0 → V=1 (top=bright)
                        return { r, g, b, 1.0f };
                    }
                    case ColorSpace::HSL:
                    {
                        auto [r, g, b] = ColorConvert::HSLtoRGB(wheelHue, sx, 1.0f - sy); // sy=0 → L=1 (top=white)
                        return { r, g, b, 1.0f };
                    }
                    case ColorSpace::Lab:
                    {
                        float angle = wheelHue * 2.0f * kPi;
                        float chroma = sx * 128.0f;
                        float L = (1.0f - sy) * 100.0f;  // sy=0 → L=100 (top=light)
                        auto [r, g, b] = ColorConvert::LabToRGB(L, chroma * std::cos(angle), chroma * std::sin(angle));
                        return { r, g, b, 1.0f };
                    }
                    case ColorSpace::Oklab:
                    {
                        float angle = wheelHue * 2.0f * kPi;
                        float chroma = sx * 0.4f;
                        float L = 1.0f - sy;              // sy=0 → L=1 (top=light)
                        auto [r, g, b] = ColorConvert::OklabToRGB(L, chroma * std::cos(angle), chroma * std::sin(angle));
                        return { r, g, b, 1.0f };
                    }
                }
            };
            SetQuadColorStyle(styleManager, cp.svQuadColor,
                cornerColor(0, 0), cornerColor(1, 0),   // TL, TR
                cornerColor(1, 1), cornerColor(0, 1));   // BR, BL
        }

        // ---- Update hue ring gradient ----
        switch (cp.activeSpace)
        {
            default:
            case ColorSpace::HSV:
            case ColorSpace::HSL:
            case ColorSpace::sRGB:
            case ColorSpace::LinearRGB:
                // HSV and HSL share the same hue circle; RGB tabs use HSV wheel
                SetHueRingGradient(styleManager, cp.hueRing);
                break;
            case ColorSpace::Lab:
                SetRingGradient(styleManager, cp.hueRing, [](float hue) -> color4f {
                    float angle = hue * 2.0f * kPi;
                    auto [r, g, b] = ColorConvert::LabToRGB(65.0f, 80.0f * std::cos(angle), 80.0f * std::sin(angle));
                    return { r, g, b, 1.0f };
                }, 8);
                break;
            case ColorSpace::Oklab:
                SetRingGradient(styleManager, cp.hueRing, [](float hue) -> color4f {
                    float angle = hue * 2.0f * kPi;
                    auto [r, g, b] = ColorConvert::OklabToRGB(0.7f, 0.2f * std::cos(angle), 0.2f * std::sin(angle));
                    return { r, g, b, 1.0f };
                }, 8);
                break;
        }

        // Dark border for the outer ring edge.
        SetColorStyle(styleManager, cp.markerColor, { 0.0f, 0.0f, 0.0f, 1.0f });
        // White for hue indicator circle.
        SetColorStyle(styleManager, cp.whiteColor, { 1.0f, 1.0f, 1.0f, 1.0f });
        // Square indicator: contrasting with vertical position (light at bottom, dark at top)
        {
            float lum = squareY;   // squareY=0 at top (bright) → dark indicator; squareY=1 at bottom (dark) → light indicator
            SetColorStyle(styleManager, cp.svIndicatorColor, { lum, lum, lum, 1.0f });
        }

        // ---- Update preview swatch colors ----
        SetColorStyle(styleManager, cp.newColor, { curR_w, curG_w, curB_w, 1.0f });

        // ---- Color wheel (hue ring + inscribed SV square) ----
        ID wheelID = idManager.AllocateDynamicID("color_picker_wheel");
        const WheelGeometry wg = MakeWheelGeometry(styleManager);
        {
            auto& dragTarget = cp.wheelDrag;
            float* hp = &cp.h;
            float* sp = &cp.s;
            float* vp = &cp.v;
            ColorSpace wheelSpace = cp.activeSpace;

            Element wheel {};
            wheel.id       = wheelID;
            wheel.styleRef = theme.colorPickerWheelStyleRef;
            wheel.state    = guiStateManager.GetCurrentState(wheelID);
            wheel.eventHandler = [this, wg, wheelID, hp, sp, vp, wheelSpace, &dragTarget](ElementEvent&& event) -> bool
            {
                auto localPos = [&](float& lx, float& ly) -> bool
                {
                    if (!event.rect) return false;
                    auto& r = event.rect->rect;
                    if (r.w <= 0 || r.h <= 0) return false;
                    const int2 c = event.LocalCursor(elementStorage);
                    lx = float(c.x - r.x);
                    ly = float(c.y - r.y);
                    return true;
                };

                // Convert a screen angle [0,1) to canonical h/s/v depending on the active space.
                // For HSV/HSL/RGB the angle IS the hue; for Lab/Oklab we must rebuild the full color.
                auto applyRing = [&](float lx, float ly)
                {
                    float dx = lx - float(wg.center);
                    float dy = ly - float(wg.center);
                    float angle01 = std::atan2(dx, -dy) / (2.0f * kPi);
                    if (angle01 < 0) angle01 += 1.0f;

                    switch (wheelSpace)
                    {
                        default:
                        case ColorSpace::HSV:
                        case ColorSpace::sRGB:
                        case ColorSpace::LinearRGB:
                            *hp = std::clamp(angle01, 0.0f, 1.0f);
                            break;
                        case ColorSpace::HSL:
                        {
                            // HSL hue = HSV hue for saturated colors, but we need to
                            // preserve the current S,L while changing hue.
                            auto [cr, cg, cb] = ColorConvert::HSVtoRGB(*hp, *sp, *vp);
                            float oldH, oldS, oldL;
                            ColorConvert::RGBtoHSL(cr, cg, cb, oldH, oldS, oldL);
                            auto [nr, ng, nb] = ColorConvert::HSLtoRGB(angle01, oldS, oldL);
                            ColorConvert::RGBtoHSV(nr, ng, nb, *hp, *sp, *vp);
                            break;
                        }
                        case ColorSpace::Lab:
                        {
                            auto [cr, cg, cb] = ColorConvert::HSVtoRGB(*hp, *sp, *vp);
                            float L, a, b;
                            ColorConvert::RGBtoLab(cr, cg, cb, L, a, b);
                            float chroma = std::sqrt(a * a + b * b);
                            float newAngle = angle01 * 2.0f * kPi;
                            auto [nr, ng, nb] = ColorConvert::LabToRGB(L, chroma * std::cos(newAngle), chroma * std::sin(newAngle));
                            ColorConvert::RGBtoHSV(nr, ng, nb, *hp, *sp, *vp);
                            break;
                        }
                        case ColorSpace::Oklab:
                        {
                            auto [cr, cg, cb] = ColorConvert::HSVtoRGB(*hp, *sp, *vp);
                            float L, a, b;
                            ColorConvert::RGBtoOklab(cr, cg, cb, L, a, b);
                            float chroma = std::sqrt(a * a + b * b);
                            float newAngle = angle01 * 2.0f * kPi;
                            auto [nr, ng, nb] = ColorConvert::OklabToRGB(L, chroma * std::cos(newAngle), chroma * std::sin(newAngle));
                            ColorConvert::RGBtoHSV(nr, ng, nb, *hp, *sp, *vp);
                            break;
                        }
                    }
                };

                auto applySquare = [&](float lx, float ly)
                {
                    float sx = std::clamp((lx - float(wg.center - wg.squareHalf)) / float(wg.squareSide), 0.0f, 1.0f);
                    float sy = std::clamp((ly - float(wg.center - wg.squareHalf)) / float(wg.squareSide), 0.0f, 1.0f);

                    switch (wheelSpace)
                    {
                        default:
                        case ColorSpace::HSV:
                        case ColorSpace::sRGB:
                        case ColorSpace::LinearRGB:
                            *sp = sx;
                            *vp = 1.0f - sy;
                            break;
                        case ColorSpace::HSL:
                        {
                            auto [cr, cg, cb] = ColorConvert::HSVtoRGB(*hp, *sp, *vp);
                            float oldH, oldS, oldL;
                            ColorConvert::RGBtoHSL(cr, cg, cb, oldH, oldS, oldL);
                            auto [nr, ng, nb] = ColorConvert::HSLtoRGB(oldH, sx, 1.0f - sy);
                            ColorConvert::RGBtoHSV(nr, ng, nb, *hp, *sp, *vp);
                            break;
                        }
                        case ColorSpace::Lab:
                        {
                            auto [cr, cg, cb] = ColorConvert::HSVtoRGB(*hp, *sp, *vp);
                            float oldL, oldA, oldB;
                            ColorConvert::RGBtoLab(cr, cg, cb, oldL, oldA, oldB);
                            float hueAngle = std::atan2(oldB, oldA);
                            float chroma = sx * 128.0f;
                            float L = (1.0f - sy) * 100.0f;
                            auto [nr, ng, nb] = ColorConvert::LabToRGB(L, chroma * std::cos(hueAngle), chroma * std::sin(hueAngle));
                            ColorConvert::RGBtoHSV(nr, ng, nb, *hp, *sp, *vp);
                            break;
                        }
                        case ColorSpace::Oklab:
                        {
                            auto [cr, cg, cb] = ColorConvert::HSVtoRGB(*hp, *sp, *vp);
                            float oldL, oldA, oldB;
                            ColorConvert::RGBtoOklab(cr, cg, cb, oldL, oldA, oldB);
                            float hueAngle = std::atan2(oldB, oldA);
                            float chroma = sx * 0.4f;
                            float L = 1.0f - sy;
                            auto [nr, ng, nb] = ColorConvert::OklabToRGB(L, chroma * std::cos(hueAngle), chroma * std::sin(hueAngle));
                            ColorConvert::RGBtoHSV(nr, ng, nb, *hp, *sp, *vp);
                            break;
                        }
                    }
                };

                switch (event.type)
                {
                    case EventType::MouseDown:
                    {
                        if (!event.state.LeftPressed()) break;
                        float lx, ly;
                        if (!localPos(lx, ly)) break;
                        float dx = lx - float(wg.center);
                        float dy = ly - float(wg.center);
                        float dist = std::sqrt(dx * dx + dy * dy);

                        if (std::abs(dx) <= float(wg.squareHalf) && std::abs(dy) <= float(wg.squareHalf))
                        {
                            dragTarget = ColorPickerState::WheelDragTarget::Square;
                            guiStateManager.Interaction().SetCaptureID(wheelID);
                            applySquare(lx, ly);
                            return true;
                        }
                        if (dist <= float(wg.outerRadius) + styleManager.Px(1.0f) && dist >= float(wg.innerRadius) - styleManager.Px(4.0f))
                        {
                            dragTarget = ColorPickerState::WheelDragTarget::Ring;
                            guiStateManager.Interaction().SetCaptureID(wheelID);
                            applyRing(lx, ly);
                            return true;
                        }
                        break;
                    }
                    case EventType::MouseMove:
                    {
                        if (!event.state.LeftDown()) break;
                        if (dragTarget == ColorPickerState::WheelDragTarget::None) break;
                        float lx, ly;
                        if (!localPos(lx, ly)) break;
                        if (dragTarget == ColorPickerState::WheelDragTarget::Ring)
                            applyRing(lx, ly);
                        else
                            applySquare(lx, ly);
                        return true;
                    }
                    case EventType::MouseUp:
                        dragTarget = ColorPickerState::WheelDragTarget::None;
                        guiStateManager.Interaction().ReleaseCaptureID();
                        return true;
                    default: break;
                }
                return false;
            };

            auto wheelRes = elementStorage.Add(std::move(wheel),
                LayoutOverride::FixedSize(wg.size + (2 * wg.padding), wg.size + (2 * wg.padding)).BreakAfter());
            VERIFY(wheelRes.status);
            VERIFY(windowRegistry.Add(wheelRes.ref));

            // Custom draw: hue ring + inscribed square + indicators.
            auto wheelRef    = wheelRes.ref;
            auto hueRingIdx  = cp.hueRing;
            auto svQuad      = cp.svQuadColor;
            auto markerC     = cp.markerColor;
            auto whiteC      = cp.whiteColor;
            auto svIndC      = cp.svIndicatorColor;
            float drawHue    = wheelHue;    // space-specific hue for indicator
            float drawSqX    = squareX;     // space-specific square X for indicator
            float drawSqY    = squareY;     // space-specific square Y for indicator
            VERIFY(elementStorage.AddCustomDraw(wheelRef,
                [wg, px = [this](float v) { return styleManager.Px(v); }, hueRingIdx, svQuad, markerC, whiteC, svIndC, drawHue, drawSqX, drawSqY](CustomDrawContext& ctx)
                {
                    const float ringWidth = float(wg.outerRadius - wg.innerRadius);
                    const float midRadius = float(wg.outerRadius + wg.innerRadius) * 0.5f;
                    int2 center { wg.center, wg.center };

                    // 1. Hue ring
                    {
                        LineStyle outerLS {};
                        outerLS.colorStyleIndex = markerC;
                        outerLS.lineWidth       = ringWidth + px(3.0f);
                        ctx.RenderCircle(center, midRadius, outerLS);

                        LineStyle hueLS {};
                        hueLS.colorStyleIndex = hueRingIdx;
                        hueLS.lineWidth       = ringWidth;
                        ctx.RenderCircle(center, midRadius, hueLS);
                    }

                    // 2. Inner square
                    ctx.RenderQuad(
                        IntRect{ .position = { wg.center - wg.squareHalf, wg.center - wg.squareHalf },
                                    .size     = { wg.squareSide, wg.squareSide } },
                        svQuad);

                    // 3. Hue indicator on ring
                    {
                        float angle = drawHue * 2.0f * kPi - kPi * 0.5f;
                        int hx = int(float(wg.center) + std::cos(angle) * (midRadius + px(1.5f)));
                        int hy = int(float(wg.center) + std::sin(angle) * (midRadius + px(1.5f)));
                        LineStyle ls {};
                        ls.colorStyleIndex = whiteC;
                        ls.lineWidth       = px(3.0f);
                        ctx.RenderCircle(int2{ hx, hy }, (ringWidth * 0.5f) + px(1.0f), ls);
                    }

                    // 4. Square indicator
                    {
                        int mx = wg.center - wg.squareHalf + int(drawSqX * float(wg.squareSide) + 0.5f);
                        int my = wg.center - wg.squareHalf + int((1.0f - drawSqY) * float(wg.squareSide) + 0.5f);
                        LineStyle ls {};
                        ls.colorStyleIndex = svIndC;
                        ls.lineWidth       = px(3.0f);
                        ctx.RenderCircle(int2{ mx, my }, px(5.0f), ls);
                    }
                }));
        }

        // ---- Color space tabs ----
        {
            struct TabDef { ColorSpace space; std::string_view label; };
            constexpr TabDef tabs[] = {
                { ColorSpace::HSV,       "HSV"  },
                { ColorSpace::HSL,       "HSL"  },
                { ColorSpace::sRGB,      "RGB"  },
                { ColorSpace::LinearRGB, "Lin"  },
                { ColorSpace::Lab,       "Lab"  },
                { ColorSpace::Oklab,     "OkL"  },
            };

            Element tabRow {};
            tabRow.id       = idManager.AllocateDynamicID("cp_tab_row");
            tabRow.styleRef = theme.colorPickerTabRowStyleRef;
            tabRow.state    = guiStateManager.GetCurrentState(tabRow.id);
            RegisterElement(std::move(tabRow), LayoutOverride{}.BreakAfter());
            VERIFY(elementStorage.BeginChildren());

            for (auto& tab : tabs)
            {
                bool isActive = (cp.activeSpace == tab.space);
                if (isActive) guiStateManager.Construction().PushSelected();

                ColorSpace targetSpace = tab.space;
                Button(tab.label, [this, targetSpace]()
                {
                    m_colorPickerSub->State().activeSpace = targetSpace;
                });

                if (isActive) guiStateManager.Construction().PopSelected();
            }

            VERIFY(elementStorage.EndChildren());
        }

        // ---- Gradient sliders ----
        auto ba = [](){ return LayoutOverride{}.FlexGrow(1).BreakAfter(); };
        {
            auto [curR, curG, curB] = ColorConvert::HSVtoRGB(cp.h, cp.s, cp.v);
            auto& gs = cp.gradientStyles;

            // Slider labels, ranges, and component values depend on the active space
            std::string_view label0, label1, label2;
            float min0 = 0, max0 = 1, min1 = 0, max1 = 1, min2 = 0, max2 = 1;

            switch (cp.activeSpace)
            {
                case ColorSpace::HSV:
                {
                    cp.comp[0] = cp.h;
                    cp.comp[1] = cp.s;
                    cp.comp[2] = cp.v;
                    label0 = "H"; label1 = "S"; label2 = "V";

                    // H: rainbow (7 stops)
                    SetHueGradientStyleHorizontal(styleManager, gs[0]);
                    // S: desaturated -> saturated at current H,V
                    auto [s0r, s0g, s0b] = ColorConvert::HSVtoRGB(cp.h, 0.0f, cp.v);
                    auto [s1r, s1g, s1b] = ColorConvert::HSVtoRGB(cp.h, 1.0f, cp.v);
                    SetGradientStyle2(styleManager, gs[1], { s0r,s0g,s0b,1 }, { s1r,s1g,s1b,1 });
                    // V: black -> bright at current H,S
                    auto [v1r, v1g, v1b] = ColorConvert::HSVtoRGB(cp.h, cp.s, 1.0f);
                    SetGradientStyle2(styleManager, gs[2], { 0,0,0,1 }, { v1r,v1g,v1b,1 });
                    break;
                }
                case ColorSpace::HSL:
                {
                    float hsl_h, hsl_s, hsl_l;
                    ColorConvert::RGBtoHSL(curR, curG, curB, hsl_h, hsl_s, hsl_l);
                    cp.comp[0] = hsl_h;
                    cp.comp[1] = hsl_s;
                    cp.comp[2] = hsl_l;
                    label0 = "H"; label1 = "S"; label2 = "L";

                    // H: rainbow
                    SetHueGradientStyleHorizontal(styleManager, gs[0]);
                    // S: desaturated -> saturated at current H,L
                    auto [s0r, s0g, s0b] = ColorConvert::HSLtoRGB(hsl_h, 0.0f, hsl_l);
                    auto [s1r, s1g, s1b] = ColorConvert::HSLtoRGB(hsl_h, 1.0f, hsl_l);
                    SetGradientStyle2(styleManager, gs[1], { s0r,s0g,s0b,1 }, { s1r,s1g,s1b,1 });
                    // L: black -> mid -> white at current H,S
                    SetSampledGradient(styleManager, gs[2], [hsl_h, hsl_s](float t) -> color4f {
                        auto [r, g, b] = ColorConvert::HSLtoRGB(hsl_h, hsl_s, t);
                        return { r, g, b, 1.0f };
                    });
                    break;
                }
                case ColorSpace::sRGB:
                {
                    cp.comp[0] = curR;
                    cp.comp[1] = curG;
                    cp.comp[2] = curB;
                    label0 = "R"; label1 = "G"; label2 = "B";

                    // R: 0 -> 1 with current G,B held
                    SetGradientStyle2(styleManager, gs[0], { 0, curG, curB, 1 }, { 1, curG, curB, 1 });
                    // G: 0 -> 1 with current R,B held
                    SetGradientStyle2(styleManager, gs[1], { curR, 0, curB, 1 }, { curR, 1, curB, 1 });
                    // B: 0 -> 1 with current R,G held
                    SetGradientStyle2(styleManager, gs[2], { curR, curG, 0, 1 }, { curR, curG, 1, 1 });
                    break;
                }
                case ColorSpace::LinearRGB:
                {
                    float linR = ColorConvert::SRGBToLinear(curR);
                    float linG = ColorConvert::SRGBToLinear(curG);
                    float linB = ColorConvert::SRGBToLinear(curB);
                    cp.comp[0] = linR;
                    cp.comp[1] = linG;
                    cp.comp[2] = linB;
                    label0 = "R"; label1 = "G"; label2 = "B";

                    // R: display-space endpoints (linear 0 -> 1, converted to sRGB for display)
                    SetGradientStyle2(styleManager, gs[0],
                        { ColorConvert::LinearToSRGB(0.0f), ColorConvert::LinearToSRGB(linG), ColorConvert::LinearToSRGB(linB), 1 },
                        { ColorConvert::LinearToSRGB(1.0f), ColorConvert::LinearToSRGB(linG), ColorConvert::LinearToSRGB(linB), 1 });
                    SetGradientStyle2(styleManager, gs[1],
                        { ColorConvert::LinearToSRGB(linR), ColorConvert::LinearToSRGB(0.0f), ColorConvert::LinearToSRGB(linB), 1 },
                        { ColorConvert::LinearToSRGB(linR), ColorConvert::LinearToSRGB(1.0f), ColorConvert::LinearToSRGB(linB), 1 });
                    SetGradientStyle2(styleManager, gs[2],
                        { ColorConvert::LinearToSRGB(linR), ColorConvert::LinearToSRGB(linG), ColorConvert::LinearToSRGB(0.0f), 1 },
                        { ColorConvert::LinearToSRGB(linR), ColorConvert::LinearToSRGB(linG), ColorConvert::LinearToSRGB(1.0f), 1 });
                    break;
                }
                case ColorSpace::Lab:
                {
                    float L, a, b;
                    ColorConvert::RGBtoLab(curR, curG, curB, L, a, b);
                    cp.comp[0] = L;
                    cp.comp[1] = a;
                    cp.comp[2] = b;
                    label0 = "L"; label1 = "a"; label2 = "b";
                    min0 = 0; max0 = 100; min1 = -128; max1 = 128; min2 = -128; max2 = 128;

                    float ca = a, cb = b, cL = L;
                    SetSampledGradient(styleManager, gs[0], [ca, cb](float t) -> color4f {
                        auto [r, g, b] = ColorConvert::LabToRGB(t * 100.0f, ca, cb);
                        return { r, g, b, 1.0f };
                    });
                    SetSampledGradient(styleManager, gs[1], [cL, cb](float t) -> color4f {
                        auto [r, g, b] = ColorConvert::LabToRGB(cL, -128.0f + t * 256.0f, cb);
                        return { r, g, b, 1.0f };
                    });
                    SetSampledGradient(styleManager, gs[2], [cL, ca](float t) -> color4f {
                        auto [r, g, b] = ColorConvert::LabToRGB(cL, ca, -128.0f + t * 256.0f);
                        return { r, g, b, 1.0f };
                    });
                    break;
                }
                case ColorSpace::Oklab:
                {
                    float L, a, b;
                    ColorConvert::RGBtoOklab(curR, curG, curB, L, a, b);
                    cp.comp[0] = L;
                    cp.comp[1] = a;
                    cp.comp[2] = b;
                    label0 = "L"; label1 = "a"; label2 = "b";
                    min0 = 0; max0 = 1; min1 = -0.5f; max1 = 0.5f; min2 = -0.5f; max2 = 0.5f;

                    float ca = a, cb = b, cL = L;
                    SetSampledGradient(styleManager, gs[0], [ca, cb](float t) -> color4f {
                        auto [r, g, b] = ColorConvert::OklabToRGB(t, ca, cb);
                        return { r, g, b, 1.0f };
                    });
                    SetSampledGradient(styleManager, gs[1], [cL, cb](float t) -> color4f {
                        auto [r, g, b] = ColorConvert::OklabToRGB(cL, -0.5f + t * 1.0f, cb);
                        return { r, g, b, 1.0f };
                    });
                    SetSampledGradient(styleManager, gs[2], [cL, ca](float t) -> color4f {
                        auto [r, g, b] = ColorConvert::OklabToRGB(cL, ca, -0.5f + t * 1.0f);
                        return { r, g, b, 1.0f };
                    });
                    break;
                }
            }

            // A: transparent -> current color (always the same)
            SetGradientStyle2(styleManager, gs[3], { curR,curG,curB,0 }, { curR,curG,curB,1 });

            // Triangle indicator color (white)
            SetColorStyle(styleManager, cp.triangleStyle, { 1, 1, 1, 1 });

            // Emit the 3 component sliders + alpha
            GradientSlider(idManager.AllocateDynamicID("gs_c0"), cp.comp[0], min0, max0, label0, gs[0], cp.triangleStyle, ba());
            GradientSlider(idManager.AllocateDynamicID("gs_c1"), cp.comp[1], min1, max1, label1, gs[1], cp.triangleStyle, ba());
            GradientSlider(idManager.AllocateDynamicID("gs_c2"), cp.comp[2], min2, max2, label2, gs[2], cp.triangleStyle, ba());
            GradientSlider(idManager.AllocateDynamicID("gs_a"),  cp.a,       0,    1,    "A",    gs[3], cp.triangleStyle, ba());

            // Snapshot for next-frame change detection
            cp.snapComp[0] = cp.comp[0];
            cp.snapComp[1] = cp.comp[1];
            cp.snapComp[2] = cp.comp[2];

            cp.slidersInitialized = true;

            // Hex field
            {
                ID hexID = idManager.AllocateDynamicID("color_picker_hex");
                if (guiStateManager.Interaction().GetFocusedID() != hexID)
                    cp.hexText = ColorToHex(curR, curG, curB);

                DragLabel("Hex");
                if (Field(hexID, cp.hexText, TextEditBehavior::None,
                    LayoutOverride{}.FlexGrow(1).BreakAfter()))
                {
                    float nr, ng, nb;
                    if (HexToColor(cp.hexText, nr, ng, nb))
                        ColorConvert::RGBtoHSV(nr, ng, nb, cp.h, cp.s, cp.v);
                }
            }
        }

        // ---- Preview swatches (old / new) ----
        {
            Element previewRow {};
            previewRow.id       = idManager.AllocateDynamicID("color_picker_previews");
            previewRow.styleRef = theme.colorPickerPreviewsStyleRef;
            previewRow.state    = guiStateManager.GetCurrentState(previewRow.id);
            RegisterElement(std::move(previewRow), {});
            VERIFY(elementStorage.BeginChildren());

            // Size comes from CSS (.color-picker-preview / .color-picker-new); NoGrow/NoShrink holds it.
            LayoutOverride swatchLayout = LayoutOverride{}.NoGrow().NoShrink();
            auto& palette = cp.palette;


            const ID swatchRange = palette.empty() ? ID::Invalid()
                : idManager.AllocateRangedID(static_cast<uint32_t>(palette.size()), "color_picker_swatch");
            for (int i = 0; i < (int)palette.size(); i++)
            {
                ID swatchID = idManager.GetSubID(swatchRange, static_cast<uint32_t>(i));

                // Custom handler: left-click to pick, right-click to remove
                NoteSwatchClaim(swatchID);
                auto& swatchStyle = m_colorPickerSub->ClaimSwatchStyle(swatchID);
                SetColorStyle(styleManager, swatchStyle, palette[i].color);
                Element sw {};
                sw.id               = swatchID;
                sw.styleRef         = theme.colorPickerPreviewStyleRef;
                sw.colorStyleIndex  = swatchStyle;
                sw.state            = guiStateManager.GetCurrentState(swatchID);
                sw.eventHandler = [this, i](ElementEvent&& event) -> bool
                {
                    if (event.type != EventType::MouseDown) return false;
                    auto& cp = m_colorPickerSub->State();
                    auto& palette = cp.palette;
                    if (i >= (int)palette.size()) return false;

                    if (event.state.LeftPressed())
                    {
                        // Pick this swatch color
                        float r = std::clamp(palette[i].color.r, 0.f, 1.f);
                        float g = std::clamp(palette[i].color.g, 0.f, 1.f);
                        float b = std::clamp(palette[i].color.b, 0.f, 1.f);
                        ColorConvert::RGBtoHSV(r, g, b, cp.h, cp.s, cp.v);
                        cp.a = palette[i].color.a;
                        return true;
                    }
                    if (event.state.RightPressed())
                    {
                        // Remove this swatch
                        palette.erase(palette.begin() + i);
                        return true;
                    }
                    return false;
                };
                RegisterControl(std::move(sw), LayoutOverride{swatchLayout});
            }

            // Add button: append current color unless it equals the last swatch
            {
                ID addID = idManager.AllocateDynamicID("color_picker_add_swatch");
                Button(addID, theme.addIcon, [this]()
                {
                    auto& cp = m_colorPickerSub->State();
                    auto [r, g, b] = ColorConvert::HSVtoRGB(cp.h, cp.s, cp.v);
                    color4f newColor { r, g, b, cp.a };

                    auto& palette = cp.palette;
                    if (!palette.empty()
                        && palette.back().color == newColor)
                        return; // already there

                    palette.push_back({ newColor });
                }, theme.colorPickerNewStyleRef, LayoutOverride{swatchLayout});
            }

            VERIFY(elementStorage.EndChildren());
        }

        // Write current HSV back to output color every frame while picker is open
        {
            auto [r, g, b] = interstice::ColorConvert::HSVtoRGB(cp.h, cp.s, cp.v);
            float mult = (allowHDR && cp.hdrBrightness > 1.0f) ? cp.hdrBrightness : 1.0f;
            outColor = { r * mult, g * mult, b * mult, cp.a };
        }
    }

    // ---------------------------------------------------------------------------
    // ColorField — swatch + optional picker popup
    // ---------------------------------------------------------------------------

    bool GUIApplication::ColorField(color4f& value, bool allowHDR, LayoutOverride&& constraints)
    {
        const color4f before = value;

        // ---- Swatch button ----
        ID swatchID = idManager.AllocateDynamicID("color_swatch");
        auto& cp = m_colorPickerSub->State();

        const bool pickerIsOpen = (cp.triggerID == swatchID); // this field's picker, not just any
        NoteSwatchClaim(swatchID);
                auto& swatchStyle = m_colorPickerSub->ClaimSwatchStyle(swatchID);
        SetColorStyle(styleManager, swatchStyle, value);
        RegisterControl(
            interstice::Element
            {
                .id = swatchID,
                // Captures a copy: handlers run after this returns, and `value` may be a temporary.
                .eventHandler = MakeButtonHandler([this, value, swatchID, allowHDR, pickerIsOpen]()
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
                    }
                }),
                .styleRef        = theme.colorSwatchStyleRef,
                .colorStyleIndex = swatchStyle
            },
            std::move(constraints));

        // ---- Picker popup ----
        if (pickerIsOpen && cp.windowID != ID::Invalid())
        {
            auto&& popupPos = LayoutOverride{}.Anchored(swatchID,
                AnchorPlacement::OutsideBottom | AnchorPlacement::InsideLeft,
                {0, int16_t(styleManager.Px(4))}, AutoFlipMode::FlipAvoidTarget);

            if (BeginWindow(cp.windowID, "Color Picker", std::move(popupPos), theme.colorPickerStyleRef,
                            ElementFlags::WindowUserControlled | ElementFlags::Shadows))
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