#include "gtest/gtest.h"

#include <gui_core/elements/gui_element_storage.h>

#include <gui_core/style/gui_style_manager.h>
#include <gui_core/style/gui_gradient_manager.h>
#include <gui_core/style/gui_style.h>
#include <gui_controls/gui_transition_manager.h>

namespace interstice
{

    static Gradient MakeTwoStopGradient()
    {
        Gradient g;
        g.type = GradientType::Linear;
        g.AddStop(color4f{ 1.0f, 1.0f, 1.0f, 1.0f }, 0.0f);
        g.AddStop(color4f{ 0.0f, 0.0f, 0.0f, 1.0f }, 1.0f);
        return g;
    }

    // TextLayoutStyle ==/hash must include textOverflow and wrapMode, or visually
    // different text styles dedup into one entry.
    TEST(TextLayoutStyleTest, EqualityAndHashAccountForTextOverflow)
    {
        TextLayoutStyle a{};
        TextLayoutStyle b{};
        a.textOverflow = TextOverflow::Clip;
        b.textOverflow = TextOverflow::Ellipsis;

        EXPECT_FALSE(a == b) << "operator== ignored textOverflow";
        EXPECT_NE(a.GetHashCode(), b.GetHashCode()) << "GetHashCode ignored textOverflow";
    }

    TEST(StyleManagerTest, Register_DeduplicatesLayoutAndVisual)
    {
        GradientManager gm(16);
        StyleManager sm(gm);

        Style s1{};
        s1.margin.top = 2;
        s1.padding.left = 3;
        s1.states[Style::kNormal].backgroundColor.gradient = MakeTwoStopGradient();

        // Register first time
        auto idx1 = sm.Register(s1);
        EXPECT_GE(int(idx1.layoutStyleIndex), 0);
        EXPECT_GE(int(idx1.visualBoxStyleIndex), 0);
        EXPECT_GE(int(idx1.visualTextStyleIndex), 0);
        size_t layoutCountAfterFirst = sm.LayoutStyleCount();
        size_t visualBoxCountAfterFirst = sm.VisualBoxStyleCount();
        size_t visualTextCountAfterFirst = sm.VisualTextStyleCount();

        // Register identical style -> should dedupe (same indices)
        auto idx2 = sm.Register(s1);
        EXPECT_EQ(idx1.layoutStyleIndex, idx2.layoutStyleIndex);
        EXPECT_EQ(idx1.visualBoxStyleIndex, idx2.visualBoxStyleIndex);
        EXPECT_EQ(idx1.visualTextStyleIndex, idx2.visualTextStyleIndex);
        EXPECT_EQ(sm.LayoutStyleCount(), layoutCountAfterFirst);
        EXPECT_EQ(sm.VisualBoxStyleCount(), visualBoxCountAfterFirst);
        EXPECT_EQ(sm.VisualTextStyleCount(), visualTextCountAfterFirst);

        // Change a layout-only field -> new layout index, same visual index
        Style s2 = s1;
        s2.margin.left = 5; // layout change only
        auto idx3 = sm.Register(s2);
        EXPECT_NE(idx3.layoutStyleIndex, idx1.layoutStyleIndex);
        EXPECT_EQ(idx3.visualBoxStyleIndex, idx1.visualBoxStyleIndex);
        EXPECT_EQ(idx3.visualTextStyleIndex, idx1.visualTextStyleIndex);

        // Change visual (background) -> new visual index
        Style s3 = s1;
        s3.states[Style::kNormal].backgroundColor.gradient = MakeTwoStopGradient();
        // modify a stop to produce different visual (toggle color)
        auto& stop = s3.states[Style::kNormal].backgroundColor.gradient.stops[0];
        auto c = stop.GetColor4f();
        c.r = 0.1f;
        stop.SetColor(c);
        auto idx4 = sm.Register(s3);
        EXPECT_NE(idx4.visualBoxStyleIndex, idx1.visualBoxStyleIndex);
    }

