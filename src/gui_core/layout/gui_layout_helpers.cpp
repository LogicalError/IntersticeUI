#include <gui_core/layout/gui_layout_helpers.h>

#include <gui_core/text/gui_text_system.h>

#include <gui_core/diagnostics/gui_profiler.h>

#include <gui_core/gui_base_types.h>
#include <gui_core/elements/gui_element_storage.h>
#include <gui_core/windows/gui_os_window_manager.h>

#include <algorithm>
#include <assert.h>

namespace tachyon
{
    int2 LayoutHelpers::ClampScrollOffsetToContent(int2 childOffset, const ElementRect& parentRect)
    {
        const IntRect& visibleBounds = parentRect.innerRect;
        const IntRect& contentBounds = parentRect.contentBounds;

        const int32_t scrollRangeX = std::max(0, contentBounds.w - visibleBounds.w);
        const int32_t scrollRangeY = std::max(0, contentBounds.h - visibleBounds.h);
        const int32_t scrollPosX = contentBounds.x - childOffset.x;
        const int32_t scrollPosY = contentBounds.y - childOffset.y;
        const int32_t clampedScrollPosX = std::clamp(scrollPosX, contentBounds.x, contentBounds.x + scrollRangeX);
        const int32_t clampedScrollPosY = std::clamp(scrollPosY, contentBounds.y, contentBounds.y + scrollRangeY);
        return int2 { contentBounds.x - clampedScrollPosX, contentBounds.y - clampedScrollPosY };
    }

    // Natural MARGIN BOX size for a leaf element (assigned to rect.size during sizing).
    int2 LayoutHelpers::CalculateLeafSize(ITextSystem& textSystem,
                                          const Element& element,
                                          const ElementRect& elementRect,
                                          int32_t& minContentWidth)
    {
        //TACHYON_PROFILE_SCOPE("LayoutHelpers::CalculateLeafSize");
        minContentWidth = 0;
        // Border box without content
        int2 minimumStyleSize;
        minimumStyleSize.x = elementRect.layoutStyle->padding.Horizontal() + elementRect.layoutStyle->borderWidths.Horizontal();
        minimumStyleSize.y = elementRect.layoutStyle->padding.Vertical()   + elementRect.layoutStyle->borderWidths.Vertical();

        // In DoNotMeasureText controls with overflow-x:auto (e.g. textboxes) the text is user
        // data and doesn't drive min width; its height still counts.
        const bool isControlWithAutoOverflow = (element.flags & ElementFlags::DoNotMeasureText) == ElementFlags::DoNotMeasureText
                                              && elementRect.layoutStyle->overflowX == Overflow::Auto;
        const bool textDrivesWidth = !isControlWithAutoOverflow;
        int2    borderBoxMinSize = minimumStyleSize;
        int32_t narrowerBy       = 0; // how much narrower the text gets at min-content
        if (element.iconID == IconID::Invalid)
        {
            if (element.textID != TextID::Invalid)
            {
                const auto measurement =
                    textSystem.GetTextMeasurement(element.textID);
                if (textDrivesWidth)
                {
                    borderBoxMinSize.x += measurement.maxWidth;
                    narrowerBy = measurement.maxWidth - measurement.minWidth;
                }
                borderBoxMinSize.y += measurement.minHeight;
            }
        } else 
        if (element.textID == TextID::Invalid)
        {
            const auto measurement =
                textSystem.GetIconMeasurement(element.iconID);
            borderBoxMinSize.x += measurement.width;
            borderBoxMinSize.y += measurement.height;
        } else
        {
            const auto textMeasurement = textSystem.GetTextMeasurement(element.textID);
            const auto iconMeasurement = textSystem.GetIconMeasurement(element.iconID);
            switch (elementRect.layoutStyle->iconSide)
            {
                case IconSide::None: break;
                case IconSide::Left:
                case IconSide::Right:
                {
                    borderBoxMinSize.x += iconMeasurement.width;
                    borderBoxMinSize.x += elementRect.layoutStyle->iconTextGap;
                    if (textDrivesWidth)
                    {
                        borderBoxMinSize.x += textMeasurement.maxWidth;
                        narrowerBy = textMeasurement.maxWidth - textMeasurement.minWidth;
                    }
                    borderBoxMinSize.y += std::max(iconMeasurement.height, textMeasurement.minHeight);
                    break;
                }
                case IconSide::Top:
                case IconSide::Bottom:
                {
                    borderBoxMinSize.x += textDrivesWidth
                        ? std::max(iconMeasurement.width, textMeasurement.maxWidth)
                        : iconMeasurement.width;
                    if (textDrivesWidth)
                        narrowerBy = std::max(iconMeasurement.width, textMeasurement.maxWidth)
                                   - std::max(iconMeasurement.width, textMeasurement.minWidth);
                    borderBoxMinSize.y += iconMeasurement.height;
                    borderBoxMinSize.y += elementRect.layoutStyle->iconTextGap;
                    borderBoxMinSize.y += textMeasurement.minHeight;
                    break;
                }
            }
        }
        
        // Size constraints are border-box sizes.
        int2 clampedBorderBox = elementRect.constraints.ApplySize(borderBoxMinSize);
        
        // rect.w/h is the margin box during sizing
        int2 marginBoxSize;
        marginBoxSize.x = clampedBorderBox.x + elementRect.layoutStyle->margin.Horizontal();
        marginBoxSize.y = clampedBorderBox.y + elementRect.layoutStyle->margin.Vertical();
        // As CSS's automatic minimum: text that overflows visibly keeps its min-content
        // width; clipped text (ellipsis) may shrink to nothing.
        if (textDrivesWidth && element.textID != TextID::Invalid &&
            elementRect.layoutStyle->overflowX == Overflow::Visible)
            minContentWidth = elementRect.constraints.ApplyWidth(borderBoxMinSize.x - narrowerBy)
                            + elementRect.layoutStyle->margin.Horizontal();
        return marginBoxSize;
    }

