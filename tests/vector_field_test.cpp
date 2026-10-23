#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <vector>

namespace
{
    using namespace interstice;
    using GUITest::HeadlessGUIApp;

    // The styles of an element's children, in order.
    std::vector<StyleRef> ChildStyles(HeadlessGUIApp& app, ID parent)
    {
        std::vector<StyleRef> styles;
        const ElementRef ref = app.elementStorage.FindElementRefByID(parent);
        if (!ref.IsValid())
            return styles;
        const auto range = app.elementStorage.GetChildRange(ref);
        for (uint32_t i = range.begin; i < range.end; ++i)
            if (const auto* element = app.elementStorage.GetElementPtr(ElementRef{ ref.depth + 1, i }))
                styles.push_back(element->styleRef);
        return styles;
    }

    // Each component gets its axis label, attached to the component's field.
    TEST(VectorField, ComponentsHaveTheirAxisLabels)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        float3 value{ 1.0f, 2.0f, 3.0f };
        const ID fieldID = app.idManager.CreateStaticID();
        app.Frame([&] {
            app.BeginRootWindow();
            app.Field(fieldID, value);
            app.EndRootWindow();
        });

        const auto styles = ChildStyles(app, fieldID);
        ASSERT_EQ(styles.size(), 6u);
        for (int i = 0; i < 3; ++i)
        {
            EXPECT_EQ(styles[size_t(2 * i)],     app.theme.axisLabelStyleRefs[i]) << i;
            EXPECT_EQ(styles[size_t(2 * i + 1)], app.theme.vectorComponentStyleRef) << i;
        }
    }

    // A caller's style replaces the attached component field.
    TEST(VectorField, ACallersStyleIsKept)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        int2 value{ 1, 2 };
        const ID fieldID = app.idManager.CreateStaticID();
        const StyleRef custom = app.theme.controls->toggle;
        app.Frame([&] {
            app.BeginRootWindow();
            app.Field(fieldID, value, TextEditBehavior::ContinuousUpdate, custom, {});
            app.EndRootWindow();
        });

        const auto styles = ChildStyles(app, fieldID);
        ASSERT_EQ(styles.size(), 4u);
        EXPECT_EQ(styles[1], custom);
        EXPECT_EQ(styles[3], custom);
    }
}
