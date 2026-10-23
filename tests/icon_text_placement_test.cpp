#include <gtest/gtest.h>

#include <gui_core/text/gui_icon_text_placement.h>

namespace interstice
{
    // ---------------------------------------------------------------------
    // ComputeIconTextPlacement / IconAwareLayoutMaxWidth unit tests.
    //
    // The placement model: icons sit ADJACENT to their text (not anchored to
    // rect edges). Vertical centering for Left/Right icons is around the
    // text height, and Bottom icons sit iconTextGap below the text — so a
    // stretched cell keeps the icon next to its label rather than letting
    // the label and icon drift apart.
    //
    // Conventions for readable assertions:
    //   * rect origin is (0, 0) unless otherwise noted
    //   * text is 40 × 17 (shorter than typical icon)
    //   * icon is 24 × 24 (SVG natural size)
    //   * gap is 8
    // ---------------------------------------------------------------------

    namespace
    {
        constexpr int32_t kTextW = 40;
        constexpr int32_t kTextH = 17;
        constexpr IconMeasurement kIcon = { 24, 24 };
        constexpr int32_t kGap  = 8;

        // Helper: true if two rects share any pixel.
        bool RectsOverlap(const IntRect& a, const IntRect& b)
        {
            return !(a.x + a.w <= b.x || b.x + b.w <= a.x ||
                     a.y + a.h <= b.y || b.y + b.h <= a.y);
        }
    }

    // ---- IconAwareLayoutMaxWidth ----------------------------------------

    TEST(IconAwareLayoutMaxWidth, NoIconReturnsFullWidth)
    {
        EXPECT_EQ(400, IconAwareLayoutMaxWidth(400, kIcon, IconSide::Right, kGap, /*hasIcon=*/false));
    }

    TEST(IconAwareLayoutMaxWidth, LeftIconReservesIconPlusGap)
    {
        EXPECT_EQ(400 - kGap - kIcon.width,
                  IconAwareLayoutMaxWidth(400, kIcon, IconSide::Left, kGap, true));
    }

    TEST(IconAwareLayoutMaxWidth, RightIconReservesIconPlusGap)
    {
        EXPECT_EQ(400 - kGap - kIcon.width,
                  IconAwareLayoutMaxWidth(400, kIcon, IconSide::Right, kGap, true));
    }

    TEST(IconAwareLayoutMaxWidth, TopIconDoesNotTouchWidth)
    {
        EXPECT_EQ(400, IconAwareLayoutMaxWidth(400, kIcon, IconSide::Top, kGap, true));
    }

    TEST(IconAwareLayoutMaxWidth, BottomIconDoesNotTouchWidth)
    {
        EXPECT_EQ(400, IconAwareLayoutMaxWidth(400, kIcon, IconSide::Bottom, kGap, true));
    }

    TEST(IconAwareLayoutMaxWidth, ClampedToZeroWhenRectIsTinierThanIcon)
    {
        EXPECT_EQ(0, IconAwareLayoutMaxWidth(10, kIcon, IconSide::Left, kGap, true));
    }

    // ---- Content-sized rect (common auto-size case) ----------------------
    // Icon and text are placed adjacent; they don't overlap in the relevant
    // axis.

    TEST(IconTextPlacement, LeftIcon_ContentSizedRect_IconFirstThenText)
    {
        // Combined = icon 24 + gap 8 + text 40 = 72; rect sized to icon height
        // so icon centering around textHeight keeps it within rect vertically.
        const IntRect rect{ .x = 0, .y = 0, .w = 72, .h = 24 };
        const auto p = ComputeIconTextPlacement(rect, kTextW, kTextH, kIcon,
                                                IconSide::Left, kGap, TextAlign::Start);

        EXPECT_EQ(p.iconRect.x, 0);
        EXPECT_EQ(p.iconRect.w, kIcon.width);
        // Text shifted past icon + gap so the two don't collide in X
        EXPECT_EQ(p.textRect.x, kIcon.width + kGap);
        EXPECT_FALSE(RectsOverlap(p.textRect, p.iconRect));
    }