    // Margin box -> border box on X, at the end of HorizontalPositioningPass.
    void LayoutHelpers::ApplyHorizontalStyleOffset(ElementRect& elementRect)
    {
        //TACHYON_PROFILE_SCOPE("LayoutHelpers::ApplyHorizontalStyleOffset");
        elementRect.rect.x += elementRect.layoutStyle->margin.left;
        elementRect.rect.w -= elementRect.layoutStyle->margin.Horizontal();
        // A flex item shrunk to a zero margin box would otherwise go negative.
        if (elementRect.rect.w < 0) elementRect.rect.w = 0;
    }

    // Margin box -> border box on Y, at the end of VerticalPositioningPass.
    void LayoutHelpers::ApplyVerticalStyleOffset(ElementRect& elementRect)
    {
        elementRect.rect.y += elementRect.layoutStyle->margin.top;
        elementRect.rect.h -= elementRect.layoutStyle->margin.Vertical();
        if (elementRect.rect.h < 0) elementRect.rect.h = 0;
    }

    // Sets a container's rect.w to its MARGIN BOX width from its content width.
    void LayoutHelpers::ApplyHorizontalStyleSize(ElementRect& elementRect, const int32_t contentWidth)
    {
        //TACHYON_PROFILE_SCOPE("LayoutHelpers::ApplyHorizontalStyleSize");
        int32_t styleWidth = elementRect.layoutStyle->padding.Horizontal() + elementRect.layoutStyle->borderWidths.Horizontal();
        int32_t borderBoxWidth = elementRect.constraints.ApplyWidth(styleWidth + contentWidth);
        elementRect.rect.w = borderBoxWidth + elementRect.layoutStyle->margin.Horizontal();
    }

    // Sets a container's rect.h to its MARGIN BOX height from its content height.
    void LayoutHelpers::ApplyVerticalStyleSize(ElementRect& elementRect, const int32_t contentHeight)
    {
        //TACHYON_PROFILE_SCOPE("LayoutHelpers::ApplyVerticalStyleSize");
        int32_t styleHeight = elementRect.layoutStyle->padding.Vertical() + elementRect.layoutStyle->borderWidths.Vertical();
        int32_t borderBoxHeight = elementRect.constraints.ApplyHeight(styleHeight + contentHeight);
        elementRect.rect.h = borderBoxHeight + elementRect.layoutStyle->margin.Vertical();
    }

