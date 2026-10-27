#include <gtest/gtest.h>

#include <gui_core/text/skribidi/gui_text_system_skribidi.h>

namespace tachyon
{
    TEST(SkribidiTextSystemCoordinateTest, ScreenToTextEditorLocalPoint)
    {
        const IntRect innerRect{ 25, 12, 160, 20 };

        tachyon::float2 localPos = { 0,0 };

        SkribidiTextSystem::ScreenToTextEditorLocalPoint(tachyon::int2{ 35, 22 }, innerRect, localPos);

        EXPECT_FLOAT_EQ(localPos.x, 10.0f);
        EXPECT_FLOAT_EQ(localPos.y, 10.0f);
    }
}