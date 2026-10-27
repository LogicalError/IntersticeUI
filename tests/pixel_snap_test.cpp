// Pixel snapping of nested boxes (gui_pixel_snap.h): at a fractional scale a box
// nested against its parent's border must meet it, and keep the same pixel
// insets, wherever the pair lands on the pixel grid.

#include <gtest/gtest.h>

#include <gui_core/graphics/gui_pixel_snap.h>

namespace tachyon
{
    namespace
    {
        float4 Scaled(float scale, float pan, float x0, float y0, float x1, float y1)
        {
            return float4{ (x0 + pan) * scale, (y0 + pan) * scale, (x1 + pan) * scale, (y1 + pan) * scale };
        }
    }

    // A 1-unit inset at 1.5x is 2 pixels at every pan offset, on every side.
    TEST(PixelSnapTest, NestedInsetIsTheSameAtEveryPosition)
    {
        for (int step = 0; step < 12; ++step)
        {
            const float  pan    = float(step) / 12.0f;
            const float4 parent = Scaled(1.5f, pan, 20, 20, 80, 80);
            const float4 child  = Scaled(1.5f, pan, 21, 21, 79, 35);
            const IntRect p = SnapAbsolute(parent);
            const IntRect c = SnapToParent(parent, p, child);

            EXPECT_EQ(c.x - p.x, 2) << "pan " << pan;
            EXPECT_EQ(c.y - p.y, 2) << "pan " << pan;
            EXPECT_EQ((p.x + p.w) - (c.x + c.w), 2) << "pan " << pan;
        }
    }

    // At scale 1 with whole-unit positions nothing moves.
    TEST(PixelSnapTest, IntegerScaleIsExact)
    {
        const float4 parent{ 10, 20, 110, 70 };
        const IntRect p = SnapAbsolute(parent);
        EXPECT_EQ(p.x, 10); EXPECT_EQ(p.y, 20); EXPECT_EQ(p.w, 100); EXPECT_EQ(p.h, 50);
        const IntRect c = SnapToParent(parent, p, float4{ 11, 21, 109, 35 });
        EXPECT_EQ(c.x, 11); EXPECT_EQ(c.y, 21); EXPECT_EQ(c.w, 98); EXPECT_EQ(c.h, 14);
    }
}
