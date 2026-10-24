// Theme metrics from the Graphite spec that code relies on.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

namespace
{
    using namespace interstice;
    using GUITest::HeadlessGUIApp;

    int IconSize(HeadlessGUIApp& app, StyleRef style, ElementState state)
    {
        const VisualTextStyle* text = app.styleManager.GetVisualTextStyle(style.visualTextStyleIndex, state);
        return text ? int(text->iconSize) : -1;
    }

    // Icons in buttons are 14 px; without a size they'd draw at their natural
    // size, which is 24 for Lucide.
    TEST(ThemeMetrics, ButtonIconsAreFourteenPixels)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        for (const StyleRef style : { app.theme.controls->button, app.theme.controls->toggle,
                                      app.theme.toolbarControls.button, app.theme.toolbarControls.toggle,
                                      app.theme.nodeViewOverlayButtonStyleRef })
            for (const ElementState state : { ElementState::None, ElementState::Hover, ElementState::Selected })
                EXPECT_EQ(IconSize(app, style, state), app.styleManager.Px(14)) << int(state);
    }
}
