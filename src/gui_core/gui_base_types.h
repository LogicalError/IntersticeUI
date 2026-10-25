#pragma once

#include <stdint.h>
#include <assert.h>
#include <algorithm>
#include <functional>


namespace interstice
{
    using OnGuiFunc = std::function<void()>;

    struct int2
    {
        int32_t x = 0, y = 0;
        
        inline int32_t&       operator[](int index)       { assert(index >= 0 && index < 2); return (&x)[index]; }
        inline const int32_t& operator[](int index) const { assert(index >= 0 && index < 2); return (&x)[index]; }
        inline bool operator==(const int2& other) const { return x == other.x && y == other.y; }
        inline bool operator!=(const int2& other) const { return !(*this == other); }

        inline int2 operator-() const { return int2{ -x, -y }; }
        inline int2 operator+() const { return int2{  x,  y }; }

        inline int2  operator- (const int2& other) const { return int2 { x - other.x, y - other.y }; }
        inline int2  operator+ (const int2& other) const { return int2 { x + other.x, y + other.y }; }
        inline int2& operator-=(const int2& other) { x -= other.x; y -= other.y; return *this; }
        inline int2& operator+=(const int2& other) { x += other.x; y += other.y; return *this; }
    };

    struct int3
    {
        int32_t x = 0, y = 0, z = 0;
        
        inline int32_t&       operator[](int index)       { assert(index >= 0 && index < 3); return (&x)[index]; }
        inline const int32_t& operator[](int index) const { assert(index >= 0 && index < 3); return (&x)[index]; }
        inline bool operator==(const int3& other) const { return x == other.x && y == other.y && z == other.z; }
        inline bool operator!=(const int3& other) const { return !(*this == other); }

        inline int3 operator-() const { return int3{ -x, -y, -z }; }
        inline int3 operator+() const { return int3{  x,  y,  z }; }

        inline int3  operator- (const int3& other) const { return int3 { x - other.x, y - other.y, z - other.z }; }
        inline int3  operator+ (const int3& other) const { return int3 { x + other.x, y + other.y, z + other.z }; }
        inline int3& operator-=(const int3& other) { x -= other.x; y -= other.y; z -= other.z; return *this; }
        inline int3& operator+=(const int3& other) { x += other.x; y += other.y; z += other.z; return *this; }
    };

    struct int4
    {
        int32_t x = 0, y = 0, z = 0, w = 0;
        
        inline int32_t&       operator[](int index)       { assert(index >= 0 && index < 4); return (&x)[index]; }
        inline const int32_t& operator[](int index) const { assert(index >= 0 && index < 4); return (&x)[index]; }
        inline bool operator==(const int4& other) const { return x == other.x && y == other.y && z == other.z && w == other.w; }
        inline bool operator!=(const int4& other) const { return !(*this == other); }

        inline int4 operator-() const { return int4{ -x, -y, -z, -w }; }
        inline int4 operator+() const { return int4{  x,  y,  z,  w }; }

        inline int4  operator- (const int4& other) const { return int4 { x - other.x, y - other.y, z - other.z, w - other.w }; }
        inline int4  operator+ (const int4& other) const { return int4 { x + other.x, y + other.y, z + other.z, w + other.w }; }
        inline int4& operator-=(const int4& other) { x -= other.x; y -= other.y; z -= other.z; w -= other.w; return *this; }
        inline int4& operator+=(const int4& other) { x += other.x; y += other.y; z += other.z; w += other.w; return *this; }
    };



    struct uint2
    {
        uint32_t x = 0, y = 0;
        
        inline uint32_t&       operator[](int index)       { assert(index >= 0 && index < 2); return (&x)[index]; }
        inline const uint32_t& operator[](int index) const { assert(index >= 0 && index < 2); return (&x)[index]; }
        inline bool operator==(const uint2& other) const { return x == other.x && y == other.y; }
        inline bool operator!=(const uint2& other) const { return !(*this == other); }

