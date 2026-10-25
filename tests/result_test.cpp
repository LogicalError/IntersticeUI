// Result<T> (#47): calls that can fail say why.

#include <gtest/gtest.h>

#include "headless_gui_app.h"
#include "misuse_reports.h"

#include <gui_core/gui_result.h>

#include <string>

namespace
{
    using namespace interstice;
    using GUITest::HeadlessGUIApp;

    Result<int> Half(int v)
    {
        if (v % 2 != 0)
            return Error{ ErrorCode::InvalidArgument, "odd" };
        return v / 2;
    }

    TEST(ResultTest, HoldsTheValueOrTheError)
    {
        const Result<int> ok = Half(8);
        ASSERT_TRUE(ok);
        EXPECT_EQ(*ok, 4);
        EXPECT_EQ(ok.ValueOr(-1), 4);

        const Result<int> bad = Half(3);
        EXPECT_FALSE(bad);
        EXPECT_EQ(bad.GetError().code, ErrorCode::InvalidArgument);
        EXPECT_EQ(bad.GetError().message, "odd");
        EXPECT_EQ(bad.ValueOr(-1), -1);

        const Result<void> done;
        EXPECT_TRUE(done);
        const Result<void> failed = Error{ ErrorCode::NotFound, "gone" };
        EXPECT_FALSE(failed);
        EXPECT_EQ(failed.GetError().code, ErrorCode::NotFound);
    }

    // A theme that doesn't load: the reload says why, and the previous theme stays.
    TEST(ResultTest, AFailedThemeReloadSaysWhy)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        EXPECT_TRUE(app.ThemeReloadResult());
        const std::string before = app.GetThemePath();

        GUITest::MisuseReports reports;   // the failure is logged too
        app.RequestReloadTheme("themes/does-not-exist.css", 1.0f);
        app.Frame([&] { app.BeginRootWindow(); app.EndRootWindow(); });

        const Result<void>& reload = app.ThemeReloadResult();
        ASSERT_FALSE(reload);
        EXPECT_EQ(reload.GetError().code, ErrorCode::ParseFailed);
        EXPECT_NE(reload.GetError().message.find("does-not-exist.css"), std::string::npos) << reload.GetError().message;
        EXPECT_EQ(app.GetThemePath(), before) << "the previous theme stays";
        app.Cleanup();
    }

    // A CSS class no style file defines: FindStyle says so.
    TEST(ResultTest, AMissingCssClassIsAnError)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        EXPECT_TRUE(app.theme.FindStyle("group-label"));
        const Result<StyleRef> missing = app.theme.FindStyle("no-such-class");
        ASSERT_FALSE(missing);
        EXPECT_EQ(missing.GetError().code, ErrorCode::NotFound);
        EXPECT_NE(missing.GetError().message.find("no-such-class"), std::string::npos);
        app.Cleanup();
    }

    // A window that isn't there can't be read back, and the error says so.
    TEST(ResultTest, ReadingPixelsOfNoWindowSaysWhy)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        std::vector<uint8_t> pixels(16);
        const Result<void> read = app.ReadWindowPixels(app.idManager.CreateStaticID(), pixels);
        ASSERT_FALSE(read);
        EXPECT_EQ(read.GetError().code, ErrorCode::NotFound);
        app.Cleanup();
    }
}
