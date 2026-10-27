// Measured sizes (#25): the vector component labels take the widest name's measured width,
// and a cursor ghost sits clear of the OS cursor at its real size.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_core/style/gui_style_manager.h>

namespace
{
    using namespace tachyon;
    using GUITest::HeadlessGUIApp;
    using ::testing::Return;

    int32_t LabelChrome(HeadlessGUIApp& app)
    {
        int32_t chrome = 0;
        for (const StyleRef style : app.theme.axisLabelStyleRefs)
            if (const LayoutStyle* ls = app.styleManager.GetLayoutStyle(style.layoutStyleIndex))
                chrome = std::max(chrome, ls->padding.Horizontal() + ls->borderWidths.Horizontal());
        return chrome;
    }

    // The width follows the font: a wider glyph gives a wider label.
    TEST(MeasuredSizesTest, VectorComponentLabelsTakeTheWidestNamesWidth)
    {
        for (const int32_t glyph : { 8, 20 })
        {
            HeadlessGUIApp app;
            app.ownedTextSystem.SetDefaultCharWidth(glyph);
            ASSERT_TRUE(app.Initialize());
            EXPECT_EQ(app.theme.axisLabelWidth, glyph + LabelChrome(app)) << "glyph width " << glyph;
            app.Cleanup();
        }
    }

    // A vector field that wraps keeps each component's label on its field's row: they wrap
    // as one unit.
    TEST(MeasuredSizesTest, AWrappingVectorFieldKeepsEachLabelWithItsField)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        const ID columnID = app.idManager.CreateStaticID();
        const ID fieldID  = app.idManager.CreateStaticID();
        int4 value{ 1, 2, 3, 4 };
        for (int width = 60; width <= 260; width += 2)   // every place a row can end
        {
            for (int i = 0; i < 2; i++)
                app.Frame([&] {
                    app.BeginRootWindow();
                    app.BeginHorizontal(columnID, app.theme.emptyHorzStyleRef, LayoutOverride{}.Width(width).NoGrow().NoShrink());
                    app.Field(fieldID, value);
                    app.EndHorizontal();
                    app.EndRootWindow();
                });
            const ElementRef group = app.elementStorage.FindElementRefByID(fieldID);
            ASSERT_TRUE(group.IsValid());
            const auto range = app.elementStorage.GetChildRange(group);
            ASSERT_EQ(range.end - range.begin, 8u) << "a label and a field per component";
            for (uint32_t c = 0; c < 4; ++c)
            {
                const IntRect label = app.elementStorage.GetElementRectPtr(ElementRef{ group.depth + 1, range.begin + 2 * c })->rect;
                const IntRect field = app.elementStorage.GetElementRectPtr(ElementRef{ group.depth + 1, range.begin + 2 * c + 1 })->rect;
                EXPECT_EQ(label.y, field.y) << "component " << c << " at width " << width;
                EXPECT_LE(label.x + label.w, field.x + 1) << "component " << c << " at width " << width;
            }
        }
        app.Cleanup();
    }

    TEST(MeasuredSizesTest, ACursorGhostClearsTheOsCursor)
    {
        HeadlessGUIApp large;
        ON_CALL(large.mockBackend, GetCursorSize()).WillByDefault(Return(64));
        ASSERT_TRUE(large.Initialize());
        EXPECT_EQ(large.CursorGhostOffset(), (int2{ 28, 28 })) << "14 px for a 32 px cursor, doubled";
        large.Cleanup();

        HeadlessGUIApp unknown;   // the backend can't tell
        ASSERT_TRUE(unknown.Initialize());
        const int32_t fallback = unknown.styleManager.Px(14);
        EXPECT_EQ(unknown.CursorGhostOffset(), (int2{ fallback, fallback }));
        unknown.Cleanup();
    }
}
