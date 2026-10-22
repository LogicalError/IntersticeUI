#pragma once

#include <gui_core/gui_base_types.h>

#include <gui_core/style/gui_style.h>
#include <gui_core/style/gui_style_text.h>

#include <gui_core/text/gui_text_system.h>

#include <algorithm>

namespace GUI
{
    // Offset of content inside a box along one axis. Values meant for the other axis
    // map as skribidi does: Top/Bottom act as left/right, Left/Right as top/bottom.
    inline int32_t HorizontalAlignOffset(TextAlign horizontalAlignment, int32_t boxWidth, int32_t contentWidth)
    {
        switch (horizontalAlignment)
        {
            case TextAlign::Center: return (boxWidth - contentWidth) / 2;
            case TextAlign::End:
            case TextAlign::Right:
            case TextAlign::Bottom: return boxWidth - contentWidth;
            default:                return 0;
        }
    }

    inline int32_t VerticalAlignOffset(TextAlign verticalAlignment, int32_t boxHeight, int32_t contentHeight)
    {
        switch (verticalAlignment)
        {
            case TextAlign::Center: return (boxHeight - contentHeight) / 2;
            case TextAlign::End:
            case TextAlign::Bottom:
            case TextAlign::Right:  return boxHeight - contentHeight;
            default:                return 0;
        }
    }

    // Placement of a text + icon pair inside `rect` (tests/icon_text_placement_test.cpp):
    //   * The icon always sits adjacent to its label, never at a stretched rect's edge.
    //   * Left / Right: icon and text share one centre line. For Left, textRect is
    //     shifted past the icon so text-align: end can't render across it; for
    //     Right, textRect keeps rect.x and shrinks to reserve the icon area.
    //   * Top / Bottom: icon is centred on the [icon, text] column. Top hugs rect's
    //     top; Bottom sits iconTextGap below the text, not at rect's bottom.
    //   * Left / Right icons taller than the text may extend outside rect; the
    //     caller sizes rect via measurement when it wants them contained.
    struct IconTextPlacement
    {
        IntRect textRect;
        IntRect iconRect;
    };

    inline IconTextPlacement ComputeIconTextPlacement(
        const IntRect&         rect,
        int32_t                textWidth,
        int32_t                textHeight,
        const IconMeasurement& iconMeasurement,
        IconSide               iconSide,
        int32_t                iconTextGap,
        TextAlign              horizontalAlignment,
        TextAlign              verticalAlignment = TextAlign::Start)
    {
        IconTextPlacement out = {
            .textRect = rect,
            .iconRect = IntRect{ 0, 0, 0, 0 },
        };

        // Icon only: align it within `rect` directly. The text+icon model below
        // centres the icon on the text line, which has no height here.
        if (textWidth <= 0)
        {
            const int32_t iconX = rect.x + HorizontalAlignOffset(horizontalAlignment, rect.w, iconMeasurement.width);
            const int32_t iconY = rect.y + VerticalAlignOffset(verticalAlignment, rect.h, iconMeasurement.height);
            out.iconRect = IntRect{ iconX, iconY, iconMeasurement.width, iconMeasurement.height };
            return out;
        }

        // Bounding box of the packed [icon][text] block, for text-align and
        // -interstice-vertical-text-align.
        int32_t combinedWidth  = textWidth;
        int32_t combinedHeight = textHeight;
        switch (iconSide)
        {
            case IconSide::Left:
            case IconSide::Right:
                combinedWidth  = textWidth + iconTextGap + iconMeasurement.width;
                combinedHeight = std::max(textHeight, iconMeasurement.height);
                break;
            case IconSide::Top:
            case IconSide::Bottom:
                combinedWidth  = std::max(textWidth, iconMeasurement.width);
                combinedHeight = textHeight + iconTextGap + iconMeasurement.height;
                break;
        }

        const int32_t textAlignmentOffsetX = HorizontalAlignOffset(horizontalAlignment, rect.w, combinedWidth);

        // Block vertical offset — shifts iconRect.y AND textRect.y by the
        // same amount so the icon/text pair tracks its alignment together.
        const int32_t blockY = rect.y + VerticalAlignOffset(verticalAlignment, rect.h, combinedHeight);

        switch (iconSide)
        {
            case IconSide::Left:
            {
                const int32_t combinedStartX = rect.x + textAlignmentOffsetX;
                out.iconRect = IntRect{
                    .x = combinedStartX,
                    .y = blockY + (combinedHeight - iconMeasurement.height) / 2,
                    .w = iconMeasurement.width,
                    .h = iconMeasurement.height
                };
                out.textRect.x = combinedStartX + iconMeasurement.width + iconTextGap;
                out.textRect.w = (rect.x + rect.w) - out.textRect.x;
                out.textRect.y = blockY + (combinedHeight - textHeight) / 2;
                break;
            }
            case IconSide::Right:
            {
                const int32_t combinedStartX = rect.x + textAlignmentOffsetX;
                out.iconRect = IntRect{
                    .x = combinedStartX + textWidth + iconTextGap,
                    .y = blockY + (combinedHeight - iconMeasurement.height) / 2,
                    .w = iconMeasurement.width,
                    .h = iconMeasurement.height
                };
                out.textRect.x = rect.x;
                out.textRect.w = rect.w - iconTextGap - iconMeasurement.width;
                out.textRect.y = blockY + (combinedHeight - textHeight) / 2;
                break;
            }
            case IconSide::Top:
            {
                const int32_t combinedStartX = rect.x + textAlignmentOffsetX;
                const int32_t iconCenterX    = combinedStartX + combinedWidth / 2;
                out.iconRect = IntRect{
                    .x = iconCenterX - iconMeasurement.width / 2,
                    .y = blockY,
                    .w = iconMeasurement.width,
                    .h = iconMeasurement.height
                };
                out.textRect.x = rect.x;
                out.textRect.w = rect.w;
                out.textRect.y = blockY + iconMeasurement.height + iconTextGap;
                out.textRect.h = rect.h - iconMeasurement.height - iconTextGap;
                break;
            }
            case IconSide::Bottom:
            {
                const int32_t combinedStartX = rect.x + textAlignmentOffsetX;
                const int32_t iconCenterX    = combinedStartX + combinedWidth / 2;
                out.textRect.x = rect.x;
                out.textRect.w = rect.w;
                out.textRect.y = blockY;
                out.textRect.h = textHeight - iconMeasurement.height - iconTextGap;
                out.iconRect = IntRect{
                    .x = iconCenterX - iconMeasurement.width / 2,
                    .y = blockY + textHeight + iconTextGap,
                    .w = iconMeasurement.width,
                    .h = iconMeasurement.height
                };
                break;
            }
        }

        (void)combinedHeight;
        return out;
    }

    // Companion helper: when the icon is on the left/right, the layout's
    // max-width must exclude the icon + gap so text-align within the layout
    // doesn't right-align all the way across the icon area. Top/Bottom icons
    // don't steal horizontal space, so layoutMaxWidth == rect.w there.
    inline int32_t IconAwareLayoutMaxWidth(
        int32_t                rectWidth,
        const IconMeasurement& iconMeasurement,
        IconSide               iconSide,
        int32_t                iconTextGap,
        bool                   hasIcon)
    {
        if (!hasIcon) return rectWidth;
        if (iconSide == IconSide::Left || iconSide == IconSide::Right)
            return std::max(0, rectWidth - iconTextGap - iconMeasurement.width);
        return rectWidth;
    }
}