    TEST(StyleManagerTest, Register_RegistersGradientsIntoGradientManager)
    {
        GradientManager gm(8);
        StyleManager sm(gm);

        Style s{};
        s.states[Style::kNormal].backgroundColor.gradient = MakeTwoStopGradient();
        s.states[Style::kNormal].borderColors[0].gradient = MakeTwoStopGradient();
        s.states[Style::kNormal].borderColors[1].gradient = MakeTwoStopGradient();
        s.states[Style::kNormal].borderColors[2].gradient = MakeTwoStopGradient();
        s.states[Style::kNormal].borderColors[3].gradient = MakeTwoStopGradient();
        s.states[Style::kNormal].textColor.gradient = MakeTwoStopGradient();

        auto idx = sm.Register(s);
        ASSERT_GE(int(idx.visualBoxStyleIndex), 0);

        const VisualBoxStyle* vs = sm.GetVisualBoxStyle(idx.visualBoxStyleIndex, ElementState::None);
        ASSERT_NE(vs, nullptr);


        const VisualColorStyle* backgroundColor = sm.GetVisualColorStyle(vs->backgroundColorIndex);

        // VisualStyle backgroundColor should be a valid atlas index (>= -1)
        EXPECT_GE(backgroundColor->colorUV.y, 0);

        // If backgroundIndex is valid, texture row should be non-zero
        if (backgroundColor->colorUV.y >= 0)
        {
            auto tex = gm.PageTextureData(backgroundColor->gradientPage);
            size_t width = gm.Width();
            size_t row = static_cast<size_t>(backgroundColor->colorUV.y);
            bool anyNonZero = false;
            for (size_t c = 0; c < width; ++c)
            {
                const color4b px = tex[row * width + c];
                if (px.r != 0 || px.g != 0 || px.b != 0 || px.a != 0)
                {
                    anyNonZero = true;
                    break;
                }
            }
            EXPECT_TRUE(anyNonZero);
        }
    }

    // Hash must depend on content, not on the object's address.
    TEST(StyleManagerTest, LayoutStyleHashIsContentBased)
    {
        LayoutStyle a;
        a.layoutData.width = 120;
        LayoutStyle b = a;
        EXPECT_EQ(a.GetHashCode(), b.GetHashCode());

        LayoutStyle c = a;
        c.layoutData.width = 121;
        EXPECT_NE(a.GetHashCode(), c.GetHashCode());
    }


    // Element rects keep the returned pointer for the whole frame, while
    // styles may still be registered mid-frame; it must stay valid.
    TEST(StyleManagerTest, LayoutStylePointerSurvivesLaterRegistrations)
    {
        GradientManager gm(16);
        StyleManager sm(gm);

        Style first{};
        first.margin.top = 1;
        const auto ref = sm.Register(first);
        const LayoutStyle* before = sm.GetLayoutStyle(ref.layoutStyleIndex);
        ASSERT_NE(before, nullptr);

        for (int i = 0; i < 500; ++i)
        {
            Style s{};
            s.layoutData.width = 1000 + i;
            sm.Register(s);
        }

        EXPECT_EQ(sm.GetLayoutStyle(ref.layoutStyleIndex), before);
    }

    TEST(StyleManagerTest, WithTextOverrideChangesOnlyWhatItSets)
    {
        GradientManager gm(16);
        StyleManager sm(gm);

        Style base{};
        for (int i = 0; i < Style::kStateCount; ++i)
        {
            base.states[i].textStyle.fontSize = 14.0f;
            base.states[i].textStyle.fontId   = FontIDFromName("Inter");
        }
        base.states[Style::kHover].textStyle.fontSize = 15.0f;
        const StyleRef ref = sm.Register(base);

        EXPECT_EQ(sm.WithTextOverride(ref, {}), ref) << "nothing to override";

        const FontID serif = FontIDFromName("Noto Serif");
        const StyleRef variant = sm.WithTextOverride(ref, { .font = serif });
        EXPECT_EQ(variant.layoutStyleIndex, ref.layoutStyleIndex);
        EXPECT_EQ(variant.visualBoxStyleIndex, ref.visualBoxStyleIndex);
        EXPECT_NE(variant.visualTextStyleIndex, ref.visualTextStyleIndex);
        for (ElementState state : { ElementState::None, ElementState::Hover })
        {
            const TextStyle& text = sm.GetVisualTextStyle(variant.visualTextStyleIndex, state)->textStyle;
            const TextStyle& orig = sm.GetVisualTextStyle(ref.visualTextStyleIndex, state)->textStyle;
            EXPECT_EQ(text.fontId, serif);
            EXPECT_EQ(text.fontSize, orig.fontSize) << "everything else comes from the base";
            EXPECT_TRUE(text.language.empty());
        }
        EXPECT_EQ(sm.WithTextOverride(ref, { .font = serif }), variant) << "repeat calls reuse the variant";

        const StyleRef japanese = sm.WithTextOverride(ref, { .language = LanguageTag("ja") });
        const TextStyle& ja = sm.GetVisualTextStyle(japanese.visualTextStyleIndex, ElementState::None)->textStyle;
        EXPECT_EQ(ja.language.view(), "ja");
        EXPECT_EQ(ja.fontId, FontIDFromName("Inter")) << "an unset font keeps the CSS font";

        sm.ClearAll();
        const StyleRef reloaded = sm.Register(base);
        const StyleRef again = sm.WithTextOverride(reloaded, { .font = serif });
        ASSERT_NE(sm.GetVisualTextStyle(again.visualTextStyleIndex, ElementState::None), nullptr);
        EXPECT_EQ(sm.GetVisualTextStyle(again.visualTextStyleIndex, ElementState::None)->textStyle.fontId, serif);
    }