    TEST(IconTextPlacement, RightIcon_ContentSizedRect_TextFirstThenIcon)
    {
        const IntRect rect{ .x = 0, .y = 0, .w = 72, .h = 24 };
        const auto p = ComputeIconTextPlacement(rect, kTextW, kTextH, kIcon,
                                                IconSide::Right, kGap, TextAlign::Start);

        EXPECT_EQ(p.textRect.x, 0);
        // textRect width reserves room for icon + gap on the right.
        EXPECT_EQ(p.textRect.w, rect.w - kGap - kIcon.width);
        EXPECT_EQ(p.iconRect.x, kTextW + kGap);
        EXPECT_FALSE(RectsOverlap(p.textRect, p.iconRect));
    }

    TEST(IconTextPlacement, TopIcon_ContentSizedRect_IconAtTopTextBelow)
    {
        const IntRect rect{ .x = 0, .y = 0, .w = 40, .h = kTextH + kGap + kIcon.height };
        const auto p = ComputeIconTextPlacement(rect, kTextW, kTextH, kIcon,
                                                IconSide::Top, kGap, TextAlign::Start);

        EXPECT_EQ(p.iconRect.y, 0);
        EXPECT_EQ(p.iconRect.h, kIcon.height);
        EXPECT_EQ(p.textRect.y, kIcon.height + kGap);
        EXPECT_FALSE(RectsOverlap(p.textRect, p.iconRect));
    }

    TEST(IconTextPlacement, BottomIcon_ContentSizedRect_TextOnTopIconBelowText)
    {
        const IntRect rect{ .x = 0, .y = 0, .w = 40, .h = kTextH + kGap + kIcon.height };
        const auto p = ComputeIconTextPlacement(rect, kTextW, kTextH, kIcon,
                                                IconSide::Bottom, kGap, TextAlign::Start);

        EXPECT_EQ(p.textRect.y, rect.y);
        // Icon sits one gap below the text's natural height — adjacent to
        // the text, not anchored to rect's bottom edge.
        EXPECT_EQ(p.iconRect.y, rect.y + kTextH + kGap);
    }

    // ---- Left/Right vertical centering -----------------------------------
    // Icon and text share one centre line inside their block (the taller of
    // the two), so a taller icon doesn't stick out above the block and the
    // icon stays aligned with its label rather than the cell extremes.

    TEST(IconTextPlacement, LeftIcon_CenteredOnTheTextLine)
    {
        const IntRect rect{ .x = 0, .y = 0, .w = 200, .h = 200 };
        const auto p = ComputeIconTextPlacement(rect, kTextW, kTextH, kIcon,
                                                IconSide::Left, kGap, TextAlign::Start);

        EXPECT_EQ(p.iconRect.y, rect.y);
        EXPECT_EQ(p.textRect.y, rect.y + (kIcon.height - kTextH) / 2);
    }

    TEST(IconTextPlacement, RightIcon_CenteredOnTheTextLine)
    {
        const IntRect rect{ .x = 0, .y = 0, .w = 200, .h = 200 };
        const auto p = ComputeIconTextPlacement(rect, kTextW, kTextH, kIcon,
                                                IconSide::Right, kGap, TextAlign::Start);

        EXPECT_EQ(p.iconRect.y, rect.y);
        EXPECT_EQ(p.textRect.y, rect.y + (kIcon.height - kTextH) / 2);
    }

    TEST(IconTextPlacement, IconShorterThanText_CenteredOnTheTextLine)
    {
        const IntRect rect{ .x = 0, .y = 0, .w = 200, .h = 200 };
        const IconMeasurement smallIcon{ 10, 10 };
        const auto p = ComputeIconTextPlacement(rect, kTextW, kTextH, smallIcon,
                                                IconSide::Left, kGap, TextAlign::Start);

        EXPECT_EQ(p.textRect.y, rect.y);
        EXPECT_EQ(p.iconRect.y, rect.y + (kTextH - smallIcon.height) / 2);
    }

    // ---- Stretched rect: icon stays adjacent to text ---------------------

    TEST(IconTextPlacement, BottomIcon_StretchedTall_IconStaysAdjacentToText)
    {
        const IntRect rect{ .x = 0, .y = 0, .w = 40, .h = 200 };
        const auto p = ComputeIconTextPlacement(rect, kTextW, kTextH, kIcon,
                                                IconSide::Bottom, kGap, TextAlign::Start);

        // Icon is placed immediately under the text regardless of extra
        // vertical space — it doesn't float to the bottom of the cell.
        EXPECT_EQ(p.iconRect.y, rect.y + kTextH + kGap);
    }