    int32_t LayoutHelpers::RecalculateContentHeight(ITextSystem& textSystem, 
                                                    const Element &element, 
                                                    const ElementRect &elementRect)
    {
        //TACHYON_PROFILE_SCOPE("LayoutHelpers::RecalculateContentHeight");

        int32_t availableWidth =
            elementRect.rect.w - elementRect.layoutStyle->GetMinWidth();

        bool horizontalIcon = elementRect.layoutStyle->iconSide == IconSide::Left ||
                              elementRect.layoutStyle->iconSide == IconSide::Right;

        int2 iconSize{0,0};
        if (element.iconID != IconID::Invalid)
        {
            iconSize = textSystem.GetIconMeasurement(element.iconID).Size();

            if (horizontalIcon)
            {
                availableWidth -=
                    (iconSize.x + elementRect.layoutStyle->iconTextGap);
            }
        }

        // minHeight of the result is the wrapped height.
        TextWrapContext wrapContext = 
        {
            .maxWidth = std::max(0, availableWidth)
        };

        const auto wrappedMeasurement =
            textSystem.MeasureText(element.textID, wrapContext);

        int32_t contentHeight = wrappedMeasurement.minHeight;

        if (element.iconID != IconID::Invalid)
        {
            contentHeight = horizontalIcon ? std::max(contentHeight, iconSize.y)
                : contentHeight + elementRect.layoutStyle->iconTextGap + iconSize.y;
        }

        // Content height only; excludes padding/borders.
        return contentHeight;
    }


    void LayoutHelpers::ApplyHorizontalPositionConstraints(OSWindowManager& osWindowManager, ElementStorage& storage, ElementRect& elementRect, const ElementRef& elementRef)
    {
        //TACHYON_PROFILE_SCOPE("LayoutHelpers::ApplyHorizontalPositionConstraints");
        IntRect targetRect = {};
        LayoutOverride positionConstraints = elementRect.constraints;
        if (positionConstraints.x.type != PositionType::Relative)
        {
            ElementRef anchorRef = ElementRef::Invalid();
            if (positionConstraints.x.type == PositionType::Anchored)
            {
                anchorRef = storage.FindElementRefByID(positionConstraints.x.anchorTarget);
            }
            // Windows are positioned in screen space (from SyncWindowsBeforeLayout), so they skip
            // the parent fallback that would add the parent window's position.
            const bool isWindow = (elementRect.flags & ElementFlags::IsWindow) != ElementFlags::None;
            if (!anchorRef.IsValid() && !isWindow)
                anchorRef = storage.GetParentRef(elementRef);
            if (anchorRef.IsValid())
            {
                const auto& anchorTargetRect = storage.GetElementRectUnsafe(anchorRef);
                targetRect = anchorTargetRect.rect;
            } else
            {
                targetRect = osWindowManager.GetMonitorArea(elementRect.monitorIndex);
            }
        }

        positionConstraints.ApplyHorizontal(elementRect.rect, targetRect);

        // Anchored elements overflowing the work area try the flipped placement, then clamp.
        if (positionConstraints.x.type == PositionType::Anchored &&
            positionConstraints.x.autoFlip != AutoFlipMode::None &&
            elementRect.monitorIndex >= 0)
        {
            const IntRect workArea = osWindowManager.GetMonitor(elementRect.monitorIndex).workArea;
            const int32_t overflowLeft  = workArea.x - elementRect.rect.x;
            const int32_t overflowRight = elementRect.rect.Max(0) - workArea.Max(0);

            if (overflowLeft > 0 || overflowRight > 0)
            {
                IntRect flippedRect = elementRect.rect;
                flippedRect.x = 0;
                LayoutOverride flipped = positionConstraints;
                flipped.x.anchorPlacement = FlipAxis(flipped.x.anchorPlacement);
                flipped.ApplyHorizontal(flippedRect, targetRect);

                const int32_t flippedOverflowLeft  = std::max(0, workArea.x - flippedRect.x);
                const int32_t flippedOverflowRight  = std::max(0, flippedRect.Max(0) - workArea.Max(0));
                if (flippedOverflowLeft + flippedOverflowRight < std::max(0, overflowLeft) + std::max(0, overflowRight))
                {
                    elementRect.rect.x = flippedRect.x;
                    positionConstraints.x.anchorPlacement = flipped.x.anchorPlacement;
                }
            }

            if (positionConstraints.x.autoFlip == AutoFlipMode::FlipAvoidTarget)
            {
                const auto horzPlacement = positionConstraints.x.anchorPlacement;
                if (horzPlacement == AnchorPlacementAxis::OutsideMax)
                {
                    // Right of the target
                    const int32_t available = workArea.Max(0) - targetRect.Max(0);
                    elementRect.rect.x = targetRect.Max(0) + positionConstraints.x.near;
                    elementRect.rect.w = std::min(elementRect.rect.w, std::max(0, available));
                }
                else if (horzPlacement == AnchorPlacementAxis::OutsideMin)
                {
                    // Left of the target
                    const int32_t available = targetRect.x - workArea.x;
                    elementRect.rect.w = std::min(elementRect.rect.w, std::max(0, available));
                    elementRect.rect.x = targetRect.x - elementRect.rect.w + positionConstraints.x.near;
                }
                else
                {
                    // Inside/Center: clamp to workArea
                    if (elementRect.rect.x < workArea.x)
                        elementRect.rect.x = workArea.x;
                    if (elementRect.rect.Max(0) > workArea.Max(0))
                        elementRect.rect.w = workArea.Max(0) - elementRect.rect.x;
                }
            }
            else
            {
                // Flip mode: clamp position only
                if (elementRect.rect.x < workArea.x)
                    elementRect.rect.x = workArea.x;
                if (elementRect.rect.Max(0) > workArea.Max(0))
                    elementRect.rect.x = workArea.Max(0) - elementRect.rect.w;
            }
        }
    }

