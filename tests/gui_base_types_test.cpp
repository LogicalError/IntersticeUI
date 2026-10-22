#include <gtest/gtest.h>
#include <gui_core/gui_base_types.h>

#include <cmath>

namespace GUI
{

    TEST(IntRectTest, InflateDeflateBeyondEvenSizeClampsAtCenter)
    {
        IntRect rect {};
        rect.x = 10;
        rect.y = 20;
        rect.w = 8;
        rect.h = 6;

        auto inflated = IntRect::Inflate(rect, -100);

        EXPECT_EQ(inflated.x, 14);
        EXPECT_EQ(inflated.y, 23);
        EXPECT_EQ(inflated.w, 0);
        EXPECT_EQ(inflated.h, 0);
    }

    TEST(IntRectTest, InflateDeflateBeyondOddSizeClampsAtCenter)
    {
        IntRect rect {};
        rect.x = 5;
        rect.y = 7;
        rect.w = 9;
        rect.h = 7;

        auto inflated = IntRect::Inflate(rect, -100);

        EXPECT_EQ(inflated.x, 9);
        EXPECT_EQ(inflated.y, 10);
        EXPECT_EQ(inflated.w, 0);
        EXPECT_EQ(inflated.h, 0);
    }

    // float3x2 tests

    TEST(float3x2Test, DefaultIsIdentity)
    {
        float3x2 m;
        EXPECT_TRUE(m.IsIdentity());
        EXPECT_EQ(m, float3x2::Identity());
    }

    TEST(float3x2Test, IsIdentityReturnsFalseForNonIdentity)
    {
        float3x2 m;
        m.r0.x = 2.0f;
        EXPECT_FALSE(m.IsIdentity());
    }

    TEST(float3x2Test, TransformPointIdentityIsNoOp)
    {
        float3x2 m = float3x2::Identity();
        float2 p = { 3.0f, 7.0f };
        float2 result = m.TransformPoint(p);
        EXPECT_FLOAT_EQ(result.x, 3.0f);
        EXPECT_FLOAT_EQ(result.y, 7.0f);
    }

    TEST(float3x2Test, TranslateMovesPoint)
    {
        float3x2 m = float3x2::Translate(10.0f, -5.0f);
        float2 result = m.TransformPoint({ 1.0f, 2.0f });
        EXPECT_FLOAT_EQ(result.x, 11.0f);
        EXPECT_FLOAT_EQ(result.y, -3.0f);
    }

    TEST(float3x2Test, ScaleScalesPoint)
    {
        float3x2 m = float3x2::Scale(2.0f, 3.0f);
        float2 result = m.TransformPoint({ 4.0f, 5.0f });
        EXPECT_FLOAT_EQ(result.x, 8.0f);
        EXPECT_FLOAT_EQ(result.y, 15.0f);
    }

    TEST(float3x2Test, MultiplyIdentityIsNoOp)
    {
        float3x2 m = float3x2::Translate(5.0f, 3.0f);
        EXPECT_EQ(m * float3x2::Identity(), m);
        EXPECT_EQ(float3x2::Identity() * m, m);
    }

    TEST(float3x2Test, MultiplyComposesTransforms)
    {
        // Translate then scale: scale(translate(p)) = scale*x + tx*scale
        float3x2 translate = float3x2::Translate(2.0f, 3.0f);
        float3x2 scale     = float3x2::Scale(2.0f, 2.0f);
        float3x2 composed  = scale * translate; // scale applied after translate

        float2 result = composed.TransformPoint({ 1.0f, 1.0f });
        // translate: (3, 4), then scale: (6, 8)
        EXPECT_FLOAT_EQ(result.x, 6.0f);
        EXPECT_FLOAT_EQ(result.y, 8.0f);
    }

    TEST(float3x2Test, InverseOfTranslateIsNegativeTranslate)
    {
        float3x2 m   = float3x2::Translate(10.0f, -20.0f);
        float3x2 inv = m.Inverse();
        float2 p     = { 5.0f, 5.0f };
        float2 r     = inv.TransformPoint(m.TransformPoint(p));
        EXPECT_NEAR(r.x, p.x, 1e-5f);
        EXPECT_NEAR(r.y, p.y, 1e-5f);
    }