    TEST(IconTextPlacement, TopIcon_StretchedTall_IconAnchorsToRectTop)
    {
        const IntRect rect{ .x = 0, .y = 0, .w = 40, .h = 200 };
        const auto p = ComputeIconTextPlacement(rect, kTextW, kTextH, kIcon,
                                                IconSide::Top, kGap, TextAlign::Start);

        // Top case hugs rect.y by definition (text flows below).
        EXPECT_EQ(p.iconRect.y, rect.y);
        EXPECT_FALSE(RectsOverlap(p.textRect, p.iconRect));
    }

    // Right icon + text-align: end in a stretched cell — the icon must not
    // overlap the text horizontally.
    TEST(IconTextPlacement, RightIcon_Stretched_AlignEnd_NoHorizontalOverlap)
    {
        const IntRect rect{ .x = 0, .y = 0, .w = 400, .h = 24 };
        const auto p = ComputeIconTextPlacement(rect, kTextW, kTextH, kIcon,
                                                IconSide::Right, kGap, TextAlign::End);

        // textRect.x stays at rect.x (the layout handles alignment inside),
        // and its width reserves room for icon + gap.
        EXPECT_EQ(p.textRect.x, rect.x);
        EXPECT_EQ(p.textRect.w, rect.w - kGap - kIcon.width);
        // Icon ends at rect's right edge under End-alignment.
        EXPECT_EQ(p.iconRect.x + p.iconRect.w, rect.x + rect.w);
        // Icon's left edge is at or past the text rect's right edge.
        EXPECT_GE(p.iconRect.x, p.textRect.x + p.textRect.w);
    }

    // Left icon + text-align: end — mirror of the above. Text must not be
    // placed beneath the icon.
    TEST(IconTextPlacement, LeftIcon_Stretched_AlignEnd_NoHorizontalOverlap)
    {
        const IntRect rect{ .x = 0, .y = 0, .w = 400, .h = 24 };
        const auto p = ComputeIconTextPlacement(rect, kTextW, kTextH, kIcon,
                                                IconSide::Left, kGap, TextAlign::End);

        // Text starts after the icon.
        EXPECT_GE(p.textRect.x, p.iconRect.x + p.iconRect.w);
    }

    // ---- Alignment sanity --------------------------------------------------

    TEST(IconTextPlacement, RightIcon_AlignStart_IconSitsImmediatelyAfterText)
    {
        const IntRect rect{ .x = 0, .y = 0, .w = 400, .h = 24 };
        const auto p = ComputeIconTextPlacement(rect, kTextW, kTextH, kIcon,
                                                IconSide::Right, kGap, TextAlign::Start);

        // With Start alignment the combined block sits at rect.x;
        // icon sits exactly text-width + gap from rect.x.
        EXPECT_EQ(p.iconRect.x, rect.x + kTextW + kGap);
    }

    TEST(IconTextPlacement, RightIcon_AlignCenter_CombinedCenteredInRect)
    {
        const IntRect rect{ .x = 0, .y = 0, .w = 400, .h = 24 };
        const auto p = ComputeIconTextPlacement(rect, kTextW, kTextH, kIcon,
                                                IconSide::Right, kGap, TextAlign::Center);

        const int32_t combined = kTextW + kGap + kIcon.width;
        // Icon ends at rect.x + (rect.w - combined) / 2 + combined
        EXPECT_EQ(p.iconRect.x + p.iconRect.w,
                  rect.x + (rect.w - combined) / 2 + combined);
    }

    // Rect with a non-zero origin — make sure rect.x / rect.y are honored.
    TEST(IconTextPlacement, RightIcon_NonZeroOrigin_CoordinatesOffsetCorrectly)
    {
        const IntRect rect{ .x = 100, .y = 50, .w = 72, .h = 24 };
        const auto p = ComputeIconTextPlacement(rect, kTextW, kTextH, kIcon,
                                                IconSide::Right, kGap, TextAlign::Start);

        EXPECT_EQ(p.iconRect.x, rect.x + kTextW + kGap);
        EXPECT_EQ(p.textRect.x, rect.x);
        EXPECT_EQ(p.textRect.y, rect.y + (kIcon.height - kTextH) / 2);
    }

    // ---- Block vertical alignment ----------------------------------------
    // verticalAlignment shifts the whole [icon, text] block by the same Y
    // offset so the pair stays together. Start/Top = no shift, Center =
    // centered in rect.h, End/Bottom = anchored to rect.h.

