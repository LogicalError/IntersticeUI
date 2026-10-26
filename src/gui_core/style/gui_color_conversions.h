#pragma once

#include <array>
#include <cmath>
#include <algorithm>

namespace interstice::ColorConvert
{
    // ---- sRGB <-> Linear ----

    inline float SRGBToLinear(float c)
    {
        if (c <= 0.04045f) return c / 12.92f;
        return std::pow((c + 0.055f) / 1.055f, 2.4f);
    }

    inline float LinearToSRGB(float c)
    {
        if (c <= 0.0031308f) return c * 12.92f;
        return 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
    }

    // ---- RGB <-> HSV ----

    inline void RGBtoHSV(float r, float g, float b, float& h, float& s, float& v)
    {
		if (!std::isfinite(r)) r = 0.0f;
        if (!std::isfinite(g)) g = 0.0f;
        if (!std::isfinite(b)) b = 0.0f;

		r = std::max(r, 0.0f);
        g = std::max(g, 0.0f);
        b = std::max(b, 0.0f);

        float maxc = std::max(r, std::max(g, b));
        float minc = std::min(r, std::min(g, b));
        
        float d = maxc - minc;
        v = maxc;
        s = (maxc == 0.0f) ? 0.0f : d / maxc;
        if (d == 0.0f) { h = 0.0f; return; }

		// Since maxc is the max of r,g,b we know maxc MUST be
        // equal to at least one of them so we can do direct 
        // floating point comparisons here.
        if (maxc == r)      h = (g - b) / d + (g < b ? 6.0f : 0.0f);
        else if (maxc == g) h = (b - r) / d + 2.0f;
        else                h = (r - g) / d + 4.0f;
        h /= 6.0f;
    }

    inline std::array<float, 3> HSVtoRGB(float h, float s, float v)
    {
        if (!std::isfinite(h)) h = 0.0f;
        if (!std::isfinite(s)) s = 0.0f;
        if (!std::isfinite(v)) v = 0.0f;

        h = std::max(h, 0.0f);
        s = std::max(s, 0.0f);
        v = std::max(v, 0.0f);

        if (s == 0.0f) return { v, v, v };

        h = h * 6.0f;
        int i = static_cast<int>(std::floor(h)) % 6;
        float f = h - std::floor(h);
        float p = v * (1.0f - s);
        float q = v * (1.0f - s * f);
        float t = v * (1.0f - s * (1.0f - f));
        switch (i)
        {
            case 0: return { v, t, p };
            case 1: return { q, v, p };
            case 2: return { p, v, t };
            case 3: return { p, q, v };
            case 4: return { t, p, v };
            case 5: default: return { v, p, q };
        }
    }

    // ---- RGB <-> HSL ----

    inline void RGBtoHSL(float r, float g, float b, float& h, float& s, float& l)
    {
        if (!std::isfinite(r)) r = 0.0f;
        if (!std::isfinite(g)) g = 0.0f;
        if (!std::isfinite(b)) b = 0.0f;

        r = std::max(r, 0.0f);
        g = std::max(g, 0.0f);
        b = std::max(b, 0.0f);

        float maxc = std::max(r, std::max(g, b));
        float minc = std::min(r, std::min(g, b));

        l = 0.5f * (maxc + minc);
        if (maxc == minc) { h = 0.0f; s = 0.0f; return; }

        float d = maxc - minc;
        s = (l > 0.5f) ? (d / (2.0f - maxc - minc)) : (d / (maxc + minc));

        // Since maxc is the max of r,g,b we know maxc MUST be
        // equal to at least one of them so we can do direct 
        // floating point comparisons here.
        if (maxc == r)      h = (g - b) / d + (g < b ? 6.0f : 0.0f);
        else if (maxc == g) h = (b - r) / d + 2.0f;
        else                h = (r - g) / d + 4.0f;
        h /= 6.0f;
    }

