#include <gui_core/gui_base_types.h>

#include <chrono>
#include <cstring>


namespace interstice
{
    float2x2 float2x2::operator*(float2x2 const& m2)
    {
        return float2x2
        {
            .r0 = { r0[0] * m2[0][0] + r1[0] * m2[0][1], r0[1] * m2[0][0] + r1[1] * m2[0][1] },
            .r1 = { r0[0] * m2[1][0] + r1[0] * m2[1][1], r0[1] * m2[1][0] + r1[1] * m2[1][1] }
        };
    }


    float4x4 float4x4::operator*(float4x4 const& m2) const
    {
        return float4x4
        {
            {
                r0[0] * m2.r0[0] + r0[1] * m2.r1[0] + r0[2] * m2.r2[0] + r0[3] * m2.r3[0],
                r0[0] * m2.r0[1] + r0[1] * m2.r1[1] + r0[2] * m2.r2[1] + r0[3] * m2.r3[1],
                r0[0] * m2.r0[2] + r0[1] * m2.r1[2] + r0[2] * m2.r2[2] + r0[3] * m2.r3[2],
                r0[0] * m2.r0[3] + r0[1] * m2.r1[3] + r0[2] * m2.r2[3] + r0[3] * m2.r3[3]
            },
            {
                r1[0] * m2.r0[0] + r1[1] * m2.r1[0] + r1[2] * m2.r2[0] + r1[3] * m2.r3[0],
                r1[0] * m2.r0[1] + r1[1] * m2.r1[1] + r1[2] * m2.r2[1] + r1[3] * m2.r3[1],
                r1[0] * m2.r0[2] + r1[1] * m2.r1[2] + r1[2] * m2.r2[2] + r1[3] * m2.r3[2],
                r1[0] * m2.r0[3] + r1[1] * m2.r1[3] + r1[2] * m2.r2[3] + r1[3] * m2.r3[3]
            },
            {
                r2[0] * m2.r0[0] + r2[1] * m2.r1[0] + r2[2] * m2.r2[0] + r2[3] * m2.r3[0],
                r2[0] * m2.r0[1] + r2[1] * m2.r1[1] + r2[2] * m2.r2[1] + r2[3] * m2.r3[1],
                r2[0] * m2.r0[2] + r2[1] * m2.r1[2] + r2[2] * m2.r2[2] + r2[3] * m2.r3[2],
                r2[0] * m2.r0[3] + r2[1] * m2.r1[3] + r2[2] * m2.r2[3] + r2[3] * m2.r3[3]
            },
            {
                r3[0] * m2.r0[0] + r3[1] * m2.r1[0] + r3[2] * m2.r2[0] + r3[3] * m2.r3[0],
                r3[0] * m2.r0[1] + r3[1] * m2.r1[1] + r3[2] * m2.r2[1] + r3[3] * m2.r3[1],
                r3[0] * m2.r0[2] + r3[1] * m2.r1[2] + r3[2] * m2.r2[2] + r3[3] * m2.r3[2],
                r3[0] * m2.r0[3] + r3[1] * m2.r1[3] + r3[2] * m2.r2[3] + r3[3] * m2.r3[3]
            }
        };
    }

    // Compose: apply this transform after b  (result = this * b)
    float3x2 float3x2::operator*(const float3x2& b) const
    {
        return float3x2
        {
            .r0 = { r0.x * b.r0.x + r0.y * b.r1.x,  r0.x * b.r0.y + r0.y * b.r1.y,  r0.x * b.r0.z + r0.y * b.r1.z + r0.z },
            .r1 = { r1.x * b.r0.x + r1.y * b.r1.x,  r1.x * b.r0.y + r1.y * b.r1.y,  r1.x * b.r0.z + r1.y * b.r1.z + r1.z }
        };
    }

    float2 float3x2::TransformPoint(float2 p) const
    {
        return { r0.x*p.x + r0.y*p.y + r0.z,
                    r1.x*p.x + r1.y*p.y + r1.z };
    }

    float2 float3x2::TransformVector(float2 v) const
    {
        return { r0.x * v.x + r0.y * v.y,
                    r1.x * v.x + r1.y * v.y };
    }

