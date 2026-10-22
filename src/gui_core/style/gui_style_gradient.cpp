#include <gui_core/style/gui_style_gradient.h>
#include <gui_core/style/gui_color_conversions.h>

#include <algorithm>
#include <cmath>
#include <array>
#include <vector>

namespace GUI
{
using namespace ColorConvert;
void Gradient::WriteTo(std::span<color4b> out) const
{
    if (out.empty())
        return;

    size_t size = out.size();

    if (stopCount == 0)
    {
        // Fill transparent black if no stops
        for (size_t i = 0; i < size; ++i)
            out[i] = color4b{ 0, 0, 0, 0 };
        return;
    }

    // Ensure stops are sorted by offset (stable)
    std::array<ColorStop, kMaxStops> sorted = stops;
    std::sort(sorted.begin(), sorted.begin() + stopCount,
              [](const ColorStop& a, const ColorStop& b)
              {
                  return a.GetOffset() < b.GetOffset();
              });

    // Clamp offsets to [0,1] for first/last stops
    sorted[0].SetOffset(std::max(0.0f, std::min(1.0f, sorted[0].GetOffset())));
    sorted[stopCount - 1].SetOffset(std::max(0.0f, std::min(1.0f, sorted[stopCount - 1].GetOffset())));

    // Interpolated sRGB color at t in [0,1].
    auto sample = [&](float t) -> color4f
    {
        t = std::max(0.0f, std::min(1.0f, t));

        // If before first stop or exactly equal
        if (t <= sorted[0].GetOffset())
            return sorted[0].GetColor4f();
        if (t >= sorted[stopCount - 1].GetOffset())
            return sorted[stopCount - 1].GetColor4f();

        // Find neighboring stops
        size_t idx = 0;
        for (size_t i = 0; i + 1 < stopCount; ++i)
        {
            if (t >= sorted[i].GetOffset() && t <= sorted[i + 1].GetOffset())
            {
                idx = i;
                break;
            }
        }

        const ColorStop& a = sorted[idx];
        const ColorStop& b = sorted[idx + 1];

        float range = b.GetOffset() - a.GetOffset();
        float local_t = (range <= 0.0f) ? 0.0f : (t - a.GetOffset()) / range;
        local_t = std::max(0.0f, std::min(1.0f, local_t));


        auto af = a.GetColor4f();
        auto bf = b.GetColor4f();

        // Interpolate alpha linearly in sRGB space
        float outA = af.a * (1.0f - local_t) + bf.a * local_t;

        // Colours interpolate premultiplied by alpha (as in CSS), so fading to a
        // transparent stop keeps the colour instead of darkening. For a linear
        // interpolation that is the same as weighting each stop by its alpha.
        {
            const float wa = af.a * (1.0f - local_t);
            const float wb = bf.a * local_t;
            if (wa + wb > 0.0f) local_t = wb / (wa + wb);
        }

        // Interpolate RGB depending on color_space
        switch (colorSpace)
        {
            case ColorSpace::sRGB:
                return color4f{
                    af.r * (1.0f - local_t) + bf.r * local_t,
                    af.g * (1.0f - local_t) + bf.g * local_t,
                    af.b * (1.0f - local_t) + bf.b * local_t,
                    outA
                };

            case ColorSpace::LinearRGB:
            {
                float lr = SRGBToLinear(af.r) * (1.0f - local_t) + SRGBToLinear(bf.r) * local_t;
                float lg = SRGBToLinear(af.g) * (1.0f - local_t) + SRGBToLinear(bf.g) * local_t;
                float lb = SRGBToLinear(af.b) * (1.0f - local_t) + SRGBToLinear(bf.b) * local_t;
                return color4f{ LinearToSRGB(lr), LinearToSRGB(lg), LinearToSRGB(lb), outA };
            }

            case ColorSpace::HSL:
            {
                float ha, sa, la, hb_, sb, lb_;
                RGBtoHSL(af.r, af.g, af.b, ha, sa, la);
                RGBtoHSL(bf.r, bf.g, bf.b, hb_, sb, lb_);

                float dh = hb_ - ha;
                if (dh > 0.5f) dh -= 1.0f;
                else if (dh < -0.5f) dh += 1.0f;
                float h = ha + dh * local_t;
                if (h < 0.0f) h += 1.0f; else if (h > 1.0f) h -= 1.0f;
                float s = sa * (1.0f - local_t) + sb * local_t;
                float l = la * (1.0f - local_t) + lb_ * local_t;

                auto rgb = HSLtoRGB(h, s, l);
                return color4f{ rgb[0], rgb[1], rgb[2], outA };
            }

            case ColorSpace::HSV:
            {
                float ha, sa, va, hb_, sb, vb;
                RGBtoHSV(af.r, af.g, af.b, ha, sa, va);
                RGBtoHSV(bf.r, bf.g, bf.b, hb_, sb, vb);

                float dh = hb_ - ha;
                if (dh > 0.5f) dh -= 1.0f;
                else if (dh < -0.5f) dh += 1.0f;
                float h = ha + dh * local_t;
                if (h < 0.0f) h += 1.0f; else if (h > 1.0f) h -= 1.0f;
                float s = sa * (1.0f - local_t) + sb * local_t;
                float v = va * (1.0f - local_t) + vb * local_t;

                auto rgb = HSVtoRGB(h, s, v);
                return color4f{ rgb[0], rgb[1], rgb[2], outA };
            }

            case ColorSpace::Lab:
            {
                float La, aa_, ba_, Lb, ab_, bb_;
                RGBtoLab(af.r, af.g, af.b, La, aa_, ba_);
                RGBtoLab(bf.r, bf.g, bf.b, Lb, ab_, bb_);

                float L  = La  * (1.0f - local_t) + Lb  * local_t;
                float aa = aa_ * (1.0f - local_t) + ab_ * local_t;
                float bb = ba_ * (1.0f - local_t) + bb_ * local_t;

                auto rgb = LabToRGB(L, aa, bb);
                return color4f{ rgb[0], rgb[1], rgb[2], outA };
            }

            case ColorSpace::Oklab:
            {
                float La, aa_, ba_, Lb, ab_, bb_;
                RGBtoOklab(af.r, af.g, af.b, La, aa_, ba_);
                RGBtoOklab(bf.r, bf.g, bf.b, Lb, ab_, bb_);

                float L  = La  * (1.0f - local_t) + Lb  * local_t;
                float aa = aa_ * (1.0f - local_t) + ab_ * local_t;
                float bb = ba_ * (1.0f - local_t) + bb_ * local_t;

                auto rgb = OklabToRGB(L, aa, bb);
                return color4f{ rgb[0], rgb[1], rgb[2], outA };
            }

            default:
                return color4f{
                    af.r * (1.0f - local_t) + bf.r * local_t,
                    af.g * (1.0f - local_t) + bf.g * local_t,
                    af.b * (1.0f - local_t) + bf.b * local_t,
                    outA
                };
        }
    };

    // Helper to pack float [0..1] into uint8 0..255 with rounding and clamping
    auto pack = [](float v) -> uint8_t
    {
        v = std::max(0.0f, std::min(1.0f, v));
        int iv = static_cast<int>(std::lround(v * 255.0f));
        if (iv < 0) iv = 0;
        else if (iv > 255) iv = 255;
        return static_cast<uint8_t>(iv);
    };

    // Fill the span. We map sample indices to t in [0,1].
    for (size_t i = 0; i < size; ++i)
    {
        float t = (size == 1) ? 0.0f : static_cast<float>(i) / static_cast<float>(size - 1);
        color4f c = sample(t); // sRGB floats in [0..1]
        out[i] = color4b{ pack(c.r), pack(c.g), pack(c.b), pack(c.a) };
    }
}
} // namespace GUI