        inline uint2  operator- (const uint2& other) const { return uint2 { x - other.x, y - other.y }; }
        inline uint2  operator+ (const uint2& other) const { return uint2 { x + other.x, y + other.y }; }
        inline uint2& operator-=(const uint2& other) { x -= other.x; y -= other.y; return *this; }
        inline uint2& operator+=(const uint2& other) { x += other.x; y += other.y; return *this; }
    };

    struct uint3
    {
        uint32_t x = 0, y = 0, z = 0;
        
        inline uint32_t&       operator[](int index)       { assert(index >= 0 && index < 3); return (&x)[index]; }
        inline const uint32_t& operator[](int index) const { assert(index >= 0 && index < 3); return (&x)[index]; }
        inline bool operator==(const uint3& other) const { return x == other.x && y == other.y && z == other.z; }
        inline bool operator!=(const uint3& other) const { return !(*this == other); }
        
        inline uint3  operator- (const uint3& other) const { return uint3 { x - other.x, y - other.y, z - other.z }; }
        inline uint3  operator+ (const uint3& other) const { return uint3 { x + other.x, y + other.y, z + other.z }; }
        inline uint3& operator-=(const uint3& other) { x -= other.x; y -= other.y; z -= other.z; return *this; }
        inline uint3& operator+=(const uint3& other) { x += other.x; y += other.y; z += other.z; return *this; }
    };

    struct uint4
    {
        uint32_t x = 0, y = 0, z = 0, w = 0;
        
        inline uint32_t&       operator[](int index)       { assert(index >= 0 && index < 4); return (&x)[index]; }
        inline const uint32_t& operator[](int index) const { assert(index >= 0 && index < 4); return (&x)[index]; }
        inline bool operator==(const uint4& other) const { return x == other.x && y == other.y && z == other.z && w == other.w; }
        inline bool operator!=(const uint4& other) const { return !(*this == other); }

        inline uint4  operator- (const uint4& other) const { return uint4 { x - other.x, y - other.y, z - other.z, w - other.w }; }
        inline uint4  operator+ (const uint4& other) const { return uint4 { x + other.x, y + other.y, z + other.z, w + other.w }; }
        inline uint4& operator-=(const uint4& other) { x -= other.x; y -= other.y; z -= other.z; w -= other.w; return *this; }
        inline uint4& operator+=(const uint4& other) { x += other.x; y += other.y; z += other.z; w += other.w; return *this; }
    };


    struct float2
    {
        float x = 0, y = 0;
        
        inline float&       operator[](int index)       { assert(index >= 0 && index < 2); return (&x)[index]; }
        inline const float& operator[](int index) const { assert(index >= 0 && index < 2); return (&x)[index]; }

        inline float2 operator-() const { return float2{ -x, -y }; }
        inline float2 operator+() const { return float2{  x,  y }; }
        
        inline float2  operator- (const int2& other) const { return float2{ x - other.x, y - other.y }; }
        inline float2  operator+ (const int2& other) const { return float2{ x + other.x, y + other.y }; }
        inline float2& operator-=(const int2& other) { x -= other.x; y -= other.y; return *this; }
        inline float2& operator+=(const int2& other) { x += other.x; y += other.y; return *this; }

        inline float2  operator- (const float2& other) const { return float2{ x - other.x, y - other.y }; }
        inline float2  operator+ (const float2& other) const { return float2{ x + other.x, y + other.y }; }
        inline float2& operator-=(const float2& other) { x -= other.x; y -= other.y; return *this; }
        inline float2& operator+=(const float2& other) { x += other.x; y += other.y; return *this; }

        inline float2  operator* (float other) const { return float2{ x * other, y * other }; }
        inline float2  operator/ (float other) const { return float2{ x / other, y / other }; }
        inline float2& operator*=(float other) { x *= other; y *= other; return *this; }
        inline float2& operator/=(float other) { x /= other; y /= other; return *this; }
        