    // Inverse of a 2D affine transform
    float3x2 float3x2::Inverse() const
    {
        float det = r0.x * r1.y - r0.y * r1.x;
        // Singular (e.g. a zero-scale animating pane): return identity so hit-testing
        // gets finite coordinates instead of NaN, which would kill input for the subtree.
        if (det > -1e-12f && det < 1e-12f)
            return float3x2::Identity();
        float inv = 1.0f / det;
        return float3x2
        {
            .r0 = {  r1.y * inv, -r0.y * inv, (r0.y * r1.z - r1.y * r0.z) * inv },
            .r1 = { -r1.x * inv,  r0.x * inv, (r1.x * r0.z - r0.x * r1.z) * inv }
        };
    }

    
    IntRect IntRect::Inflate(IntRect a, int32_t t, int32_t r, int32_t b, int32_t l)
    {
        auto x1 = a.x - l;
        auto x2 = a.x + a.w + r;
        if (x1 > x2) { x1 = x2 = a.x + a.w / 2; }
        auto y1 = a.y - t;
        auto y2 = a.y + a.h + b;
        if (y1 > y2) { y1 = y2 = a.y + a.h / 2; }
        return IntRect { .x = x1, .y = y1, .w = x2 - x1, .h = y2 - y1 };
    }
       

    IntRect IntRect::Inflate(IntRect a, int32_t n)
    {
        auto x1 = a.x - n;
        auto x2 = a.x + a.w + n;
        if (x1 > x2) { x1 = x2 = a.x + a.w / 2; }
        auto y1 = a.y - n;
        auto y2 = a.y + a.h + n;
        if (y1 > y2) { y1 = y2 = a.y + a.h / 2; }
        return IntRect { .x = x1, .y = y1, .w = x2 - x1, .h = y2 - y1 };
    }

    IntRect IntRect::Deflate(IntRect a, int32_t t, int32_t r, int32_t b, int32_t l)
    {
        auto x1 = a.x + l;
        auto x2 = a.x + a.w - r;
        if (x1 > x2) { x1 = x2 = a.x + a.w / 2; }
        auto y1 = a.y + t;
        auto y2 = a.y + a.h - b;
        if (y1 > y2) { y1 = y2 = a.y + a.h / 2; }
        return IntRect { .x = x1, .y = y1, .w = x2 - x1, .h = y2 - y1 };
    }

    IntRect IntRect::Deflate(IntRect a, int32_t n)
    {
        auto x1 = a.x + n;
        auto x2 = a.x + a.w - n;
        if (x1 > x2) { x1 = x2 = a.x + a.w / 2; }
        auto y1 = a.y + n;
        auto y2 = a.y + a.h - n;
        if (y1 > y2) { y1 = y2 = a.y + a.h / 2; }
        return IntRect { .x = x1, .y = y1, .w = x2 - x1, .h = y2 - y1 };
    }

    IntRect IntRect::Intersection(IntRect a, IntRect b)
    {
        int32_t a_x1 = a.x, a_x2 = a.x + std::max(0, a.w);
        int32_t a_y1 = a.y, a_y2 = a.y + std::max(0, a.h);
        int32_t b_x1 = b.x, b_x2 = b.x + std::max(0, b.w);
        int32_t b_y1 = b.y, b_y2 = b.y + std::max(0, b.h);

		a.x = std::max(a_x1, b_x1);
		a.y = std::max(a_y1, b_y1);
		a.w = std::max(0, std::min(a_x2, b_x2) - a.x);
		a.h = std::max(0, std::min(a_y2, b_y2) - a.y);
        return a;
    }

    IntRect IntRect::Union(IntRect a, IntRect b)
    {
        int32_t a_x1 = a.x, a_x2 = a.x + std::max(0, a.w);
        int32_t a_y1 = a.y, a_y2 = a.y + std::max(0, a.h);
        int32_t b_x1 = b.x, b_x2 = b.x + std::max(0, b.w);
        int32_t b_y1 = b.y, b_y2 = b.y + std::max(0, b.h);

		a.x = std::min(a_x1, b_x1);
		a.y = std::min(a_y1, b_y1);
		a.w = std::max(0, std::max(a_x2, b_x2) - a.x);
		a.h = std::max(0, std::max(a_y2, b_y2) - a.y);
        return a;
    }

