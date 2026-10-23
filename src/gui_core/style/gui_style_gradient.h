#pragma once

#include <gui_core/gui_base_types.h>

#include <span>
#include <cmath>
#include <array>

namespace interstice
{
    // ------------------------------------------------------------------------
    // Color and Gradient Types
    // ------------------------------------------------------------------------

    // Color stops for gradients
    struct ColorStop
    {
    private:
        color4b  color = {};
        uint16_t offset = 0; // 0.0 to 1.0

    public:
        bool operator==(const ColorStop& other) const noexcept = default;

        ColorStop& SetColor(color4b newColor) { color = newColor; return *this; }

        ColorStop& SetColor(color4f newColor)
        {
            newColor.r = (newColor.r < 0) ? 0 : (newColor.r > 1) ? 1 : newColor.r;
            newColor.g = (newColor.g < 0) ? 0 : (newColor.g > 1) ? 1 : newColor.g;
            newColor.b = (newColor.b < 0) ? 0 : (newColor.b > 1) ? 1 : newColor.b;
            newColor.a = (newColor.a < 0) ? 0 : (newColor.a > 1) ? 1 : newColor.a;
            auto pack = [](float value) -> byte
            {
                int iv = static_cast<int>(std::lround(value * 255.0f));
                iv = std::clamp(iv, 0, 255);
                return static_cast<byte>(iv);
            };
            color = color4b{
                .r = pack(newColor.r),
                .g = pack(newColor.g),
                .b = pack(newColor.b),
                .a = pack(newColor.a)
            };
            return *this;
        }

        ColorStop& SetOffset(float newOffset)
        {
            if (newOffset < 0.0f) newOffset = 0.0f;
            if (newOffset >= 1.0f) newOffset = 1.0f;
            offset = static_cast<uint16_t>(newOffset * 65535.0f);
            return *this;
        }

        float GetOffset() const { return (offset / 65535.0f); }
        color4f GetColor4f() const { return color4f { color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f }; }
        color4b GetColor4b() const { return color; }
    };

    enum class GradientType : uint8_t
    {
        None,
        Linear,
        Radial
    };

    // Color spaces for gradient interpolation
    enum class ColorSpace : uint8_t
    {
        sRGB,       // Default - standard sRGB interpolation
        LinearRGB,  // Linear RGB interpolation
        HSL,        // Hue-Saturation-Lightness interpolation
        HSV,        // Hue-Saturation-Value interpolation
        Lab,        // CIE Lab interpolation (perceptual)
        Oklab       // Oklab interpolation (perceptual)
    };

    struct Gradient
    {
        static constexpr int kMaxStops = 8;
        
        std::array<ColorStop, kMaxStops> stops{};
        uint16_t     value      = 0; // Angle for linear, Radius for radial
        uint8_t      stopCount  = 0;
        GradientType type       = GradientType::None;
        ColorSpace   colorSpace = ColorSpace::sRGB;

        float GetValue() const 
        {
            // multiply it by even number so that 90 degrees is exactly 0.25, 180 is 0.5, etc.
            return float(value) / 65534.0f;
        }

        // Convenience builders for common patterns
        static Gradient Solid(const color4f& color)
        {
            Gradient g;
            g.type = GradientType::Linear;
            g.AddStop(color, 0.0f);
            g.AddStop(color, 1.0f);
            return g;
        }

        static Gradient Solid(const color4b& color)
        {
            Gradient g;
            g.type = GradientType::Linear;
            g.AddStop(color, 0.0f);
            g.AddStop(color, 1.0f);
            return g;
        }

        static Gradient Linear(const color4f& c0, const color4f& c1)
        {
            Gradient g;
            g.type = GradientType::Linear;
            g.AddStop(c0, 0.0f);
            g.AddStop(c1, 1.0f);
            return g;
        }