    // Caches of style indices (e.g. node-view wire colours) detect a theme
    // reload through the generation, which only ClearAll changes.
    TEST(StyleManagerTest, GenerationChangesOnlyOnClearAll)
    {
        GradientManager gm(16);
        StyleManager sm(gm);

        const uint32_t start = sm.Generation();
        sm.Register(Style{});
        sm.RegisterColorStyle(ColorStyle::Create(color4f{ 1, 0, 0, 1 }));
        EXPECT_EQ(sm.Generation(), start);

        sm.ClearAll();
        EXPECT_NE(sm.Generation(), start);
    }


    // The UI scale is applied to styles as they are registered: fonts scale exactly,
    // every other metric to the nearest whole pixel (a set border stays >= 1px),
    // and "auto"/unbounded sentinels are left alone.
    TEST(StyleManagerTest, RegisterAppliesTheUIScale)
    {
        GradientManager gm(16);
        StyleManager    sm(gm);
        sm.SetUIScale(1.5f);

        Style s{};
        s.padding      = BoxEdges(4);
        s.margin       = BoxEdges(3);
        s.borderWidths = BoxEdges(1);
        s.borderRadius = BoxCorners(6);
        s.iconSize     = 16;
        s.iconTextGap  = 5;
        s.layoutData.width = 100;
        s.layoutData.minHeight = 20;
        s.layoutData.flex.gap = 2;
        for (auto& st : s.states)
            st.textStyle.fontSize = 13.0f;

        const StyleRef ref = sm.Register(s);
        const LayoutStyle* layout = sm.GetLayoutStyle(ref.layoutStyleIndex);
        ASSERT_NE(layout, nullptr);
        EXPECT_EQ(layout->padding.top, 6);
        EXPECT_EQ(layout->margin.left, 5);              // 4.5 rounds up
        EXPECT_EQ(layout->borderWidths.bottom, 2);      // 1.5 rounds up
        EXPECT_EQ(layout->iconSize, 24);
        EXPECT_EQ(layout->iconTextGap, 8);
        EXPECT_EQ(layout->layoutData.width, 150);
        EXPECT_EQ(layout->layoutData.minHeight, 30);
        EXPECT_EQ(layout->layoutData.flex.gap, 3);
        EXPECT_EQ(layout->layoutData.height, LayoutData::kAutoSize);
        EXPECT_EQ(layout->layoutData.maxWidth, LayoutData::kUnboundedMax);

        const VisualBoxStyle* box = sm.GetVisualBoxStyle(ref.visualBoxStyleIndex, ElementState::None);
        ASSERT_NE(box, nullptr);
        EXPECT_EQ(box->borderRadius.topLeft, 9);
        EXPECT_EQ(box->borderWidths.top, 2);

        const VisualTextStyle* text = sm.GetVisualTextStyle(ref.visualTextStyleIndex, ElementState::None);
        ASSERT_NE(text, nullptr);
        EXPECT_FLOAT_EQ(text->textStyle.fontSize, 19.5f) << "fonts are not rounded";
    }

    TEST(StyleManagerTest, ThinBordersStayVisibleWhenScaledDown)
    {
        GradientManager gm(16);
        StyleManager    sm(gm);
        sm.SetUIScale(0.4f);

        Style s{};
        s.borderWidths = BoxEdges(1);
        const StyleRef ref = sm.Register(s);
        EXPECT_EQ(sm.GetLayoutStyle(ref.layoutStyleIndex)->borderWidths.top, 1);
    }

    // Sizes given in code (line widths, thresholds, default widths) scale like
    // style metrics: to whole pixels, and a non-zero size never vanishes.
    TEST(StyleManagerTest, PxScalesCodeSizesToWholePixels)
    {
        GradientManager gm(16);
        StyleManager    sm(gm);
        sm.SetUIScale(1.5f);
        EXPECT_EQ(sm.Px(4), 6);
        EXPECT_EQ(sm.Px(3), 5);
        EXPECT_EQ(sm.Px(0), 0);
        EXPECT_EQ(sm.Px(-2), -3);
        EXPECT_FLOAT_EQ(sm.Px(3.0f), 5.0f);

        sm.SetUIScale(0.4f);
        EXPECT_EQ(sm.Px(1), 1);
        EXPECT_EQ(sm.Px(-1), -1);
        EXPECT_FLOAT_EQ(sm.Px(1.0f), 1.0f);
    }