    // Apply a 2D affine transform and return the axis-aligned bounding rect of the result
    IntRect IntRect::Transform(IntRect parentRect, const float3x2& m) const
    {
        return Transform(parentRect.position, m);
    }

    IntRect IntRect::Transform(int2 offset, const float3x2& m) const
    {
        IntRect temp = *this;
        temp -= offset;
		temp = temp.Transform(m);
        temp += offset;
        return temp;
    }


    // Apply a 2D affine transform and return the axis-aligned bounding rect of the result
    IntRect IntRect::Transform(const float3x2& m) const
    {
        float2 p0 = m.TransformPoint({ (float)x,       (float)y });
        float2 p1 = m.TransformPoint({ (float)(x + w), (float)y });
        float2 p2 = m.TransformPoint({ (float)x,       (float)(y + h) });
        float2 p3 = m.TransformPoint({ (float)(x + w), (float)(y + h) });
        float minX = std::min({ p0.x, p1.x, p2.x, p3.x });
        float minY = std::min({ p0.y, p1.y, p2.y, p3.y });
        float maxX = std::max({ p0.x, p1.x, p2.x, p3.x });
        float maxY = std::max({ p0.y, p1.y, p2.y, p3.y });
        return IntRect{ (int32_t)minX, (int32_t)minY, (int32_t)(maxX - minX), (int32_t)(maxY - minY) };
    }

    uint16_t FloatToHalf(float value)
    {
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));

        uint32_t sign = (bits >> 31) & 0x1;
        uint32_t exp  = (bits >> 23) & 0xFF;
        uint32_t mant = bits & 0x7FFFFF;

        uint16_t hsign = static_cast<uint16_t>(sign << 15);

        // Handle special cases
        if (exp == 255) // Inf or NaN
        {
            if (mant == 0)
                return hsign | 0x7C00; // Inf
            else
                return hsign | 0x7C00 | static_cast<uint16_t>(mant >> 13) | 1; // NaN
        }

        // Normalize exponent from bias 127 to bias 15
        int32_t newExp = int32_t(exp) - 127 + 15;

        if (newExp >= 31)
        {
            // Overflow -> Inf
            return hsign | 0x7C00;
        }
        else if (newExp <= 0)
        {
            // Subnormal or underflow
            if (newExp < -10)
            {
                // Too small -> zero
                return hsign;
            }

            // Subnormal
            mant |= 0x800000; // Add implicit leading 1

            int shift = 14 - newExp;
            uint16_t hmant = uint16_t(mant >> shift);

            // Round
            if ((mant >> (shift - 1)) & 1)
                hmant++;

            return hsign | hmant;
        }
        else
        {
            // Normalized number
            uint16_t hexp = uint16_t(newExp) << 10;
            uint16_t hmant = uint16_t(mant >> 13);

            // Round to nearest
            if (mant & 0x1000)
                hmant++;

            return hsign | hexp | (hmant & 0x3FF);
        }
    }

    float HalfToFloat(uint16_t value)
    {
        uint32_t sign = (value >> 15) & 0x1;
        uint32_t exp = (value >> 10) & 0x1F;
        uint32_t mant = value & 0x3FF;

        uint32_t fsign = sign << 31;

        uint32_t fexp;
        uint32_t fmant;

        if (exp == 0)
        {
            if (mant == 0)
            {
                // Zero
                fexp = 0;
                fmant = 0;
            } else
            {
                // Subnormal -> normalize
                int shift = 0;
                while ((mant & 0x400) == 0)
                {
                    mant <<= 1;
                    shift++;
                }
                mant &= 0x3FF; // remove leading 1

                fexp = (127 - 15 - shift) << 23;
                fmant = mant << 13;
            }
        } else if (exp == 31)
        {
            // Inf or NaN
            fexp = 0xFF << 23;
            fmant = mant << 13;

            if (fmant == 0)
            {
                // Inf
            } else
            {
                // NaN: make sure mantissa is non-zero
                fmant |= 1;
            }
        } else
        {
            // Normalized
            fexp = (exp - 15 + 127) << 23;
            fmant = mant << 13;
        }

        uint32_t bits = fsign | fexp | fmant;

        float result;
        std::memcpy(&result, &bits, sizeof(result));
        return result;
    }

    int64_t SteadyClockNowNanos()
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }


}// namespace interstice