    TEST(IconTextPlacement, VerticalAlign_Start_DoesNotShift)
    {
        const IntRect rect{ .x = 0, .y = 0, .w = 40, .h = 200 };
        const auto pTop = ComputeIconTextPlacement(rect, kTextW, kTextH, kIcon,
                                                   IconSide::Top, kGap, TextAlign::Start, TextAlign::Start);
        EXPECT_EQ(pTop.iconRect.y, rect.y);
        EXPECT_EQ(pTop.textRect.y, rect.y + kIcon.height + kGap);
    }

    TEST(IconTextPlacement, VerticalAlign_Center_CentersBlockInRect)
    {
        const IntRect rect{ .x = 0, .y = 0, .w = 40, .h = 200 };
        const int32_t combinedH = kTextH + kGap + kIcon.height;
        const int32_t expectedBlockY = rect.y + (rect.h - combinedH) / 2;

        const auto p = ComputeIconTextPlacement(rect, kTextW, kTextH, kIcon,
                                                IconSide::Top, kGap, TextAlign::Start, TextAlign::Center);
        EXPECT_EQ(p.iconRect.y, expectedBlockY);
        EXPECT_EQ(p.textRect.y, expectedBlockY + kIcon.height + kGap);
    }

    TEST(IconTextPlacement, VerticalAlign_End_AnchorsBlockToRectBottom)
    {
        const IntRect rect{ .x = 0, .y = 0, .w = 40, .h = 200 };
        const auto p = ComputeIconTextPlacement(rect, kTextW, kTextH, kIcon,
                                                IconSide::Bottom, kGap, TextAlign::Start, TextAlign::End);

        // For IconSide::Bottom: block = [text][gap][icon]. iconRect.y should
        // hit the rect's bottom edge when End-aligned vertically.
        EXPECT_EQ(p.iconRect.y + p.iconRect.h, rect.y + rect.h);
        // Text sits iconTextGap above the icon's top.
        EXPECT_EQ(p.textRect.y + kTextH + kGap, p.iconRect.y);
    }

    TEST(IconTextPlacement, VerticalAlign_LeftIcon_Center_ShiftsBothRects)
    {
        const IntRect rect{ .x = 0, .y = 0, .w = 200, .h = 200 };
        const int32_t combinedH = std::max(kTextH, kIcon.height);
        const int32_t expectedBlockY = rect.y + (rect.h - combinedH) / 2;

        const auto p = ComputeIconTextPlacement(rect, kTextW, kTextH, kIcon,
                                                IconSide::Left, kGap, TextAlign::Start, TextAlign::Center);
        EXPECT_EQ(p.textRect.y, expectedBlockY + (combinedH - kTextH) / 2);
        EXPECT_EQ(p.iconRect.y, expectedBlockY + (combinedH - kIcon.height) / 2);
    }

    TEST(IconTextPlacement, VerticalAlign_BottomAlias_BehavesLikeEnd)
    {
        const IntRect rect{ .x = 0, .y = 0, .w = 40, .h = 200 };
        const auto pEnd    = ComputeIconTextPlacement(rect, kTextW, kTextH, kIcon,
                                                      IconSide::Top, kGap, TextAlign::Start, TextAlign::End);
        const auto pBottom = ComputeIconTextPlacement(rect, kTextW, kTextH, kIcon,
                                                      IconSide::Top, kGap, TextAlign::Start, TextAlign::Bottom);
        EXPECT_EQ(pEnd.iconRect.y,  pBottom.iconRect.y);
        EXPECT_EQ(pEnd.textRect.y,  pBottom.textRect.y);
    }

    TEST(IconTextPlacement, VerticalAlign_TopAlias_BehavesLikeStart)
    {
        const IntRect rect{ .x = 0, .y = 0, .w = 40, .h = 200 };
        const auto pStart = ComputeIconTextPlacement(rect, kTextW, kTextH, kIcon,
                                                     IconSide::Top, kGap, TextAlign::Start, TextAlign::Start);
        const auto pTop   = ComputeIconTextPlacement(rect, kTextW, kTextH, kIcon,
                                                     IconSide::Top, kGap, TextAlign::Start, TextAlign::Top);
        EXPECT_EQ(pStart.iconRect.y, pTop.iconRect.y);
        EXPECT_EQ(pStart.textRect.y, pTop.textRect.y);
    }

    // ---- Scenarios from the panel_misc icon boxes -------------------------

