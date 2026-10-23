// The UI scale styles are registered at: the main window's display scale
// (the primary monitor's before any window exists) times the app's zoom.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <filesystem>
#include <fstream>

using ::testing::Return;

namespace interstice
{
    using GUITest::HeadlessGUIApp;

    TEST(UIScaleTest, StylesFollowTheDisplayScaleTimesTheZoom)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        EXPECT_FLOAT_EQ(app.styleManager.GetUIScale(), 1.0f);

        MonitorInfo monitor{};
        monitor.workArea     = IntRect{ 0, 0, 1920, 1080 };
        monitor.isPrimary    = true;
        monitor.contentScale = 1.5f;
        ON_CALL(app.mockBackend, GetMonitors()).WillByDefault(Return(std::vector<MonitorInfo>{ monitor }));
        app.Frame([] {});
        app.Frame([] {});
        EXPECT_FLOAT_EQ(app.styleManager.GetUIScale(), 1.5f) << "moved to a 150% monitor";

        app.SetUIScale(2.0f);
        app.Frame([] {});
        EXPECT_FLOAT_EQ(app.styleManager.GetUIScale(), 3.0f) << "zoom on top of the display scale";
    }

    // Metrics the theme reads back from styles are at the UI scale too.
    TEST(UIScaleTest, ThemeMetricsAreScaled)
    {
        HeadlessGUIApp normal;
        ASSERT_TRUE(normal.Initialize());
        HeadlessGUIApp zoomed;
        zoomed.SetUIScale(2.0f);
        ASSERT_TRUE(zoomed.Initialize());
        EXPECT_EQ(zoomed.theme.socketSize, 2 * normal.theme.socketSize);
        EXPECT_EQ(normal.theme.scrollbarThickness, 12);
        EXPECT_EQ(zoomed.theme.scrollbarThickness, 24);
    }

    // The demo log's cells centre their text in rows the level icon makes taller.
    TEST(ThemeTest, LogCellsCentreTheirText)
    {
        HeadlessGUIApp app;
        const std::string_view files[] = { "styles.css", "demo.css" };
        app.SetStyleFiles(files);
        ASSERT_TRUE(app.Initialize());
        const VisualTextStyle* text = app.styleManager.GetVisualTextStyle(
            app.theme.GetStyle("log-cell").visualTextStyleIndex, ElementState::None);
        ASSERT_NE(text, nullptr);
        EXPECT_EQ(text->textLayoutStyle.verticalAlignment, TextAlign::Center);
    }

    // A label group's width is CSS pixels: it follows a live scale change, and an
    // edge drag of N pixels changes it by N / scale.
    TEST(UIScaleTest, LabelGroupWidthIsInCssPixels)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        const ID labelID = app.idManager.CreateStaticID();
        auto frame = [&] {
            app.Frame([&] {
                app.BeginRootWindow({ 800, 600 });
                if (app.BeginArea(app.theme.dockContentStyleRef, LayoutOverride{}.FlexGrow(1)))
                {
                    app.BeginLabelGroup(120, 40, 300);
                    app.DragLabel(labelID, "X");
                    app.EndLabelGroup();
                    app.EndArea();
                }
                app.EndRootWindow();
            });
        };
        frame();
        EXPECT_EQ(app.GetElementRect(labelID).w, 120);
        app.SetUIScale(2.0f);
        frame();
        frame();
        const IntRect r0 = app.GetElementRect(labelID);
        EXPECT_EQ(r0.w, 240) << "the stored width scales with the UI";

        const int2 from{ r0.x + r0.w - 2, r0.y + r0.h / 2 };
        const int2 to  { from.x + 40, from.y };
        app.InjectMouseMove(from);
        frame();
        app.InjectMouseDown(from);
        frame();
        app.InjectMouseMove(to, MouseButtons::Left);
        frame();
        app.InjectMouseUp(to);
        frame();
        frame();
        EXPECT_EQ(app.GetElementRect(labelID).w, 280) << "a 40 px drag is 20 CSS px, laid out at 2x";
    }

    // An app adds its own CSS classes with an extra style file and finds any class,
    // its own or the library's, by name.
    TEST(ThemeTest, StyleFilesAddClassesFoundByName)
    {
        const auto extra = std::filesystem::temp_directory_path() / "interstice_theme_test.css";
        {
            std::ofstream out(extra);
            out << ".app-panel { padding: 7px; }" << std::endl;
        }
        HeadlessGUIApp app;
        const std::string extraPath = extra.string();
        const std::string_view files[] = { "styles.css", extraPath };
        app.SetStyleFiles(files);
        ASSERT_TRUE(app.Initialize());

        const StyleRef own = app.theme.GetStyle("app-panel");
        ASSERT_NE(own.layoutStyleIndex, LayoutStyleIndex::Invalid);
        EXPECT_EQ(app.styleManager.GetLayoutStyle(own.layoutStyleIndex)->padding.top, 7);
        EXPECT_NE(app.theme.GetStyle("button").layoutStyleIndex, LayoutStyleIndex::Invalid);
        EXPECT_EQ(app.theme.GetStyle("no-such-class").layoutStyleIndex, LayoutStyleIndex::Invalid);
        std::filesystem::remove(extra);
    }
}
