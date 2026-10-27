// Theme metrics from the Graphite spec that code relies on.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <algorithm>
#include <cmath>

namespace
{
    using namespace tachyon;
    using GUITest::HeadlessGUIApp;

    int IconSize(HeadlessGUIApp& app, StyleRef style, ElementState state)
    {
        const VisualTextStyle* text = app.styleManager.GetVisualTextStyle(style.visualTextStyleIndex, state);
        return text ? int(text->iconSize) : -1;
    }

    // WCAG contrast ratio of two sRGB colors.
    float Contrast(color4f a, color4f b)
    {
        auto luminance = [](color4f c) {
            auto channel = [](float v) { return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
            return 0.2126f * channel(c.r) + 0.7152f * channel(c.g) + 0.0722f * channel(c.b);
        };
        const float la = luminance(a), lb = luminance(b);
        return (std::max(la, lb) + 0.05f) / (std::min(la, lb) + 0.05f);
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

    // Every divider highlights alike when hovered: panel splitters and column sizers.
    TEST(ThemeMetrics, DividersShareOneHoverColor)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        auto hoverBackground = [&](StyleRef style) {
            const VisualBoxStyle* box = app.styleManager.GetVisualBoxStyle(style.visualBoxStyleIndex, ElementState::Hover);
            return box ? box->backgroundColorIndex : VisualColorStyleIndex{};
        };
        const auto dock = hoverBackground(app.theme.dockSizerHorzStyleRef);
        EXPECT_EQ(hoverBackground(app.theme.dockSizerVertStyleRef), dock);
        EXPECT_EQ(hoverBackground(app.theme.tableSizerStyleRef), dock);
        const color4f divider = app.theme.GetColor("--divider-hover");
        EXPECT_GT(divider.a, 0.0f) << "the palette defines --divider-hover";
    }

    // Captions and warnings stay readable (4.5:1) on every surface of the spec.
    TEST(ThemeMetrics, SecondaryTextHasContrastOnEverySurface)
    {
        const char* surfaces[] = { "--bg-app", "--bg-input", "--bg-canvas", "--bg-panel", "--bg-header", "--bg-raised",
                                   "--bg-control", "--bg-active", "--bg-pressed", "--row-stripe" };
        for (const char* theme : { "themes/dark.css", "themes/light.css" })
        {
            HeadlessGUIApp app(theme);
            ASSERT_TRUE(app.Initialize());
            for (const char* surface : surfaces)
            {
                const color4f bg = app.theme.GetColor(surface, { -1, -1, -1, -1 });
                ASSERT_GE(bg.r, 0.0f) << theme << " defines " << surface;
                EXPECT_GE(Contrast(app.theme.GetColor("--text-3"), bg), 4.5f) << theme << " text-3 on " << surface;
                EXPECT_GE(Contrast(app.theme.GetColor("--warn-text"), bg), 4.5f) << theme << " warn-text on " << surface;
            }
        }
    }

    // The active tab's icon is accent-colored; elsewhere icons take the text color.
    TEST(ThemeMetrics, ActiveTabIconIsAccent)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        const VisualTextStyle* active = app.styleManager.GetVisualTextStyle(app.theme.tabContentStyleRef.visualTextStyleIndex, ElementState::Selected);
        const VisualTextStyle* idle   = app.styleManager.GetVisualTextStyle(app.theme.tabContentStyleRef.visualTextStyleIndex, ElementState::None);
        ASSERT_NE(active, nullptr);
        ASSERT_NE(idle, nullptr);
        EXPECT_NE(active->iconColorIndex, active->textColorIndex);
        EXPECT_EQ(idle->iconColorIndex, idle->textColorIndex);
    }

    // The theme gives the caret its color (--caret).
    TEST(ThemeMetrics, TheCaretTakesTheThemesColor)
    {
        HeadlessGUIApp app;
        ASSERT_TRUE(app.Initialize());
        EXPECT_NE(app.ownedTextSystem.caretColor, VisualColorStyleIndex::Invalid);
    }
}