    TEST(float3x2Test, InverseOfScaleIsReciprocalScale)
    {
        float3x2 m   = float3x2::Scale(4.0f, 0.5f);
        float3x2 inv = m.Inverse();
        float2 p     = { 8.0f, 6.0f };
        float2 r     = inv.TransformPoint(m.TransformPoint(p));
        EXPECT_NEAR(r.x, p.x, 1e-5f);
        EXPECT_NEAR(r.y, p.y, 1e-5f);
    }

    TEST(float3x2Test, InverseOfComposedTransformRoundTrips)
    {
        float3x2 m   = float3x2::Scale(2.0f, 3.0f) * float3x2::Translate(7.0f, -4.0f);
        float3x2 inv = m.Inverse();
        float2 p     = { 1.0f, 2.0f };
        float2 r     = inv.TransformPoint(m.TransformPoint(p));
        EXPECT_NEAR(r.x, p.x, 1e-4f);
        EXPECT_NEAR(r.y, p.y, 1e-4f);
    }

    TEST(float3x2Test, ZoomAtCursorKeepsCursorFixed)
    {
        // Simulate zoom-at-cursor: ZoomAt(P, s) pre-multiplied onto identity
        float2 cursor = { 100.0f, 80.0f };
        float s = 1.5f;
        float3x2 zoomDelta;
        zoomDelta.r0 = { s, 0.0f, cursor.x * (1.0f - s) };
        zoomDelta.r1 = { 0.0f, s,  cursor.y * (1.0f - s) };

        float2 result = zoomDelta.TransformPoint(cursor);
        EXPECT_NEAR(result.x, cursor.x, 1e-4f);
        EXPECT_NEAR(result.y, cursor.y, 1e-4f);
    }

    TEST(float3x2Test, ZoomAtCursorComposedTwiceKeepsCursorFixed)
    {
        // Two successive zooms at the same cursor should still keep cursor fixed
        float2 cursor = { 50.0f, 60.0f };
        auto makeZoom = [](float2 c, float s) {
            float3x2 z;
            z.r0 = { s, 0.0f, c.x * (1.0f - s) };
            z.r1 = { 0.0f, s,  c.y * (1.0f - s) };
            return z;
        };

        float3x2 t = makeZoom(cursor, 1.2f) * makeZoom(cursor, 1.1f);
        float2 result = t.TransformPoint(cursor);
        EXPECT_NEAR(result.x, cursor.x, 1e-4f);
        EXPECT_NEAR(result.y, cursor.y, 1e-4f);
    }

    TEST(float3x2Test, IntRectTransformWithScale)
    {
        IntRect rect { 10, 20, 30, 40 };
        float3x2 scale = float3x2::Scale(2.0f, 2.0f);
        IntRect result = rect.Transform(scale);
        EXPECT_EQ(result.x, 20);
        EXPECT_EQ(result.y, 40);
        EXPECT_EQ(result.w, 60);
        EXPECT_EQ(result.h, 80);
    }

    TEST(float3x2Test, IntRectTransformWithTranslate)
    {
        IntRect rect { 5, 10, 20, 15 };
        float3x2 translate = float3x2::Translate(3.0f, -7.0f);
        IntRect result = rect.Transform(translate);
        EXPECT_EQ(result.x, 8);
        EXPECT_EQ(result.y, 3);
        EXPECT_EQ(result.w, 20);
        EXPECT_EQ(result.h, 15);
    }

    // Inverse() of a singular (zero-scale) transform must not produce NaN/Inf,
    // which would break hit-testing for that subtree.
    TEST(float3x2Test, InverseOfSingularTransformIsFinite)
    {
        float3x2 singular = float3x2::Scale(0.0f, 0.0f);
        float3x2 inv      = singular.Inverse();
        float2   r        = inv.TransformPoint({ 5.0f, 5.0f });
        EXPECT_TRUE(std::isfinite(r.x)) << "Inverse() of a singular transform produced non-finite x";
        EXPECT_TRUE(std::isfinite(r.y)) << "Inverse() of a singular transform produced non-finite y";
    }

}