    void LayoutHelpers::ApplyVerticalPositionConstraints(OSWindowManager& osWindowManager, ElementStorage& storage, ElementRect& elementRect, const ElementRef& elementRef)
    {
        //TACHYON_PROFILE_SCOPE("LayoutHelpers::ApplyVerticalPositionConstraints");
        IntRect targetRect = {};
        LayoutOverride positionConstraints = elementRect.constraints;
        if (positionConstraints.y.type != PositionType::Relative)
        {
            ElementRef anchorRef = ElementRef::Invalid();
            if (positionConstraints.y.type == PositionType::Anchored)
            {
                anchorRef = storage.FindElementRefByID(positionConstraints.y.anchorTarget);
            }
            // Windows are positioned in screen space (from SyncWindowsBeforeLayout), so they skip
            // the parent fallback that would add the parent window's position.
            const bool isWindow = (elementRect.flags & ElementFlags::IsWindow) != ElementFlags::None;
            if (!anchorRef.IsValid() && !isWindow)
                anchorRef = storage.GetParentRef(elementRef);
            if (anchorRef.IsValid())
            {
                const auto& anchorTargetRect = storage.GetElementRectUnsafe(anchorRef);
                targetRect = anchorTargetRect.rect;
            } else
            {
                targetRect = osWindowManager.GetMonitorArea(elementRect.monitorIndex);
            }
        }

        positionConstraints.ApplyVertical(elementRect.rect, targetRect);

        // Anchored elements overflowing the work area try the flipped placement, then clamp.
        if (positionConstraints.y.type == PositionType::Anchored &&
            positionConstraints.y.autoFlip != AutoFlipMode::None &&
            elementRect.monitorIndex >= 0)
        {
            const IntRect workArea = osWindowManager.GetMonitor(elementRect.monitorIndex).workArea;
            const int32_t overflowTop    = workArea.y - elementRect.rect.y;
            const int32_t overflowBottom = elementRect.rect.Max(1) - workArea.Max(1);

            if (overflowTop > 0 || overflowBottom > 0)
            {
                IntRect flippedRect = elementRect.rect;
                flippedRect.y = 0;
                LayoutOverride flipped = positionConstraints;
                flipped.y.anchorPlacement = FlipAxis(flipped.y.anchorPlacement);
                flipped.ApplyVertical(flippedRect, targetRect);

                const int32_t flippedOverflowTop    = std::max(0, workArea.y - flippedRect.y);
                const int32_t flippedOverflowBottom  = std::max(0, flippedRect.Max(1) - workArea.Max(1));
                if (flippedOverflowTop + flippedOverflowBottom < std::max(0, overflowTop) + std::max(0, overflowBottom))
                {
                    elementRect.rect.y = flippedRect.y;
                    positionConstraints.y.anchorPlacement = flipped.y.anchorPlacement;
                }
            }

            if (positionConstraints.y.autoFlip == AutoFlipMode::FlipAvoidTarget)
            {
                const auto vertPlacement = positionConstraints.y.anchorPlacement;
                const bool isOutside = (vertPlacement == AnchorPlacementAxis::OutsideMax || vertPlacement == AnchorPlacementAxis::OutsideMin);

                // Too tall for either side above/below?
                const int32_t spaceBelow = workArea.Max(1) - targetRect.Max(1);
                const int32_t spaceAbove = targetRect.y - workArea.y;
                const bool wouldOverlap = isOutside && elementRect.rect.h > std::max(spaceBelow, spaceAbove);

                if (wouldOverlap)
                {
                    // Place it beside the target at full workArea height: the roomier side
                    // first, then the other; if neither fits, clamp and accept the overlap.
                    const int32_t spaceRight   = workArea.Max(0) - targetRect.Max(0);
                    const int32_t spaceLeft    = targetRect.x - workArea.x;
                    const int32_t width        = elementRect.rect.w;
                    const bool    preferRight  = (spaceRight >= spaceLeft);
                    const bool    fitsRight    = (width <= spaceRight);
                    const bool    fitsLeft     = (width <= spaceLeft);

                    // Write it into the X anchor too, so VerticalPositioningPass re-resolving
                    // X keeps the side placement.
                    auto setSidePlacementX = [&](bool placeRight)
                    {
                        elementRect.rect.x = placeRight
                            ? targetRect.Max(0)
                            : targetRect.x - width;
                        elementRect.constraints.x.anchorPlacement = placeRight
                            ? AnchorPlacementAxis::OutsideMax
                            : AnchorPlacementAxis::OutsideMin;
                        elementRect.constraints.x.near = 0;
                    };

                    if (preferRight ? fitsRight : fitsLeft)
                    {
                        setSidePlacementX(preferRight);
                    }
                    else if (preferRight ? fitsLeft : fitsRight)
                    {
                        setSidePlacementX(!preferRight);
                    }
                    else
                    {
                        elementRect.rect.x = workArea.x;
                    }

                    elementRect.rect.y = workArea.y;
                    elementRect.rect.h = std::min(elementRect.rect.h, workArea.h);
                }
                else if (isOutside)
                {
                    // Fits on one side: clamp position and height to avoid overlap
                    if (vertPlacement == AnchorPlacementAxis::OutsideMax)
                    {
                        elementRect.rect.y = targetRect.Max(1) + positionConstraints.y.near;
                        elementRect.rect.h = std::min(elementRect.rect.h, std::max(0, spaceBelow));
                    }
                    else
                    {
                        elementRect.rect.h = std::min(elementRect.rect.h, std::max(0, spaceAbove));
                        elementRect.rect.y = targetRect.y - elementRect.rect.h + positionConstraints.y.near;
                    }
                }
                else
                {
                    // Inside/Center: clamp to workArea
                    if (elementRect.rect.y < workArea.y)
                        elementRect.rect.y = workArea.y;
                    if (elementRect.rect.Max(1) > workArea.Max(1))
                        elementRect.rect.h = workArea.Max(1) - elementRect.rect.y;
                }
            }
            else
            {
                // Flip mode: clamp position only
                if (elementRect.rect.y < workArea.y)
                    elementRect.rect.y = workArea.y;
                if (elementRect.rect.Max(1) > workArea.Max(1))
                    elementRect.rect.y = workArea.Max(1) - elementRect.rect.h;
            }
        }
    }

} // namespace tachyon
