// Icon names: the library's own icons don't take an app's names. The demo's colored "info"
// and "error" badges came back as the theme's grey outlines, which held those names first.

#include "skribidi_test_fixture.h"

#include <gui_controls/gui_theme.h>

#include <cstring>

namespace
{
    using GUITest::SkribidiHeadlessTest;

    const char* kBadge = R"(<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><circle cx="12" cy="12" r="10" fill="#E5484D"/></svg>)";

    TEST_F(SkribidiHeadlessTest, AnAppIconIsNotTheThemesIconOfTheSameName)
    {
        interstice::GUITheme theme;
        theme.InitializeDefaultStyles(styleManager, textSystem.get());
        ASSERT_NE(theme.infoIcon, interstice::IconID::Invalid);

        const interstice::IconID info  = textSystem->RegisterIconFromSVG("info",  kBadge, std::strlen(kBadge));
        const interstice::IconID error = textSystem->RegisterIconFromSVG("error", kBadge, std::strlen(kBadge));
        EXPECT_NE(info, interstice::IconID::Invalid);
        EXPECT_NE(info, theme.infoIcon);
        EXPECT_NE(error, theme.errorIcon);
    }

    // The same icon registered again (a theme reload) is the same icon.
    TEST_F(SkribidiHeadlessTest, AnIconRegisteredAgainIsTheSameIcon)
    {
        const interstice::IconID first  = textSystem->RegisterIconFromSVG("badge", kBadge, std::strlen(kBadge));
        const interstice::IconID second = textSystem->RegisterIconFromSVG("badge", kBadge, std::strlen(kBadge));
        EXPECT_EQ(first, second);
    }
}
