// The gradient editor dialog: opened by a GradientField's bar; a click on the
// gradient adds a stop; Cancel restores the gradient, OK keeps it.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_controls/gui_application_internal.h>
#include <gui_controls/subsystems/gui_gradient_editor_subsystem.h>

namespace
{
    using namespace tachyon;
    using GUITest::HeadlessGUIApp;

    class GradientEditorDialog : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ASSERT_TRUE(app.Initialize());
            gradient.type = GradientType::Linear;
            gradient.AddStop(color4f{ 1, 0, 0, 1 }, 0.0f);
            gradient.AddStop(color4f{ 0, 0, 1, 1 }, 1.0f);
        }

        void Emit()
        {
            app.Frame([&] {
                app.BeginRootWindow({ 1200, 900 });
                app.GradientField(gradient, LayoutOverride{}.Width(300));
                app.EndRootWindow();
            });
        }

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

        void Open()
        {
            Emit();
            app.ClickAt(app.CenterOf(FindByStyle(app.theme.gradientSwatchStyleRef)));
            Emit();
            Emit();
        }

        void ClickAt(int2 pos)
        {
            app.ClickAt(pos);
            Emit();
            Emit();
        }

        // A point on the dialog's gradient bar, `t` across it (the bar is inset 24 px at the sides).
        int2 OnTheBar(float t)
        {
            const IntRect r = app.GetElementRect(FindByStyle(app.theme.gradientEditorBarStyleRef));
            const int inset = app.styleManager.Px(24);
            const int barTop = r.y + (r.h - app.styleManager.Px(44 + 6 + 18)) / 2;
            return { r.x + inset + int(t * float(r.w - 2 * inset)), barTop + app.styleManager.Px(20) };
        }

        HeadlessGUIApp app;
        Gradient       gradient;
    };

    TEST_F(GradientEditorDialog, TheFieldOpensIt)
    {
        Open();
        EXPECT_NE(FindByStyle(app.theme.gradientEditorBarStyleRef), ID::Invalid());
        EXPECT_NE(FindByStyle(app.theme.dialogPrimaryButtonStyleRef), ID::Invalid()) << "with an OK button";
    }

    TEST_F(GradientEditorDialog, CancelRestoresTheGradient)
    {
        Open();
        ClickAt(OnTheBar(0.5f));
        EXPECT_EQ(gradient.stopCount, 3) << "a click on the bar added a stop";
        ClickAt(app.CenterOf(FindByStyle(app.theme.dialogButtonStyleRef)));
        EXPECT_EQ(gradient.stopCount, 2);
        EXPECT_EQ(FindByStyle(app.theme.gradientEditorBarStyleRef), ID::Invalid()) << "and closed the dialog";
    }

    TEST_F(GradientEditorDialog, OkKeepsTheGradient)
    {
        Open();
        ClickAt(OnTheBar(0.5f));
        ClickAt(app.CenterOf(FindByStyle(app.theme.dialogPrimaryButtonStyleRef)));
        EXPECT_EQ(gradient.stopCount, 3);
        EXPECT_EQ(FindByStyle(app.theme.gradientEditorBarStyleRef), ID::Invalid()) << "and closed the dialog";
    }

    TEST_F(GradientEditorDialog, TheHeaderDragsTheDialog)
    {
        Open();
        const ID window = GUIApplicationInternals::GradientEditor(app).State().windowID;
        const IntRect header = app.GetElementRect(FindByStyle(app.theme.dialogHeaderStyleRef));
        EXPECT_EQ(app.TitleBarHitTest(window, { header.x + header.w / 2, header.y + header.h / 2 }), WindowHitArea::Caption);
        EXPECT_EQ(app.TitleBarHitTest(window, app.CenterOf(FindByStyle(app.theme.dialogCloseStyleRef))), WindowHitArea::Client);
    }

    // As for the color picker: after a theme reload it creates its styles again.
    TEST_F(GradientEditorDialog, AThemeReloadRecreatesItsStyles)
    {
        Open();
        auto& ge = GUIApplicationInternals::GradientEditor(app).State();
        ge.stopHaloColor = app.theme.accentColor;
        GUIApplicationInternals::ReloadThemeNextFrame(app);
        Emit();
        ASSERT_NE(ge.stopHaloColor, VisualColorStyleIndex::Invalid);
        EXPECT_NE(ge.stopHaloColor, app.theme.accentColor);
    }
}