    // A style sized from measured pixels registers as-is; ToPixels gives the
    // scaled copy of a CSS-pixel style to start from.
    TEST(StyleManagerTest, PixelStylesRegisterUnscaled)
    {
        GradientManager gm(16);
        StyleManager    sm(gm);
        sm.SetUIScale(1.5f);

        Style css{};
        css.padding  = BoxEdges(4);
        css.iconSize = 16;
        Style px = sm.ToPixels(css);
        EXPECT_EQ(px.padding.top, 6);
        EXPECT_EQ(px.iconSize, 24);

        px.iconSize = 37;   // measured
        const StyleRef ref = sm.Register(px, StyleUnits::Pixels);
        const LayoutStyle* layout = sm.GetLayoutStyle(ref.layoutStyleIndex);
        EXPECT_EQ(layout->iconSize, 37);
        EXPECT_EQ(layout->padding.top, 6);
    }

    // Shadows are registered per state at the UI scale; the focus ring only in focused states.
    TEST(StyleManagerTest, ShadowsAreScaledAndTheFocusRingIsInFocusedStatesOnly)
    {
        GradientManager gm(16);
        StyleManager sm(gm);
        sm.SetUIScale(2.0f);

        Style s{};
        for (StyleState& state : s.states)
            state.boxShadow[0] = BoxShadow{ .x = 1, .y = -2, .blur = 3, .spread = 4, .color = { 0, 0, 0, 90 } };
        s.focusRing = BoxShadow{ .spread = 3, .color = { 0, 0, 255, 77 } };
        const StyleRef ref = sm.Register(s);

        const VisualShadowStyle* normal  = sm.GetVisualShadowStyle(ComputeGPUStyleIndex(ref.visualBoxStyleIndex, ElementState::None));
        const VisualShadowStyle* focused = sm.GetVisualShadowStyle(ComputeGPUStyleIndex(ref.visualBoxStyleIndex, ElementState::Focused | ElementState::Hover));
        ASSERT_NE(normal, nullptr);
        ASSERT_NE(focused, nullptr);
        EXPECT_EQ(normal->layers[0].x, 2);
        EXPECT_EQ(normal->layers[0].y, -4);
        EXPECT_EQ(normal->layers[0].blur, 6);
        EXPECT_EQ(normal->layers[0].spread, 8);
        EXPECT_FALSE(normal->layers[0].IsNone());
        EXPECT_TRUE(normal->layers[1].IsNone());
        EXPECT_TRUE(normal->ring.IsNone());
        EXPECT_EQ(focused->ring.spread, 6);
        EXPECT_FALSE(focused->ring.IsNone());
    }

    // An inset focus ring is a border-only box style with the element's corners.
    TEST(StyleManagerTest, AnInsetFocusRingIsAnOutlineBoxStyle)
    {
        GradientManager gm(16);
        StyleManager sm(gm);
        Style s{};
        s.borderRadius = BoxCorners(4);
        s.focusRing    = BoxShadow{ .spread = 1, .color = { 0, 0, 255, 255 }, .inset = true };
        const StyleRef ref = sm.Register(s);

        const VisualShadowStyle* focused = sm.GetVisualShadowStyle(ComputeGPUStyleIndex(ref.visualBoxStyleIndex, ElementState::Focused));
        ASSERT_NE(focused, nullptr);
        EXPECT_TRUE(focused->ring.IsNone()) << "nothing outside the box";
        ASSERT_NE(focused->insetRing, 0u);
        const VisualBoxStyle& outline = sm.GetVisualBoxStylesSpan()[focused->insetRing - 1];
        EXPECT_EQ(outline.borderWidths, BoxEdges(1));
        EXPECT_EQ(outline.borderRadius, BoxCorners(4));
        EXPECT_NE(outline.borderColorIndex[0], VisualColorStyleIndex::Invalid);
    }

    TEST(StyleManagerTest, AStyleWithoutShadowsHasNone)
    {
        GradientManager gm(16);
        StyleManager sm(gm);
        const StyleRef ref = sm.Register(Style{});
        EXPECT_EQ(sm.GetVisualShadowStyle(ComputeGPUStyleIndex(ref.visualBoxStyleIndex, ElementState::None)), nullptr);
    }

    // Two styles that differ only in their shadow are two box styles.
    TEST(StyleManagerTest, ShadowsTellBoxStylesApart)
    {
        GradientManager gm(16);
        StyleManager sm(gm);
        Style plain{};
        Style shadowed{};
        shadowed.states[Style::kNormal].boxShadow[0] = BoxShadow{ .y = 1, .blur = 2, .color = { 0, 0, 0, 40 } };
        EXPECT_NE(sm.Register(plain).visualBoxStyleIndex, sm.Register(shadowed).visualBoxStyleIndex);
    }
}