    inline std::array<float, 3> HSLtoRGB(float h, float s, float l)
    {
        if (!std::isfinite(h)) h = 0.0f;
        if (!std::isfinite(s)) s = 0.0f;
        if (!std::isfinite(l)) l = 0.0f;

        h = std::max(h, 0.0f);
        s = std::max(s, 0.0f);
        l = std::max(l, 0.0f);

        if (s == 0.0f) return { l, l, l };

        auto hue2rgb = [](float p, float q, float t) -> float
        {
            if (t < 0.0f) t += 1.0f;
            if (t > 1.0f) t -= 1.0f;
            if (t < 1.0f / 6.0f) return p + (q - p) * 6.0f * t;
            if (t < 1.0f / 2.0f) return q;
            if (t < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
            return p;
        };
        float q = (l < 0.5f) ? (l * (1.0f + s)) : (l + s - l * s);
        float p = 2.0f * l - q;
        return { hue2rgb(p, q, h + 1.0f / 3.0f), hue2rgb(p, q, h), hue2rgb(p, q, h - 1.0f / 3.0f) };
    }

    // ---- RGB <-> CIE Lab ----

    namespace detail
    {
        constexpr float kD65WhiteX = 0.95047f;
        constexpr float kD65WhiteY = 1.00000f;
        constexpr float kD65WhiteZ = 1.08883f;
        constexpr float kLabDelta  = 6.0f / 29.0f;

        inline std::array<float, 3> RGBtoXYZ(float r, float g, float b)
        {
            float R = SRGBToLinear(r), G = SRGBToLinear(g), B = SRGBToLinear(b);
            return {
                R * 0.4124564f + G * 0.3575761f + B * 0.1804375f,
                R * 0.2126729f + G * 0.7151522f + B * 0.0721750f,
                R * 0.0193339f + G * 0.1191920f + B * 0.9503041f
            };
        }

        inline std::array<float, 3> XYZtoRGB(float X, float Y, float Z)
        {
            float R =  3.2404542f * X - 1.5371385f * Y - 0.4985314f * Z;
            float G = -0.9692660f * X + 1.8760108f * Y + 0.0415560f * Z;
            float B =  0.0556434f * X - 0.2040259f * Y + 1.0572252f * Z;
            return { std::clamp(R, 0.0f, 1.0f), std::clamp(G, 0.0f, 1.0f), std::clamp(B, 0.0f, 1.0f) };
        }
    }

    inline void RGBtoLab(float r, float g, float b, float& L, float& a, float& bOut)
    {
        if (!std::isfinite(r)) r = 0.0f;
        if (!std::isfinite(g)) g = 0.0f;
        if (!std::isfinite(b)) b = 0.0f;

        r = std::max(r, 0.0f);
        g = std::max(g, 0.0f);
        b = std::max(b, 0.0f);

        auto [X, Y, Z] = detail::RGBtoXYZ(r, g, b);
        auto f = [](float t) {
            if (t > detail::kLabDelta * detail::kLabDelta * detail::kLabDelta)
                return std::cbrt(t);
            return t / (3.0f * detail::kLabDelta * detail::kLabDelta) + 4.0f / 29.0f;
        };
        float fx = f(X / detail::kD65WhiteX);
        float fy = f(Y / detail::kD65WhiteY);
        float fz = f(Z / detail::kD65WhiteZ);
        L    = 116.0f * fy - 16.0f;
        a    = 500.0f * (fx - fy);
        bOut = 200.0f * (fy - fz);
    }

    inline std::array<float, 3> LabToRGB(float L, float a, float b)
    {
        if (!std::isfinite(L)) L = 0.0f;
        if (!std::isfinite(a)) a = 0.0f;
        if (!std::isfinite(b)) b = 0.0f;

        // a and b are signed opponent-color channels (green<->red, blue<->yellow)
        // and are legitimately negative - only lightness L is clamped to >= 0.
        L = std::max(L, 0.0f);

        auto finv = [](float t) {
            if (t > detail::kLabDelta) return t * t * t;
            return 3.0f * detail::kLabDelta * detail::kLabDelta * (t - 4.0f / 29.0f);
        };
        float fy = (L + 16.0f) / 116.0f;
        float fx = fy + a / 500.0f;
        float fz = fy - b / 200.0f;
        auto [Rl, Gl, Bl] = detail::XYZtoRGB(
            detail::kD65WhiteX * finv(fx),
            detail::kD65WhiteY * finv(fy),
            detail::kD65WhiteZ * finv(fz));
        return { std::clamp(LinearToSRGB(Rl), 0.0f, 1.0f),
                 std::clamp(LinearToSRGB(Gl), 0.0f, 1.0f),
                 std::clamp(LinearToSRGB(Bl), 0.0f, 1.0f) };
    }

    // ---- RGB <-> Oklab ----

    inline void RGBtoOklab(float r, float g, float b, float& L, float& a, float& bOut)
    {
        if (!std::isfinite(r)) r = 0.0f;
        if (!std::isfinite(g)) g = 0.0f;
        if (!std::isfinite(b)) b = 0.0f;

        r = std::max(r, 0.0f);
        g = std::max(g, 0.0f);
        b = std::max(b, 0.0f);

        float R = SRGBToLinear(r), G = SRGBToLinear(g), B = SRGBToLinear(b);
        float l = 0.4122214708f * R + 0.5363325363f * G + 0.0514459929f * B;
        float m = 0.2119034982f * R + 0.6806995451f * G + 0.1073969566f * B;
        float s = 0.0883024619f * R + 0.2817188376f * G + 0.6299787005f * B;
        float l_ = std::cbrt(l), m_ = std::cbrt(m), s_ = std::cbrt(s);
        L    =  0.2104542553f * l_ + 0.7936177850f * m_ - 0.0040720468f * s_;
        a    =  1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_;
        bOut =  0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_;
    }

    inline std::array<float, 3> OklabToRGB(float L, float a, float b)
    {
        if (!std::isfinite(L)) L = 0.0f;
        if (!std::isfinite(a)) a = 0.0f;
        if (!std::isfinite(b)) b = 0.0f;

        // a and b are signed opponent-color channels (green<->red, blue<->yellow)
        // and are legitimately negative - only lightness L is clamped to >= 0.
        L = std::max(L, 0.0f);

        float l_ = L + 0.3963377774f * a + 0.2158037573f * b;
        float m_ = L - 0.1055613458f * a - 0.0638541728f * b;
        float s_ = L - 0.0894841775f * a - 1.2914855480f * b;
        float l = l_ * l_ * l_, m = m_ * m_ * m_, s = s_ * s_ * s_;
        float R = +4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s;
        float G = -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s;
        float B = -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s;
        return { std::clamp(LinearToSRGB(std::clamp(R, 0.0f, 1.0f)), 0.0f, 1.0f),
                 std::clamp(LinearToSRGB(std::clamp(G, 0.0f, 1.0f)), 0.0f, 1.0f),
                 std::clamp(LinearToSRGB(std::clamp(B, 0.0f, 1.0f)), 0.0f, 1.0f) };
    }
}