        static Gradient Linear(const color4b& c0, const color4b& c1)
        {
            Gradient g;
            g.type = GradientType::Linear;
            g.AddStop(c0, 0.0f);
            g.AddStop(c1, 1.0f);
            return g;
        }

        static Gradient Linear(const color4f& c0, const color4f& c1, const color4f& c2)
        {
            Gradient g;
            g.type = GradientType::Linear;
            g.AddStop(c0, 0.0f);
            g.AddStop(c1, 0.5f);
            g.AddStop(c2, 1.0f);
            return g;
        }

        static Gradient Linear(const color4b& c0, const color4b& c1, const color4b& c2)
        {
            Gradient g;
            g.type = GradientType::Linear;
            g.AddStop(c0, 0.0f);
            g.AddStop(c1, 0.5f);
            g.AddStop(c2, 1.0f);
            return g;
        }

        static Gradient Linear(const color4f& c0, const color4f& c1, const color4f& c2, const color4f& c3)
        {
            Gradient g;
            g.type = GradientType::Linear;
            g.AddStop(c0, 0.00f);
            g.AddStop(c1, 0.25f);
            g.AddStop(c2, 0.50f);
            g.AddStop(c3, 1.00f);
            return g;
        }

        static Gradient Linear(const color4b& c0, const color4b& c1, const color4b& c2, const color4b& c3)
        {
            Gradient g;
            g.type = GradientType::Linear;
            g.AddStop(c0, 0.00f);
            g.AddStop(c1, 0.25f);
            g.AddStop(c2, 0.50f);
            g.AddStop(c3, 1.00f);
            return g;
        }

        // Angle as a fraction of a full turn in [0, 1), wrapping negative angles
        // (-90deg == 270deg). Scaled by an even number so that 90 degrees is
        // exactly 0.25, 180 is 0.5, etc.; the visual style turns this into a 2x2 transform.
        static uint16_t EncodeAngle(float angleDegrees)
        {
            double whole;
            double turn = std::modf(double(angleDegrees) / 360.0, &whole);
            if (turn < 0.0) turn += 1.0;
            return static_cast<uint16_t>(turn * 65534.0);
        }

        static Gradient Linear(float angleDegrees = 0.0f)
        {
            Gradient g;
            g.type  = GradientType::Linear;
            g.value = EncodeAngle(angleDegrees);
            return g;
        }

        static Gradient Radial(float angleDegrees = 1.0f)
        {
            Gradient g;
            g.type  = GradientType::Radial;
            g.value = EncodeAngle(angleDegrees);
            return g;
        }

        // Fluent interface for building gradients
        inline Gradient& WithColorSpace(ColorSpace cs) { colorSpace = cs; return *this; }
        inline ColorSpace GetColorSpace() const { return colorSpace; }

        // Add a color stop. Returns false if the stop list is full.
        inline bool AddStop(const color4f& color, float offset)
        {
            if (stopCount >= kMaxStops)
                return false;
            stops[stopCount++] = ColorStop {}.SetColor(color).SetOffset(offset);
            return true;
        }

        inline bool AddStop(const color4b& color, float offset)
        {
            if (stopCount >= kMaxStops)
                return false;
            stops[stopCount++] = ColorStop{}.SetColor(color).SetOffset(offset);
            return true;
        }

        // Clear all stops
        inline void ClearStops() { stopCount = 0; }

        // Check if gradient is effectively empty/transparent
        inline bool IsEmpty() const { return type == GradientType::None || stopCount == 0; }

        // Write the gradient into a preallocated span of `byte4` values.
        // Output is sRGB encoded and packed to 0..255 per channel.
        void WriteTo(std::span<color4b> out) const;

        bool operator==(const Gradient& other) const noexcept
        {
            if (type != other.type || stopCount != other.stopCount || 
                value != other.value || colorSpace != other.colorSpace)
                return false;
            
            for (uint8_t i = 0; i < stopCount; ++i)
                if (stops[i] != other.stops[i])
                    return false;
            
            return true;
        }
    };

}//namespace interstice