        inline bool operator==(const float2& other) const { return x == other.x && y == other.y; }
        inline bool operator!=(const float2& other) const { return !(*this == other); }
    };

    struct float3
    {
        float x = 0, y = 0, z = 0;
        
        inline float&       operator[](int index)       { assert(index >= 0 && index < 3); return (&x)[index]; }
        inline const float& operator[](int index) const { assert(index >= 0 && index < 3); return (&x)[index]; }

        inline float3 operator-() const { return float3{ -x, -y, -z }; }
        inline float3 operator+() const { return float3{  x,  y,  z }; }

        inline float3  operator- (const float3& other) const { return float3{ x - other.x, y - other.y, z - other.z }; }
        inline float3  operator+ (const float3& other) const { return float3{ x + other.x, y + other.y, z + other.z }; }
        inline float3& operator-=(const float3& other) { x -= other.x; y -= other.y; z -= other.z; return *this; }
        inline float3& operator+=(const float3& other) { x += other.x; y += other.y; z += other.z; return *this; }

        inline float3  operator* (float other) const { return float3{ x * other, y * other, z * other }; }
        inline float3  operator/ (float other) const { return float3{ x / other, y / other, z / other }; }
        inline float3& operator*=(float other) { x *= other; y *= other; z *= other; return *this; }
        inline float3& operator/=(float other) { x /= other; y /= other; z /= other; return *this; }
        
        inline bool operator==(const float3& other) const { return x == other.x && y == other.y && z == other.z; }
        inline bool operator!=(const float3& other) const { return !(*this == other); }
    };

    struct float4
    {
        float x = 0, y = 0, z = 0, w = 0;
        
        inline float&       operator[](int index)       { assert(index >= 0 && index < 4); return (&x)[index]; }
        inline const float& operator[](int index) const { assert(index >= 0 && index < 4); return (&x)[index]; }

        inline float4 operator-() const { return float4{ -x, -y, -z, -w }; }
        inline float4 operator+() const { return float4{  x,  y,  z,  w }; }

        inline float4  operator- (const float4& other) const { return float4{ x - other.x, y - other.y, z - other.z, w - other.w }; }
        inline float4  operator+ (const float4& other) const { return float4{ x + other.x, y + other.y, z + other.z, w + other.w }; }
        inline float4& operator-=(const float4& other) { x -= other.x; y -= other.y; z -= other.z; w -= other.w; return *this; }
        inline float4& operator+=(const float4& other) { x += other.x; y += other.y; z += other.z; w += other.w; return *this; }

        inline float4  operator* (float other) const { return float4{ x * other, y * other, z * other, w * other }; }
        inline float4  operator/ (float other) const { return float4{ x / other, y / other, z / other, w / other }; }
        inline float4& operator*=(float other) { x *= other; y *= other; z *= other; w *= other; return *this; }
        inline float4& operator/=(float other) { x /= other; y /= other; z /= other; w /= other; return *this; }
        
        inline bool operator==(const float4& other) const { return x == other.x && y == other.y && z == other.z && w == other.w; }
        inline bool operator!=(const float4& other) const { return !(*this == other); }

        inline float4 operator*(float const scale)
        {
			return float4{x * scale, y * scale, z * scale, w * scale};
        }
    };

    struct float2x2
    {
        float2 r0 = float2{1,0}, r1 = float2{0,1};

        inline float2& operator[](int index) { assert(index >= 0 && index < 2); return (&r0)[index]; }
        inline const float2& operator[](int index) const { assert(index >= 0 && index < 2); return (&r0)[index]; }

        inline bool operator==(const float2x2& other) const { return r0 == other.r0 && r1 == other.r1; }
        inline bool operator!=(const float2x2& other) const { return !(*this == other); }

        float2x2 operator*(float2x2 const& m2);
    };

    // 2D affine transform matrix (2 rows x 3 columns):
    //   x' = r0.x*x + r0.y*y + r0.z
    //   y' = r1.x*x + r1.y*y + r1.z
    struct float3x2
    {
        float3 r0 = float3 { 1,0,0 }, r1 = float3 { 0,1,0 };

