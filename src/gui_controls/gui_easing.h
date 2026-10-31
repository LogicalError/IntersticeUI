#pragma once

namespace tachyon
{
    /// @addtogroup app
    /// @{

    /// A CSS cubic-bezier(x1, y1, x2, y2) timing curve: maps the share of a transition's time
    /// that has passed (0..1) to how far its value has gone (0..1).
    struct CubicBezier
    {
        float x1 = 0.0f, y1 = 0.0f, x2 = 1.0f, y2 = 1.0f;

        /// How far the value has gone at time `t`.
        float operator()(float t) const
        {
            if (t <= 0.0f) return 0.0f;
            if (t >= 1.0f) return 1.0f;
            return Y(ParamForX(t));
        }

        /// The time at which the value has gone `value` of the way: the inverse of operator(),
        /// for curves whose y only grows (every curve below).
        float TimeAt(float value) const
        {
            if (value <= 0.0f) return 0.0f;
            if (value >= 1.0f) return 1.0f;
            float lo = 0.0f, hi = 1.0f;
            for (int i = 0; i < 32; ++i)
            {
                const float mid = 0.5f * (lo + hi);
                (Y(mid) < value ? lo : hi) = mid;
            }
            return X(0.5f * (lo + hi));
        }

        bool operator==(const CubicBezier&) const = default;

    private:
        // The curve runs from (0, 0) to (1, 1) through the two control points.
        static float Bezier(float a, float b, float s)
        {
            const float r = 1.0f - s;
            return 3.0f * r * r * s * a + 3.0f * r * s * s * b + s * s * s;
        }
        float X(float s) const { return Bezier(x1, x2, s); }
        float Y(float s) const { return Bezier(y1, y2, s); }

        // x grows with s (x1 and x2 are in [0, 1]), so halving finds it.
        float ParamForX(float x) const
        {
            float lo = 0.0f, hi = 1.0f;
            for (int i = 0; i < 32; ++i)
            {
                const float mid = 0.5f * (lo + hi);
                (X(mid) < x ? lo : hi) = mid;
            }
            return 0.5f * (lo + hi);
        }
    };

    namespace Easing
    {
        inline constexpr CubicBezier Linear    { 0.0f, 0.0f, 1.0f, 1.0f };
        inline constexpr CubicBezier EaseOut   { 0.2f, 0.0f, 0.0f, 1.0f };   ///< fast start, long settle
        inline constexpr CubicBezier EaseInOut { 0.4f, 0.0f, 0.2f, 1.0f };   ///< slow at both ends
    }

    /// @}
} // namespace tachyon