    // Bottom box with a flex-stretched cell: the icon sits iconTextGap below the text.
    TEST(IconTextPlacement, Regression_MiscPanelBottomBox_IconBelowText)
    {
        const IntRect rect{ .x = 0, .y = 0, .w = 400, .h = 200 };
        const auto p = ComputeIconTextPlacement(rect, kTextW, kTextH, kIcon,
                                                IconSide::Bottom, kGap, TextAlign::End);

        EXPECT_EQ(p.iconRect.y, rect.y + kTextH + kGap);
    }

    // Right box: text-align: end + stretched width must not draw the icon over
    // the text's trailing characters.
    TEST(IconTextPlacement, Regression_MiscPanelRightBox_IconDoesNotCoverText)
    {
        const IntRect rect{ .x = 0, .y = 0, .w = 400, .h = 24 };
        const auto p = ComputeIconTextPlacement(rect, kTextW, kTextH, kIcon,
                                                IconSide::Right, kGap, TextAlign::End);

        EXPECT_GE(p.iconRect.x, p.textRect.x + p.textRect.w);
    }

    // Left box: mirror — the icon must not be drawn over the text.
    TEST(IconTextPlacement, Regression_MiscPanelLeftBox_IconDoesNotCoverText)
    {
        const IntRect rect{ .x = 0, .y = 0, .w = 400, .h = 24 };
        const auto p = ComputeIconTextPlacement(rect, kTextW, kTextH, kIcon,
                                                IconSide::Left, kGap, TextAlign::End);

        EXPECT_GE(p.textRect.x, p.iconRect.x + p.iconRect.w);
    }

    // ---- Icon-only (no text) ---------------------------------------------
    // An icon-only block (textWidth == 0) centres the icon in the rect: no gap
    // for absent text, and no text-height term pulling it up out of the rect.

    TEST(IconTextPlacement, IconOnly_FillsRect_NoOffset)
    {
        // 24px icon, 24x24 rect → fills it exactly, origin at (0,0).
        const IntRect rect{ .x = 0, .y = 0, .w = 24, .h = 24 };
        const auto p = ComputeIconTextPlacement(rect, /*textW*/0, /*textH*/0, kIcon,
                                                IconSide::Left, kGap,
                                                TextAlign::Center, TextAlign::Center);
        EXPECT_EQ(p.iconRect.x, 0);
        EXPECT_EQ(p.iconRect.y, 0);   // not pulled up out of the rect
        EXPECT_EQ(p.iconRect.w, kIcon.width);
        EXPECT_EQ(p.iconRect.h, kIcon.height);
    }

    TEST(IconTextPlacement, IconOnly_SmallerThanRect_CentredBothAxes)
    {
        const IconMeasurement icon16{ 16, 16 };
        const IntRect rect{ .x = 10, .y = 100, .w = 24, .h = 20 };
        const auto p = ComputeIconTextPlacement(rect, 0, 0, icon16,
                                                IconSide::Left, /*gap*/6,
                                                TextAlign::Center, TextAlign::Center);
        EXPECT_EQ(p.iconRect.x, 10 + (24 - 16) / 2); // gap does not shift it
        EXPECT_EQ(p.iconRect.y, 100 + (20 - 16) / 2); // centred, not pushed up
    }

    // Cross-axis values follow skribidi: Left/Right mean top/bottom vertically,
    // Top/Bottom mean left/right horizontally.
    TEST(IconTextPlacement, CrossAxisAlignValuesMatchSkribidi)
    {
        EXPECT_EQ(VerticalAlignOffset(TextAlign::Left,  100, 20), 0);
        EXPECT_EQ(VerticalAlignOffset(TextAlign::Right, 100, 20), 80);

        const IntRect rect{ .x = 0, .y = 0, .w = 100, .h = 100 };
        const IconMeasurement icon{ 20, 20 };
        EXPECT_EQ(ComputeIconTextPlacement(rect, 0, 0, icon, IconSide::Left, 0, TextAlign::Top).iconRect.x, 0);
        EXPECT_EQ(ComputeIconTextPlacement(rect, 0, 0, icon, IconSide::Left, 0, TextAlign::Bottom).iconRect.x, 80);
        EXPECT_EQ(ComputeIconTextPlacement(rect, kTextW, kTextH, kIcon, IconSide::Left, kGap, TextAlign::Bottom).iconRect.x,
                  rect.w - (kTextW + kGap + kIcon.width));
    }
}