        inline float3& operator[](int index) { assert(index >= 0 && index < 2); return (&r0)[index]; }
        inline const float3& operator[](int index) const { assert(index >= 0 && index < 2); return (&r0)[index]; }

        inline bool operator==(const float3x2& other) const { return r0 == other.r0 && r1 == other.r1; }
        inline bool operator!=(const float3x2& other) const { return !(*this == other); }

        static constexpr float3x2 Identity() { return float3x2{}; }

        inline bool IsIdentity() const
        {
            return r0.x == 1.0f && r0.y == 0.0f && r0.z == 0.0f &&
                   r1.x == 0.0f && r1.y == 1.0f && r1.z == 0.0f;
        }

        // Compose: apply this transform after b  (result = this * b)
        float3x2 operator*(const float3x2& b) const;


        inline static float3x2 Scale(int2 scale) { return { { (float)scale.x, 0, 0 }, { 0, (float)scale.y, 0 } }; }
        inline static float3x2 Scale(int x, int y) { return { { (float)x, 0, 0 }, { 0, (float)y, 0 } }; }
        inline static float3x2 Scale(int s) { return { { (float)s, 0, 0 }, { 0, (float)s, 0 } }; }
        inline static float3x2 Translate(int2 translation) { return { { 1, 0, (float)translation.x }, { 0, 1, (float)translation.y } }; }
        inline static float3x2 Translate(int x, int y) { return { { 1, 0, (float)x }, { 0, 1, (float)y } }; }

        inline static float3x2 Scale(float2 scale) { return { { scale.x, 0, 0 }, { 0, scale.y, 0 } }; }
        inline static float3x2 Scale(float x, float y) { return { { x, 0, 0 }, { 0, y, 0 } }; }
        inline static float3x2 Scale(float s) { return { { s, 0, 0 }, { 0, s, 0 } }; }
        inline static float3x2 Translate(float2 translation) { return { { 1, 0, translation.x }, { 0, 1, translation.y } }; }
        inline static float3x2 Translate(float x, float y) { return { { 1, 0, x }, { 0, 1, y } }; }

        float2 TransformPoint(float2 p) const;
        float2 TransformVector(float2 p) const;

        float3x2 Inverse() const;
    };

    struct float4x4
    {
        float4 r0 = float4{1,0,0,0}, r1 = float4{0,1,0,0}, r2 = float4{0,0,1,0}, r3 = float4{0,0,0,1};

        inline float4& operator[](int index) { assert(index >= 0 && index < 4); return (&r0)[index]; }
        inline const float4& operator[](int index) const { assert(index >= 0 && index < 4); return (&r0)[index]; }

        inline bool operator==(const float4x4& other) const { return r0 == other.r0 && r1 == other.r1 && r2 == other.r2 && r3 == other.r3; }
        inline bool operator!=(const float4x4& other) const { return !(*this == other); }


        float4x4 operator*(float4x4 const& m2) const;
    };



    using byte = unsigned char;

    struct byte2
    {
        byte x = 0, y = 0;
        
        inline byte&       operator[](int index)       { assert(index >= 0 && index < 2); return (&x)[index]; }
        inline const byte& operator[](int index) const { assert(index >= 0 && index < 2); return (&x)[index]; }
        
        inline bool operator==(const byte2& other) const { return x == other.x && y == other.y; }
        inline bool operator!=(const byte2& other) const { return !(*this == other); }
    };

    struct byte3
    {
        byte x = 0, y = 0, z = 0;
        
        inline byte&       operator[](int index)       { assert(index >= 0 && index < 3); return (&x)[index]; }
        inline const byte& operator[](int index) const { assert(index >= 0 && index < 3); return (&x)[index]; }
        
        inline bool operator==(const byte3& other) const { return x == other.x && y == other.y && z == other.z; }
        inline bool operator!=(const byte3& other) const { return !(*this == other); }
    };

