// ColorField: a swatch, the color as hex and its alpha in percent; typing a hex color sets it.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_controls/controls_color_internal.h>
#include <gui_controls/gui_application_internal.h>
#include <gui_controls/subsystems/gui_color_picker_subsystem.h>

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

    // The header is the dialog's title bar: it drags the window, its close button doesn't.
    TEST_F(ColorPickerDialog, TheHeaderDragsTheDialog)
    {
        Open();
        const ID window = GUIApplicationInternals::ColorPicker(app).State().windowID;
        const IntRect header = app.GetElementRect(FindByStyle(app.theme.dialogHeaderStyleRef));
        EXPECT_EQ(app.TitleBarHitTest(window, { header.x + header.w / 2, header.y + header.h / 2 }), WindowHitArea::Caption);
        EXPECT_EQ(app.TitleBarHitTest(window, app.CenterOf(FindByStyle(app.theme.dialogCloseStyleRef))), WindowHitArea::Client);
    }

    // A theme reload frees every style; the open picker must create its own again rather
    // than keep writing through indices the new theme now uses (here: the accent's).
    TEST_F(ColorPickerDialog, AThemeReloadRecreatesItsStyles)
    {
        Open();
        auto& cp = GUIApplicationInternals::ColorPicker(app).State();
        cp.hueStrip = app.theme.accentColor;
        GUIApplicationInternals::ReloadThemeNextFrame(app);
        Emit();
        ASSERT_NE(cp.hueStrip, VisualColorStyleIndex::Invalid);
        EXPECT_NE(cp.hueStrip, app.theme.accentColor);
    }

    // The hue strip goes around the whole hue circle, red back to red, within the
    // gradient's stop limit (more stops used to be dropped, cutting it off at cyan).
    TEST(ColorPickerHueStrip, GoesAllTheWayAround)
    {
        const Gradient g = HueStripGradient(ColorSpace::HSV);
        ASSERT_GE(g.stopCount, 2);
        const color4f first = g.stops[0].GetColor4f();
        const color4f last  = g.stops[size_t(g.stopCount - 1)].GetColor4f();
        EXPECT_FLOAT_EQ(g.stops[size_t(g.stopCount - 1)].GetOffset(), 1.0f);
        EXPECT_EQ(first.r, 1.0f); EXPECT_EQ(first.g, 0.0f); EXPECT_EQ(first.b, 0.0f);
        EXPECT_EQ(last.r,  1.0f); EXPECT_EQ(last.g,  0.0f); EXPECT_EQ(last.b,  0.0f);
    }

    // A dialog's header names the edited field after its title, unless it only repeats it.
    TEST(DialogHeader, ShowsTheFieldUnlessItRepeatsTheTitle)
    {
        EXPECT_EQ(DialogContext("Base color", "Color"), "Base color");
        EXPECT_EQ(DialogContext("Color", "Color"), "");
        EXPECT_EQ(DialogContext("color", "Color"), "") << "regardless of case";
        EXPECT_EQ(DialogContext("", "Color"), "");
    }

    // The eyedropper: pressed and released, the color is the screen's under the cursor, opaque.
    TEST_F(ColorPickerDialog, TheEyedropperPicksTheScreensColor)
    {
        using ::testing::_;
        using ::testing::Return;
        ON_CALL(app.mockBackend, ReadScreenPixel(_)).WillByDefault(Return(std::optional<color4b>(color4b{ 255, 0, 0, 255 })));
        Open();
        const ID eyedropper = FindByStyle(app.theme.dialogSquareButtonStyleRef);
        ASSERT_NE(eyedropper, ID::Invalid()) << "shown where the screen can be read";
        Click(app.theme.dialogSquareButtonStyleRef);
        EXPECT_FLOAT_EQ(color.r, 1.0f);
        EXPECT_FLOAT_EQ(color.g, 0.0f);
        EXPECT_FLOAT_EQ(color.b, 0.0f);
        EXPECT_FLOAT_EQ(color.a, 1.0f) << "the field was half transparent; the screen isn't";
    }

    TEST_F(ColorPickerDialog, NoEyedropperWhereTheScreenCantBeRead)
    {
        Open();
        EXPECT_EQ(FindByStyle(app.theme.dialogSquareButtonStyleRef), ID::Invalid());
    }
}
