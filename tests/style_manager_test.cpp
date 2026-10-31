#include "gtest/gtest.h"

#include <gui_core/elements/gui_element_storage.h>
#include <gui_core/graphics/gui_renderer.h>

#include <gui_core/style/gui_style_manager.h>
#include <gui_core/style/gui_gradient_manager.h>
#include <gui_core/style/gui_style.h>
#include <gui_controls/gui_transition_manager.h>

namespace tachyon
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
        s1.states[ElementState::None].backgroundColor.gradient = MakeTwoStopGradient();

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
        s3.states[ElementState::None].backgroundColor.gradient = MakeTwoStopGradient();
        // modify a stop to produce different visual (toggle color)
        auto& stop = s3.states[ElementState::None].backgroundColor.gradient.stops[0];
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
        s.states[ElementState::None].backgroundColor.gradient = MakeTwoStopGradient();
        s.states[ElementState::None].borderColors[0].gradient = MakeTwoStopGradient();
        s.states[ElementState::None].borderColors[1].gradient = MakeTwoStopGradient();
        s.states[ElementState::None].borderColors[2].gradient = MakeTwoStopGradient();
        s.states[ElementState::None].borderColors[3].gradient = MakeTwoStopGradient();
        s.states[ElementState::None].textColor.gradient = MakeTwoStopGradient();

        auto idx = sm.Register(s);
        ASSERT_GE(int(idx.visualBoxStyleIndex), 0);

        const VisualBoxStyle* vs = sm.GetVisualBoxStyle(idx.visualBoxStyleIndex, ElementState::None);
        ASSERT_NE(vs, nullptr);


        const VisualColorStyle* backgroundColor = sm.GetVisualColorStyle(vs->backgroundColorIndex);

        // VisualStyle backgroundColor should be a valid atlas index (>= -1)
        EXPECT_GE(backgroundColor->colorUV.y, 0);

        // The texture row should be non-zero
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
        base.states[ElementState::Hover].textStyle.fontSize = 15.0f;
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
        shadowed.states[ElementState::None].boxShadow[0] = BoxShadow{ .y = 1, .blur = 2, .color = { 0, 0, 0, 40 } };
        EXPECT_NE(sm.Register(plain).visualBoxStyleIndex, sm.Register(shadowed).visualBoxStyleIndex);
    }

    // A box background hides what's under it when every color it paints with is opaque.
    TEST(StyleManagerTest, OpaqueBackgroundsArePerState)
    {
        GradientManager gm(16);
        StyleManager    sm(gm);
        auto opaque = [&](const ColorStyle& background, int state = int(ElementState::None)) {
            Style s{};
            for (StyleState& st : s.states)
                st.backgroundColor = background;
            const StyleRef ref = sm.Register(s);
            return sm.IsBoxBackgroundOpaque((uint32_t(ref.visualBoxStyleIndex) - 1) * Style::kStateCount + uint32_t(state));
        };
        EXPECT_TRUE (opaque(ColorStyle::Create(color4b{ 10, 20, 30, 255 })));
        EXPECT_FALSE(opaque(ColorStyle::Create(color4b{ 10, 20, 30, 254 })));
        EXPECT_FALSE(opaque(ColorStyle{})) << "no background";
        EXPECT_TRUE (opaque(ColorStyle::Create(color4b{ 0, 0, 0, 255 }, color4b{ 255, 255, 255, 255 }, ColorType::LinearGradient)));
        EXPECT_FALSE(opaque(ColorStyle::Create(color4b{ 0, 0, 0, 255 }, color4b{ 255, 255, 255, 0 }, ColorType::LinearGradient)));
        EXPECT_FALSE(opaque(ColorStyle::Create(color4b{ 0, 0, 0, 255 }, color4b{ 255, 255, 255, 255 }, ColorType::StretchedTexture)));
        EXPECT_TRUE (opaque(ColorStyle::Create(color4b{ 1, 0, 0, 255 }, color4b{ 0, 1, 0, 255 }, color4b{ 0, 0, 1, 255 }, ColorType::InterpolatedTriangle)));
        EXPECT_FALSE(opaque(ColorStyle::Create(color4b{ 1, 0, 0, 255 }, color4b{ 0, 1, 0, 255 }, color4b{ 0, 0, 1, 255 }, color4b{ 0, 0, 0, 0 }, ColorType::InterpolatedBox)));

        // Per state: opaque only while hovered.
        Style s{};
        s.states[ElementState::Hover].backgroundColor = ColorStyle::Create(color4b{ 10, 20, 30, 255 });
        const StyleRef ref = sm.Register(s);
        const uint32_t base = (uint32_t(ref.visualBoxStyleIndex) - 1) * Style::kStateCount;
        EXPECT_FALSE(sm.IsBoxBackgroundOpaque(base + int(ElementState::None)));
        EXPECT_TRUE (sm.IsBoxBackgroundOpaque(base + int(ElementState::Hover)));
        EXPECT_FALSE(sm.IsBoxBackgroundOpaque(1000000)) << "out of range";

        sm.ClearAll();
        EXPECT_FALSE(sm.IsBoxBackgroundOpaque(base + int(ElementState::Hover))) << "cleared with the styles";
    }

    // text-shadow goes with the state's text style, at the UI scale, its color registered.
    TEST(StyleManagerTest, TextShadowsAreScaledPerState)
    {
        GradientManager gm(16);
        StyleManager    sm(gm);
        sm.SetUIScale(2.0f);

        Style s{};
        s.states[ElementState::Hover].textShadow[0] = BoxShadow{ .x = 1, .y = -2, .blur = 3, .color = { 255, 0, 0, 255 } };
        s.states[ElementState::Hover].textShadow[1] = BoxShadow{ .blur = 4, .color = { 0, 0, 255, 128 } };
        const StyleRef ref = sm.Register(s);

        const VisualTextStyle* normal = sm.GetVisualTextStyle(ref.visualTextStyleIndex, ElementState::None);
        ASSERT_NE(normal, nullptr);
        for (const VisualShadow& v : normal->textShadows)
            EXPECT_TRUE(v.IsNone());

        const VisualTextStyle* hover = sm.GetVisualTextStyle(ref.visualTextStyleIndex, ElementState::Hover);
        ASSERT_NE(hover, nullptr);
        EXPECT_EQ(hover->textShadows[0].x, 2);
        EXPECT_EQ(hover->textShadows[0].y, -4);
        EXPECT_EQ(hover->textShadows[0].blur, 6u);
        EXPECT_EQ(hover->textShadows[0].color, sm.RegisterColorStyle(ColorStyle::Create(color4b{ 255, 0, 0, 255 })));
        EXPECT_EQ(hover->textShadows[1].blur, 8u);
        EXPECT_EQ(hover->textShadows[1].color, sm.RegisterColorStyle(ColorStyle::Create(color4b{ 0, 0, 255, 128 })));

        // Text styles that differ only in their shadows aren't shared.
        EXPECT_NE(sm.Register(Style{}).visualTextStyleIndex, ref.visualTextStyleIndex);
    }

    // A decoration line without a color of its own is drawn in the text color; a state
    // without a text color draws it transparent.
    TEST(StyleManagerTest, DecorationColorsArePerStateAndDefaultToTheTextColor)
    {
        GradientManager gm(16);
        StyleManager sm(gm);
        const color4b red{ 255, 0, 0, 255 }, green{ 0, 255, 0, 255 };

        Style s{};
        s.states[ElementState::None].textColor = ColorStyle::Create(red);
        s.states[ElementState::Hover].textColor = ColorStyle::Create(red);
        s.states[ElementState::Hover].decorationColors[size_t(DecorationLine::Over)] = ColorStyle::Create(green);
        const StyleRef ref = sm.Register(s);

        const VisualTextStyle* normal = sm.GetVisualTextStyle(ref.visualTextStyleIndex, ElementState::None);
        const VisualTextStyle* hover  = sm.GetVisualTextStyle(ref.visualTextStyleIndex, ElementState::Hover);
        const VisualTextStyle* active = sm.GetVisualTextStyle(ref.visualTextStyleIndex, ElementState::Active);
        ASSERT_TRUE(normal && hover && active);
        EXPECT_EQ(normal->textColorIndex, sm.RegisterColorStyle(ColorStyle::Create(red)));
        for (VisualColorStyleIndex line : normal->decorationColorIndex)
            EXPECT_EQ(line, normal->textColorIndex);
        EXPECT_EQ(hover->decorationColorIndex[size_t(DecorationLine::Over)], sm.RegisterColorStyle(ColorStyle::Create(green)));
        EXPECT_EQ(hover->decorationColorIndex[size_t(DecorationLine::Under)], hover->textColorIndex);
        EXPECT_EQ(active->textColorIndex, sm.RegisterColorStyle(ColorStyle::Create(color4b{ 0, 0, 0, 0 })));

        // Text styles that differ only in a decoration color aren't shared.
        Style other = s;
        other.states[ElementState::Hover].decorationColors[size_t(DecorationLine::Over)] = ColorStyle::Create(red);
        EXPECT_NE(sm.Register(other).visualTextStyleIndex, ref.visualTextStyleIndex);
    }

    // A box drawn without a style (GPU box style 0) gets the fallback style, which is
    // registered first, and again after ClearAll.
    TEST(StyleManagerTest, TheFallbackStyleIsRegisteredFirst)
    {
        GradientManager gm(16);
        StyleManager sm(gm);

        for (int pass = 0; pass < 2; ++pass)
        {
            const StyleRef fallback = sm.FallbackStyle();
            EXPECT_EQ(fallback.layoutStyleIndex, LayoutStyleIndex(1)) << "pass " << pass;
            EXPECT_EQ(fallback.visualBoxStyleIndex, VisualBoxStyleIndex(1)) << "pass " << pass;
            ASSERT_NE(sm.GetLayoutStyle(fallback.layoutStyleIndex), nullptr);
            EXPECT_EQ(sm.GetLayoutStyle(fallback.layoutStyleIndex)->padding, BoxEdges(10));

            const VisualBoxStyle* normal = sm.GetVisualBoxStyleAt(0);
            const VisualBoxStyle* hover  = sm.GetVisualBoxStyleAt(uint32_t(ElementState::Hover));
            ASSERT_TRUE(normal && hover);
            const VisualColorStyleIndex background = sm.RegisterColorStyle(ColorStyle::Create(color4b(101, 133, 149, 255)));
            const VisualColorStyleIndex border     = sm.RegisterColorStyle(ColorStyle::Create(color4b(121, 153, 169, 255)));
            EXPECT_EQ(normal->backgroundColorIndex, background);
            EXPECT_EQ(normal->borderColorIndex[0], border);
            EXPECT_EQ(hover->backgroundColorIndex, border);

            EXPECT_NE(sm.Register(Style{}).layoutStyleIndex, LayoutStyleIndex(1));
            sm.ClearAll();
        }
    }

    // How text sits in its box is part of the layout style; the text style is shared.
    TEST(StyleManagerTest, TheTextLayoutIsPartOfTheLayoutStyle)
    {
        GradientManager gm(16);
        StyleManager sm(gm);

        Style start{}, centered{};
        centered.textLayoutStyle.horizontalAlignment = TextAlign::Center;
        centered.textLayoutStyle.textWrapping        = TextWrap::Wrap;
        const StyleRef a = sm.Register(start);
        const StyleRef b = sm.Register(centered);
        EXPECT_NE(a.layoutStyleIndex, b.layoutStyleIndex);
        EXPECT_EQ(a.visualTextStyleIndex, b.visualTextStyleIndex);
        ASSERT_NE(sm.GetLayoutStyle(b.layoutStyleIndex), nullptr);
        EXPECT_EQ(sm.GetLayoutStyle(b.layoutStyleIndex)->textLayoutStyle, centered.textLayoutStyle);
        EXPECT_EQ(sm.GetLayoutStyle(a.layoutStyleIndex)->textLayoutStyle, TextLayoutStyle{});
        EXPECT_EQ(sm.Register(centered).layoutStyleIndex, b.layoutStyleIndex);
    }
    // A wrapping variant of a style changes only its layout's flex-wrap and is registered once.
    TEST(StyleManagerTest, WithFlexWrapChangesOnlyTheWrap)
    {
        GradientManager gm(16);
        StyleManager sm(gm);
        Style s{};
        s.padding = BoxEdges(3);
        s.layoutData.flex.gap = 7;
        s.states[ElementState::None].backgroundColor = ColorStyle::Create(color4b(10, 20, 30, 255));
        const StyleRef base = sm.Register(s);

        const StyleRef wrapped = sm.WithFlexWrap(base, FlexWrap::Wrap);
        EXPECT_NE(wrapped.layoutStyleIndex, base.layoutStyleIndex);
        EXPECT_EQ(wrapped.visualBoxStyleIndex, base.visualBoxStyleIndex);
        EXPECT_EQ(wrapped.visualTextStyleIndex, base.visualTextStyleIndex);
        const LayoutStyle* a = sm.GetLayoutStyle(base.layoutStyleIndex);
        const LayoutStyle* b = sm.GetLayoutStyle(wrapped.layoutStyleIndex);
        ASSERT_TRUE(a && b);
        EXPECT_EQ(b->layoutData.flex.wrap, FlexWrap::Wrap);
        LayoutStyle same = *b;
        same.layoutData.flex.wrap = a->layoutData.flex.wrap;
        EXPECT_EQ(same, *a) << "nothing else changed";

        EXPECT_EQ(sm.WithFlexWrap(base, FlexWrap::Wrap).layoutStyleIndex, wrapped.layoutStyleIndex) << "registered once";
        EXPECT_EQ(sm.WithFlexWrap(base, FlexWrap::NoWrap), base) << "already that way";
        EXPECT_EQ(sm.WithFlexWrap(wrapped, FlexWrap::NoWrap).layoutStyleIndex, base.layoutStyleIndex) << "and back to the base";
    }
    // A state's opacity is kept with the style; two styles that differ only in it stay apart.
    TEST(StyleManagerTest, OpacityIsKeptPerState)
    {
        GradientManager gm(16);
        StyleManager sm(gm);
        Style faded{};
        faded.states[ElementState::Disabled].opacity = 0.45f;
        Style plain{};
        const StyleRef fadedRef = sm.Register(faded);
        const StyleRef plainRef = sm.Register(plain);
        EXPECT_NE(fadedRef.visualBoxStyleIndex, plainRef.visualBoxStyleIndex);
        EXPECT_FLOAT_EQ(sm.GetOpacity(GUIRenderer::StyleIndex(fadedRef.visualBoxStyleIndex, ElementState::Disabled)), 0.45f);
        EXPECT_LT(sm.GetOpacity(GUIRenderer::StyleIndex(fadedRef.visualBoxStyleIndex, ElementState::None)), 0.0f);
        EXPECT_LT(sm.GetOpacity(GUIRenderer::StyleIndex(plainRef.visualBoxStyleIndex, ElementState::Disabled)), 0.0f);
        EXPECT_FLOAT_EQ(sm.GetOpacity(GUIRenderer::StyleIndex(fadedRef.visualBoxStyleIndex, ElementState::Disabled | ElementState::Hover)), 0.45f)
            << "disabled stands for every state it's in";
    }
}