    struct byte4
    {
        byte x = 0, y = 0, z = 0, w = 0;
        
        inline byte&       operator[](int index)       { assert(index >= 0 && index < 4); return (&x)[index]; }
        inline const byte& operator[](int index) const { assert(index >= 0 && index < 4); return (&x)[index]; }
        
        inline bool operator==(const byte4& other) const { return x == other.x && y == other.y && z == other.z && w == other.w; }
        inline bool operator!=(const byte4& other) const { return !(*this == other); }
    };




    struct short2
    {
        short x = 0, y = 0;
        
        inline short&       operator[](int index)       { assert(index >= 0 && index < 2); return (&x)[index]; }
        inline const short& operator[](int index) const { assert(index >= 0 && index < 2); return (&x)[index]; }
        
        inline bool operator==(const short2& other) const { return x == other.x && y == other.y; }
        inline bool operator!=(const short2& other) const { return !(*this == other); }
    };

    struct short3
    {
        short x = 0, y = 0, z = 0;
        
        inline short&       operator[](int index)       { assert(index >= 0 && index < 3); return (&x)[index]; }
        inline const short& operator[](int index) const { assert(index >= 0 && index < 3); return (&x)[index]; }
        
        inline bool operator==(const short3& other) const { return x == other.x && y == other.y && z == other.z; }
        inline bool operator!=(const short3& other) const { return !(*this == other); }
    };

    struct short4
    {
        short x = 0, y = 0, z = 0, w = 0;

        inline short&       operator[](int index)       { assert(index >= 0 && index < 4); return (&x)[index]; }
        inline const short& operator[](int index) const { assert(index >= 0 && index < 4); return (&x)[index]; }
        
        inline bool operator==(const short4& other) const { return x == other.x && y == other.y && z == other.z && w == other.w; }
        inline bool operator!=(const short4& other) const { return !(*this == other); }
    };



    using ushort = unsigned short;

    struct ushort2
    {
        ushort x = 0, y = 0;
        
        inline ushort&       operator[](int index)       { assert(index >= 0 && index < 2); return (&x)[index]; }
        inline const ushort& operator[](int index) const { assert(index >= 0 && index < 2); return (&x)[index]; }
        
        inline bool operator==(const ushort2& other) const { return x == other.x && y == other.y; }
        inline bool operator!=(const ushort2& other) const { return !(*this == other); }
    };

    struct ushort3
    {
        ushort x = 0, y = 0, z = 0;
        
        inline ushort&       operator[](int index)       { assert(index >= 0 && index < 3); return (&x)[index]; }
        inline const ushort& operator[](int index) const { assert(index >= 0 && index < 3); return (&x)[index]; }
        
        inline bool operator==(const ushort3& other) const { return x == other.x && y == other.y && z == other.z; }
        inline bool operator!=(const ushort3& other) const { return !(*this == other); }
    };

    struct ushort4
    {
        ushort x = 0, y = 0, z = 0, w = 0;
        
        inline ushort&       operator[](int index)       { assert(index >= 0 && index < 4); return (&x)[index]; }
        inline const ushort& operator[](int index) const { assert(index >= 0 && index < 4); return (&x)[index]; }
        
        inline bool operator==(const ushort4& other) const { return x == other.x && y == other.y && z == other.z && w == other.w; }
        inline bool operator!=(const ushort4& other) const { return !(*this == other); }
    };


    struct color4b
    {
        byte r = 0, g = 0, b = 0, a = 0;

        inline byte& operator[](int index) { assert(index >= 0 && index < 4); return (&r)[index]; }
        inline const byte& operator[](int index) const { assert(index >= 0 && index < 4); return (&r)[index]; }

        inline bool operator==(const color4b& other) const { return r == other.r && g == other.g && b == other.b && a == other.a; }
        inline bool operator!=(const color4b& other) const { return !(*this == other); }
    };

    struct color4f
    {
        float r = 0, g = 0, b = 0, a = 0;

