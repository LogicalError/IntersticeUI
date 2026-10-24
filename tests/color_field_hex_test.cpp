// ColorField: a swatch, the color as hex and its alpha in percent; typing a hex color sets it.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

namespace
{
    using namespace interstice;
    using GUITest::HeadlessGUIApp;

    class ColorFieldHex : public ::testing::Test
    {
    protected:
        void SetUp() override { ASSERT_TRUE(app.Initialize()); }

        void Emit()
        {
            app.Frame([&] {
                app.BeginRootWindow({ 800, 600 });
                app.ColorField(color, false, LayoutOverride{}.Width(300));
                app.EndRootWindow();
            });
        }

        // The first element drawn in `style`.
        ID FindByStyle(StyleRef style)
        {
            const auto& storage = app.elementStorage;
            for (auto it = storage.TopDownDepthScanBegin(); it != storage.TopDownDepthScanEnd(); ++it)
                for (const Element& e : it->GetChildren(storage))
                    if (e.styleRef.layoutStyleIndex == style.layoutStyleIndex
                        && e.styleRef.visualBoxStyleIndex == style.visualBoxStyleIndex)
                        return e.id;
            return ID::Invalid();
        }

        HeadlessGUIApp app;
        color4f        color{ 0.0f, 0.0f, 1.0f, 0.5f };
    };

    TEST_F(ColorFieldHex, HasASwatchAHexFieldAndAlpha)
    {
        Emit();
        EXPECT_NE(FindByStyle(app.theme.colorSwatchStyleRef), ID::Invalid());
        EXPECT_NE(FindByStyle(app.theme.colorHexStyleRef), ID::Invalid());
        EXPECT_NE(FindByStyle(app.theme.colorAlphaInputStyleRef), ID::Invalid());
        EXPECT_EQ(app.GetElementRect(FindByStyle(app.theme.colorSwatchStyleRef)).w, app.styleManager.Px(56));
    }

    TEST_F(ColorFieldHex, TypingAHexColorSetsIt)
    {
        Emit();
        const ID hexID = FindByStyle(app.theme.colorHexStyleRef);
        ASSERT_NE(hexID, ID::Invalid());
        app.ClickAt(app.CenterOf(hexID));
        Emit();
        app.InjectKey(KeyCode::End);
        for (int i = 0; i < 6; ++i)
            app.InjectKey(KeyCode::Backspace);
        Emit();
        app.InjectText("FF0000");
        Emit();
        app.InjectKey(KeyCode::Return);
        Emit();
        Emit();
        EXPECT_FLOAT_EQ(color.r, 1.0f);
        EXPECT_FLOAT_EQ(color.g, 0.0f);
        EXPECT_FLOAT_EQ(color.b, 0.0f);
        EXPECT_FLOAT_EQ(color.a, 0.5f) << "the alpha is left alone";
    }

    // The swatch opens the picker dialog; a click on its square changes the color.
    class ColorPickerDialog : public ColorFieldHex
    {
    protected:
        void Open()
        {
            Emit();
            app.ClickAt(app.CenterOf(FindByStyle(app.theme.colorSwatchStyleRef)));
            Emit();
            Emit();
        }
        void Click(StyleRef style)
        {
            const ID id = FindByStyle(style);
            ASSERT_NE(id, ID::Invalid());
            app.ClickAt(app.CenterOf(id));
            Emit();
            Emit();
        }
    };

    TEST_F(ColorPickerDialog, TheSwatchOpensIt)
    {
        Open();
        const ID square = FindByStyle(app.theme.colorPickerSquareStyleRef);
        ASSERT_NE(square, ID::Invalid()) << "the dialog is open";
        EXPECT_EQ(app.GetElementRect(square).w, app.styleManager.Px(240));
        EXPECT_NE(FindByStyle(app.theme.dialogPrimaryButtonStyleRef), ID::Invalid()) << "with an OK button";
    }

    TEST_F(ColorPickerDialog, CancelRestoresTheColor)
    {
        const color4f original = color;
        Open();
        Click(app.theme.colorPickerSquareStyleRef);
        EXPECT_NE(color.r, original.r) << "the square changed the color";
        Click(app.theme.dialogButtonStyleRef);
        EXPECT_EQ(color.r, original.r);
        EXPECT_EQ(color.b, original.b);
        EXPECT_EQ(FindByStyle(app.theme.colorPickerSquareStyleRef), ID::Invalid()) << "and closed the dialog";
    }

    TEST_F(ColorPickerDialog, OkKeepsTheColor)
    {
        const color4f original = color;
        Open();
        Click(app.theme.colorPickerSquareStyleRef);
        const color4f picked = color;
        EXPECT_NE(picked.r, original.r);
        Click(app.theme.dialogPrimaryButtonStyleRef);
        EXPECT_EQ(color.r, picked.r);
        EXPECT_EQ(FindByStyle(app.theme.colorPickerSquareStyleRef), ID::Invalid()) << "and closed the dialog";
    }
}