        inline float& operator[](int index) { assert(index >= 0 && index < 4); return (&r)[index]; }
        inline const float& operator[](int index) const { assert(index >= 0 && index < 4); return (&r)[index]; }

        inline bool operator==(const color4f& other) const { return r == other.r && g == other.g && b == other.b && a == other.a; }
        inline bool operator!=(const color4f& other) const { return !(*this == other); }
    };


    struct IntRect
    {
        union
        {
            struct { int32_t x, y, w, h; };
            struct { int2 position, size; };
        };

        bool Contains(int2 p) const { return p.x >= x && p.y >= y && p.x < x + w && p.y < y + h; }
        
        inline int32_t Min(int axis) const { return position[axis]; }
        inline int32_t Max(int axis) const { return position[axis] + size[axis]; }

        inline int2 Min() const { return position; }
        inline int2 Max() const { return position + size; }

        inline int2 Center()      const { return { x + w / 2, y + h / 2 }; }
        inline int2 TopLeft()     const { return { x,         y         }; }
        inline int2 TopCenter()   const { return { x + w / 2, y         }; }
        inline int2 TopRight()    const { return { x + w,     y         }; }
        inline int2 MiddleLeft()  const { return { x,         y + h / 2 }; }
        inline int2 MiddleRight() const { return { x + w,     y + h / 2 }; }
        inline int2 BottomLeft()  const { return { x,         y + h     }; }
        inline int2 BottomCenter()const { return { x + w / 2, y + h     }; }
        inline int2 BottomRight() const { return { x + w,     y + h     }; }

        inline bool operator==(const IntRect& other) const { return x == other.x && y == other.y && w == other.w && h == other.h; }
        inline bool operator!=(const IntRect& other) const { return !(*this == other); }

        inline IntRect operator+ (const int2& other) const { return IntRect { .x = x + other.x, .y = y + other.y, .w = w, .h = h }; }
        inline IntRect operator- (const int2& other) const { return IntRect { .x = x - other.x, .y = y - other.y, .w = w, .h = h }; }

        inline IntRect& operator+=(const int2& other) { x += other.x; y += other.y; return *this; }
        inline IntRect& operator-=(const int2& other) { x -= other.x; y -= other.y; return *this; }

        static IntRect Inflate(IntRect a, int32_t t, int32_t r, int32_t b, int32_t l);
        inline IntRect Inflate(int32_t t, int32_t r, int32_t b, int32_t l) const { return Inflate(*this, t, r, b, l); }

        static IntRect Inflate(IntRect a, int32_t n);
        inline IntRect Inflate(int32_t n) const { return Inflate(*this, n); }

        static IntRect Deflate(IntRect a, int32_t t, int32_t r, int32_t b, int32_t l);
        inline IntRect Deflate(int32_t t, int32_t r, int32_t b, int32_t l) const { return Deflate(*this, t, r, b, l); }

        static IntRect Deflate(IntRect a, int32_t n);
        inline IntRect Deflate(int32_t n) const { return Deflate(*this, n); }

        static IntRect Intersection(IntRect a, IntRect b);
        inline IntRect Intersection(IntRect b) const { return Intersection(*this, b); }

        static IntRect Union(IntRect a, IntRect b);
        inline IntRect Union(IntRect b) const { return Union(*this, b); }

        inline bool IsEmpty() const { return *this == IntRect{ 0,0,0,0 }; }

        // Apply a 2D affine transform and return the axis-aligned bounding rect of the result
        IntRect Transform(const float3x2& m) const;
        IntRect Transform(int2 offset, const float3x2& m) const;
        IntRect Transform(IntRect parentRect, const float3x2& m) const;
    };

    uint16_t FloatToHalf(float value);
    float HalfToFloat(uint16_t value);

    // Monotonic timestamp in nanoseconds (steady_clock), as a plain int64_t so
    // widely-included headers avoid <chrono>'s parse cost (~0.5s per TU).
    int64_t SteadyClockNowNanos();

}// namespace interstice