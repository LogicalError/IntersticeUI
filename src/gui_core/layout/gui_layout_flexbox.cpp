#include <gui_core/gui_base_types.h>

#include <gui_core/elements/gui_element_storage.h>

#include <gui_core/diagnostics/gui_profiler.h>

#include <gui_core/layout/gui_layout_flexbox.h>
#include <gui_core/layout/gui_layout_helpers.h>

#include <algorithm>
#include <assert.h>
#include <vector>

namespace GUI
{
    namespace
    {
        // ---- flex-column keep-with-next groups ----
        //
        // In flex-column, items chained via keep-with-next form one horizontal "row" sharing
        // a Y slot, so `Label + Field` works without a nested flex-row. Without keep-with-next
        // every group is a single item and this is plain flex-column.
        template <typename Visitor>
        static inline void VisitColumnGroups(std::span<const ElementRect> childRects,
                                             int16_t gap,
                                             Visitor&& visitor)
        {
            if (childRects.empty()) return;

            auto axisSize = [](const ElementRect& c, int axis) -> int32_t {
                int32_t s = c.rect.size[axis];
                if (s == 0)
                {
                    if (axis == 0 && c.constraints.HasFixedWidth())  s = c.constraints.width.value;
                    if (axis == 1 && c.constraints.HasFixedHeight()) s = c.constraints.height.value;
                }
                return s;
            };

            size_t  groupStart     = 0;
            size_t  groupCount     = 0; // includes out-of-flow (so span subspan stays contiguous)
            int32_t groupMainSize  = 0; // sum of in-flow widths + inner gaps
            int32_t groupCrossSize = 0; // max in-flow height
            int32_t inFlowCount    = 0;
            bool    prevKeep       = false;

            for (size_t i = 0; i < childRects.size(); ++i)
            {
                const ElementRect& c = childRects[i];
                if (!c.constraints.IsInFlow())
                {
                    groupCount++;
                    continue;
                }

                // New row unless the previous item keeps with this one; break-before always starts one.
                bool breakBefore = c.constraints.GetBreakBefore(c.layoutStyle);
                if (inFlowCount > 0 && (!prevKeep || breakBefore))
                {
                    visitor(groupStart, groupCount, groupMainSize, groupCrossSize, inFlowCount);
                    groupStart     = i;
                    groupCount     = 0;
                    groupMainSize  = 0;
                    groupCrossSize = 0;
                    inFlowCount    = 0;
                }

                int32_t w = axisSize(c, 0);
                int32_t h = axisSize(c, 1);
                groupMainSize  = (inFlowCount == 0) ? w : (groupMainSize + gap + w);
                groupCrossSize = std::max(groupCrossSize, h);
                groupCount++;
                inFlowCount++;
                prevKeep = c.constraints.GetKeepWithNext(c.layoutStyle);
            }
            if (inFlowCount > 0)
                visitor(groupStart, groupCount, groupMainSize, groupCrossSize, inFlowCount);
        }

        template <typename Visitor>
        static inline void VisitLines(std::span<const ElementRect> childRects,
                                      int32_t   maxMainSize,
                                      int16_t   gap,
                                      bool      wrapEnabled,
                                      int       mainAxis,
                                      int       crossAxis,
                                      Visitor&& visitor)
        {
            if (childRects.empty())
                return;

            if (!wrapEnabled || maxMainSize <= 0)
            {
                int32_t totalChildrenInFlow = 0;
                int32_t totalMain = (mainAxis == 0)
                    ? LayoutHelpers::CalcLayoutAdd<0>(childRects, totalChildrenInFlow)
                    : LayoutHelpers::CalcLayoutAdd<1>(childRects, totalChildrenInFlow);

                if (totalChildrenInFlow == 0)
                    return;

                int32_t totalGap = static_cast<int32_t>(totalChildrenInFlow - 1) * gap;

                int32_t maxCross = (crossAxis == 0)
                    ? LayoutHelpers::CalcLayoutMax<0>(childRects)
                    : LayoutHelpers::CalcLayoutMax<1>(childRects);

                visitor(size_t{0}, childRects.size(), totalMain + totalGap, maxCross, totalChildrenInFlow);
                return;
            }

            size_t lineStart = 0;
            size_t lineCount = 0;
            int32_t lineMain = 0;
            int32_t lineCross = 0;

            auto getAxisSize = [](const ElementRect& child, int axis) -> int32_t
            {
                int32_t size = child.rect.size[axis];
                if (size == 0)
                {
                    if (axis == 0 && child.constraints.HasFixedWidth())
                        size = child.constraints.width.value;
                    else if (axis == 1 && child.constraints.HasFixedHeight())
                        size = child.constraints.height.value;
                }
                return size;
            };

            // Returns the combined main-axis size of element i plus all following elements
            // glued to it via keepWithNext, including gaps between them.
            auto computeGroupSize = [&](size_t startIdx) -> int32_t
            {
                int32_t total = 0;
                int count = 0;
                for (size_t j = startIdx; j < childRects.size(); ++j)
                {
                    const ElementRect& elem = childRects[j];
                    if (!elem.constraints.IsInFlow())
                        continue;
                    int32_t sz = getAxisSize(elem, mainAxis);
                    total = (count == 0) ? sz : total + gap + sz;
                    count++;
                    if (!elem.constraints.GetKeepWithNext(elem.layoutStyle))
                        break;
                }
                return total;
            };

            int totalChildrenInFlow = 0;
            bool prevBreakAfter   = false;
            bool prevKeepWithNext = false;
            for (size_t i = 0; i < childRects.size(); ++i)
            {
                const ElementRect& childElem = childRects[i];
                if (!childElem.constraints.IsInFlow())
                {
                    lineCount++;
                    // Out-of-flow elements are transparent to line breaking.
                    continue;
                }

                int32_t childMain = getAxisSize(childElem, mainAxis);
                int32_t childCross = getAxisSize(childElem, crossAxis);

                // A keep-group that doesn't fit breaks before the group, not inside it.
                bool hasKeepWithNext = childElem.constraints.GetKeepWithNext(childElem.layoutStyle);
                int32_t checkMain = hasKeepWithNext ? computeGroupSize(i) : childMain;

                bool explicitBreak = prevBreakAfter ||
                                     childElem.constraints.GetBreakBefore(childElem.layoutStyle);
                bool sizeBreak     = !prevKeepWithNext && (lineMain + gap + checkMain) > maxMainSize;
                // Out-of-flow children take no room: only in-flow items make a line wrappable.
                bool shouldWrap    = totalChildrenInFlow > 0 && (explicitBreak || sizeBreak);

                if (shouldWrap)
                {
                    if (totalChildrenInFlow > 0)
                    {
                        visitor(lineStart, lineCount, lineMain, lineCross, totalChildrenInFlow);
                    }
                    lineStart = i;
                    lineCount = 0;
                    lineMain = 0;
                    lineCross = 0;
                    totalChildrenInFlow = 0;
                }
                if (totalChildrenInFlow == 0)
                    lineMain = childMain;
                else
                    lineMain += gap + childMain;
                totalChildrenInFlow++;

                lineCross = std::max(lineCross, childCross);
                lineCount++;

                prevBreakAfter   = childElem.constraints.GetBreakAfter(childElem.layoutStyle);
                prevKeepWithNext = childElem.constraints.GetKeepWithNext(childElem.layoutStyle);
            }

            if (lineCount > 0 && totalChildrenInFlow > 0)
            {
                visitor(lineStart, lineCount, lineMain, lineCross, totalChildrenInFlow);
            }
        }
    }

    int32_t LayoutFlexbox::HorizontalSizing(const ElementRect&     parentRect,
                                            std::span<ElementRect> childRects)
    {
        if (childRects.size() == 0)
            return 0;

        //PROFILE_SCOPE("LayoutFlexbox::HorizontalSizing");
        const FlexProperties& flexLayout = parentRect.layoutStyle->layoutData.flex;
        switch (flexLayout.direction)
        {
            default: return 0;

            // Rows: content width = sum of children + gaps
            case FlexDirection::Row:
            case FlexDirection::RowReverse:
            {
                int32_t totalChildrenInFlow = 0;
                int32_t totalSize = LayoutHelpers::CalcLayoutAdd<0>(childRects, totalChildrenInFlow);
                int32_t totalGaps = (totalChildrenInFlow < 1) ? 0 : ((totalChildrenInFlow - 1) * flexLayout.gap);

                // Fixed-width parent: distribute the remaining space.
                if (parentRect.constraints.HasFixedWidth())
                {
                    int32_t parentContentWidth = LayoutHelpers::FixedContentSize<0>(parentRect);
                    int32_t used = totalSize + totalGaps;
                    int32_t remaining = parentContentWidth - used;
                    // Grow into spare room; shrink only when the row can't wrap instead.
                    if (remaining > 0 || flexLayout.wrap == FlexWrap::NoWrap)
                        LayoutHelpers::ResolveFlexibleLengths<0>(childRects, remaining);
                }

                return totalSize + totalGaps;
            }

            // Columns: content width = widest keep-with-next row (see VisitColumnGroups)
            case FlexDirection::Column:
            case FlexDirection::ColumnReverse:
            {
                if (flexLayout.wrap != FlexWrap::NoWrap && parentRect.constraints.HasFixedHeight())
                {
                    const IntRect& parentContentRect = parentRect.innerRect;
                    int32_t totalCross = 0, lineCount = 0;
                    VisitLines(childRects, parentContentRect.size[1], flexLayout.gap, true, 1, 0,
                               [&](size_t, size_t, int32_t, int32_t crossSize, int32_t)
                               {
                                   totalCross += crossSize;
                                   lineCount++;
                               });
                    if (lineCount > 1)
                        totalCross += (lineCount - 1) * flexLayout.gap;
                    return totalCross;
                }

                int32_t maxRow = 0;
                VisitColumnGroups(childRects, flexLayout.gap,
                    [&](size_t, size_t, int32_t rowWidth, int32_t, int32_t)
                    {
                        maxRow = std::max(maxRow, rowWidth);
                    });
                return maxRow;
            }
        }
    }

    int32_t LayoutFlexbox::VerticalSizing(const ElementRect&           parentRect,
                                          std::span<ElementRect> childRects)
    {
        if (childRects.empty())
            return 0;
        
        //PROFILE_SCOPE("LayoutFlexbox::VerticalSizing");
        const FlexProperties &flexLayout = parentRect.layoutStyle->layoutData.flex;
        switch (flexLayout.direction)
        {
            default: return 0;

            // Rows: content height = tallest child
            case FlexDirection::Row:
            case FlexDirection::RowReverse:
            {
                if (flexLayout.wrap != FlexWrap::NoWrap)
                {
                    const IntRect& parentContentRect = parentRect.innerRect;
                    int32_t totalCross = 0, lineCount = 0;
                    VisitLines(childRects, parentContentRect.size[0], flexLayout.gap, true, 0, 1,
                               [&](size_t, size_t, int32_t, int32_t crossSize, int32_t)
                               {
                                   totalCross += crossSize;
                                   lineCount++;
                               });
                    if (lineCount > 1)
                        totalCross += (lineCount - 1) * flexLayout.gap;
                    return totalCross;
                }


                // Cross-axis stretch happens in VerticalPositioning.
                return LayoutHelpers::CalcLayoutMax<1>(childRects);
            }

            // Columns: content height = sum of keep-with-next row heights + gaps.
            // Flex distribution happens top-down in VerticalPositioning, where the
            // parent's final size is known.
            case FlexDirection::Column:
            case FlexDirection::ColumnReverse:
            {
                int32_t total    = 0;
                int32_t numRows  = 0;
                VisitColumnGroups(childRects, flexLayout.gap,
                    [&](size_t, size_t, int32_t, int32_t rowHeight, int32_t)
                    {
                        total += rowHeight;
                        numRows++;
                    });
                int32_t gaps = (numRows < 2) ? 0 : ((numRows - 1) * flexLayout.gap);
                return total + gaps;
            }
        }
    }

    void LayoutFlexbox::HorizontalPositioning(const ElementRect&     parentRect,
                                              std::span<ElementRect> childRects)
    {
        if (childRects.empty())
            return;

        //PROFILE_SCOPE("LayoutFlexbox::HorizontalPositioning");
        const LayoutStyle* layoutStyle = parentRect.layoutStyle;
        const FlexProperties& flexLayout = layoutStyle->layoutData.flex;
        const IntRect& parentContentRect = parentRect.innerRect;

        if (flexLayout.direction == FlexDirection::Row || flexLayout.direction == FlexDirection::RowReverse)
        {
            // Items start from their flex-basis; lines break on that size (as in CSS).
            LayoutHelpers::ApplyFlexBasis<0>(childRects);

            uint16_t lineIdx = 0;
            VisitLines(childRects, parentContentRect.size[0], flexLayout.gap, flexLayout.wrap != FlexWrap::NoWrap, 0, 1,
                       [&](size_t start, size_t count, int32_t mainSize, int32_t, int32_t totalChildrenInFlow)
                       {
                           auto lineSpan = childRects.subspan(start, count);

                           // VerticalPositioning reuses these line breaks.
                           for (ElementRect& c : lineSpan)
                               c.wrapLineIndex = lineIdx;
                           lineIdx++;

                           // mainSize includes the gaps.
                           mainSize += LayoutHelpers::ResolveFlexibleLengths<0>(lineSpan, parentContentRect.size[0] - mainSize);

                           if (flexLayout.direction == FlexDirection::Row)
                           {
                               LayoutHelpers::JustifyChildren<0, false>(lineSpan, flexLayout.justifyContent, flexLayout.gap,
                                                                        parentContentRect, mainSize, totalChildrenInFlow);
                           } else
                           {
                               LayoutHelpers::JustifyChildren<0, true>(lineSpan, flexLayout.justifyContent, flexLayout.gap,
                                                                       parentContentRect, mainSize, totalChildrenInFlow);
                           }
                       });
        } else // Column or ColumnReverse
        {
            if (flexLayout.wrap == FlexWrap::NoWrap)
            {
                // Single-item rows use cross-axis alignment; multi-item keep-with-next
                // rows flex and justify horizontally like a flex-row line.
                VisitColumnGroups(childRects, flexLayout.gap,
                    [&](size_t start, size_t count, int32_t rowWidth, int32_t, int32_t inFlow)
                    {
                        auto rowSpan = childRects.subspan(start, count);
                        if (inFlow <= 1)
                        {
                            for (ElementRect& c : rowSpan)
                            {
                                if (!c.constraints.IsInFlow()) continue;
                                LayoutHelpers::Align<0>(flexLayout.alignItems, parentContentRect, c);
                            }
                            return;
                        }

                        rowWidth += LayoutHelpers::ResolveFlexibleLengths<0>(rowSpan, parentContentRect.size[0] - rowWidth);
                        if (flexLayout.direction == FlexDirection::Column)
                        {
                            LayoutHelpers::JustifyChildren<0, false>(rowSpan, flexLayout.justifyContent, flexLayout.gap,
                                                                     parentContentRect, rowWidth, static_cast<int>(inFlow));
                        } else
                        {
                            LayoutHelpers::JustifyChildren<0, true>(rowSpan, flexLayout.justifyContent, flexLayout.gap,
                                                                    parentContentRect, rowWidth, static_cast<int>(inFlow));
                        }
                    });
                return;
            }

            bool wrapReverse = (flexLayout.wrap == FlexWrap::WrapReverse);
            int32_t crossCursor = wrapReverse
                ? parentContentRect.position[0] + parentContentRect.size[0]
                : parentContentRect.position[0];

            int32_t availableHeight = parentContentRect.size[1];
            if (availableHeight <= 0 && parentRect.constraints.HasFixedHeight())
                availableHeight = LayoutHelpers::FixedContentSize<1>(parentRect);

            VisitLines(childRects, availableHeight, flexLayout.gap, true, 1, 0,
                       [&](size_t start, size_t count, int32_t, int32_t crossSize, int32_t)
                       {
                           int32_t lineStart = 0;
                           if (wrapReverse)
                           {
                               crossCursor -= crossSize;
                               lineStart = crossCursor;
                               crossCursor -= flexLayout.gap;
                           } else
                           {
                               lineStart = crossCursor;
                               crossCursor += crossSize + flexLayout.gap;
                           }

                           auto lineSpan = childRects.subspan(start, count);
                           IntRect lineRect = parentContentRect;
                           lineRect.position[0] = lineStart;
                           lineRect.size[0] = crossSize;
                           for (ElementRect& childElem : lineSpan)
                           {
                               if (!childElem.constraints.IsInFlow())
                                   continue;
                               LayoutHelpers::Align<0>(flexLayout.alignItems, lineRect, childElem);
                           }
                       });
        }
    }


    IntRect LayoutFlexbox::VerticalPositioning(const ElementRect&     parentRect, 
                                               const IntRect&         clipRect,
                                               std::span<ElementRect> childRects)
    {
        if (childRects.empty()) 
        {
            return {};
        }

        //PROFILE_SCOPE("LayoutFlexbox::VerticalPositioning");
        const LayoutStyle* layoutStyle = parentRect.layoutStyle;
        const FlexProperties& flexLayout = layoutStyle->layoutData.flex;
        const IntRect& parentContentRect = parentRect.innerRect;

        if (flexLayout.direction == FlexDirection::Column || flexLayout.direction == FlexDirection::ColumnReverse)
        {
            int32_t availableHeight = parentContentRect.size[1];
            if (availableHeight <= 0 && parentRect.constraints.HasFixedHeight())
                availableHeight = LayoutHelpers::FixedContentSize<1>(parentRect);

            // Main-axis flex distribution; the parent's final height is known here (top-down).
            //
            // Only NoWrap columns distribute on the main axis; wrap-columns would
            // need it per wrap line (#64).
            // A column with any keep-with-next child is a stack of label/field rows ("form
            // mode") and skips main-axis distribution: flex-grow there usually comes from
            // row-oriented widget styles and would make every row tall.
            bool anyKeepWithNext = false;
            for (const ElementRect& c : childRects)
            {
                if (!c.constraints.IsInFlow()) continue;
                if (c.constraints.GetKeepWithNext(c.layoutStyle)) { anyKeepWithNext = true; break; }
            }

            // A zero height is real (squeezed by siblings): children still flex down to it.
            if (flexLayout.wrap == FlexWrap::NoWrap && availableHeight >= 0 && !anyKeepWithNext)
            {
                // Basis first, so basis:0 + grow:1 fills the remaining space however tall the
                // content is (needed for scrolling lists).
                LayoutHelpers::ApplyFlexBasis<1>(childRects);

                // Free space is measured against stacked row heights, not individual children.
                int32_t totalRowSize = 0;
                int32_t numRows      = 0;
                VisitColumnGroups(childRects, flexLayout.gap,
                    [&](size_t, size_t, int32_t, int32_t rowH, int32_t)
                    {
                        totalRowSize += rowH;
                        numRows++;
                    });
                int32_t totalGaps = (numRows < 2) ? 0 : ((numRows - 1) * flexLayout.gap);
                int32_t used = totalRowSize + totalGaps;
                LayoutHelpers::ResolveFlexibleLengths<1>(childRects, availableHeight - used);
            }

            IntRect contentRect {};
            if (flexLayout.wrap == FlexWrap::NoWrap)
            {
                // Stack rows vertically; a row is as tall as its tallest (post-grow) child.
                const bool reverse = (flexLayout.direction == FlexDirection::ColumnReverse);
                const int32_t parentMin = parentContentRect.position[1];
                const int32_t parentMax = parentContentRect.position[1] + parentContentRect.size[1];
                int32_t cursor = reverse ? parentMax : parentMin;
                VisitColumnGroups(childRects, flexLayout.gap,
                    [&](size_t start, size_t count, int32_t, int32_t, int32_t)
                    {
                        auto rowSpan = childRects.subspan(start, count);
                        int32_t rowHeight = 0;
                        for (const ElementRect& c : rowSpan)
                        {
                            if (!c.constraints.IsInFlow()) continue;
                            rowHeight = std::max<int32_t>(rowHeight, c.rect.size[1]);
                        }
                        int32_t rowY;
                        if (reverse)
                        {
                            cursor -= rowHeight;
                            rowY = cursor;
                            cursor -= flexLayout.gap;
                        }
                        else
                        {
                            rowY = cursor;
                            cursor += rowHeight + flexLayout.gap;
                        }
                        // Align within the row like a flex-row line, so stretch gives a label
                        // its row's full height (the label-group resize dragger relies on it).
                        IntRect rowRect = parentContentRect;
                        rowRect.position[1] = rowY;
                        rowRect.size[1]     = rowHeight;
                        for (ElementRect& c : rowSpan)
                        {
                            if (!c.constraints.IsInFlow()) continue;
                            LayoutHelpers::Align<1>(flexLayout.alignItems, rowRect, c);
                            c.clipRect = clipRect;
                            contentRect = IntRect::Union(contentRect, c.rect - parentContentRect.position);
                        }
                    });
                return contentRect;
            }

            VisitLines(childRects, availableHeight, flexLayout.gap, flexLayout.wrap != FlexWrap::NoWrap, 1, 0,
                       [&](size_t start, size_t count, int32_t mainSize, int32_t, int32_t totalChildrenInFlow)
                       {
                           auto lineSpan = childRects.subspan(start, count);
                           if (flexLayout.direction == FlexDirection::Column)
                           {
                               LayoutHelpers::JustifyChildren<1, false>(lineSpan, flexLayout.justifyContent, flexLayout.gap,
                                                                        parentContentRect, mainSize, totalChildrenInFlow);
                           } else
                           {
                               LayoutHelpers::JustifyChildren<1, true>(lineSpan, flexLayout.justifyContent, flexLayout.gap,
                                                                       parentContentRect, mainSize, totalChildrenInFlow);
                           }

                           for (ElementRect& childElem : lineSpan)
                           {
                               if (!childElem.constraints.IsInFlow())
                                   continue;
                               childElem.clipRect = clipRect;
                               contentRect = IntRect::Union(contentRect, childElem.rect - parentContentRect.position);
                           }
                       });
            return contentRect;
        } else // Row or RowReverse
        {
            if (flexLayout.wrap == FlexWrap::NoWrap)
            {
                IntRect contentRect {};
                for (ElementRect& childElem : childRects)
                {
                    if (!childElem.constraints.IsInFlow())
                        continue;
                    childElem.clipRect = clipRect;
                    LayoutHelpers::Align<1>(flexLayout.alignItems, parentContentRect, childElem);
                    contentRect = IntRect::Union(contentRect, childElem.rect - parentContentRect.position);
                }
                return contentRect;
            }

            // Lines come from wrapLineIndex: re-running VisitLines on the flexed widths
            // could break differently than HorizontalPositioning did.
            struct LineInfo { size_t start, count; int32_t crossSize; };
            std::vector<LineInfo> lines;
            lines.reserve(4);
            {
                uint16_t prevLineIdx = UINT16_MAX;
                size_t lineStart = 0;
                size_t lineCount = 0;
                int32_t lineCross = 0;

                for (size_t i = 0; i < childRects.size(); ++i)
                {
                    const ElementRect& child = childRects[i];
                    uint16_t idx = child.wrapLineIndex;

                    if (idx != prevLineIdx && lineCount > 0)
                    {
                        lines.push_back({ lineStart, lineCount, lineCross });
                        lineStart = i;
                        lineCount = 0;
                        lineCross = 0;
                    }

                    if (child.constraints.IsInFlow())
                        lineCross = std::max(lineCross, child.rect.size[1]);
                    lineCount++;
                    prevLineIdx = idx;
                }
                if (lineCount > 0)
                    lines.push_back({ lineStart, lineCount, lineCross });
            }

            // align-content: distributes cross-axis space between wrap lines.
            const int32_t lineCount = static_cast<int32_t>(lines.size());
            int32_t totalLineCross = 0;
            for (const auto& l : lines) totalLineCross += l.crossSize;
            const int32_t linesGap  = std::max<int32_t>(0, (lineCount - 1)) * flexLayout.gap;
            const int32_t availCross = parentContentRect.size[1];
            int32_t freeCross = availCross - totalLineCross - linesGap;
            if (freeCross < 0) freeCross = 0;

            int32_t startOffset = 0;
            int32_t betweenGap  = flexLayout.gap;
            if (lineCount > 0)
            {
                switch (flexLayout.alignContent)
                {
                    case AlignContent::FlexStart:
                        break;
                    case AlignContent::FlexEnd:
                        startOffset = freeCross;
                        break;
                    case AlignContent::Center:
                        startOffset = freeCross / 2;
                        break;
                    case AlignContent::Stretch:
                    {
                        // Split free space evenly across lines.
                        if (freeCross > 0)
                        {
                            int32_t perLine  = freeCross / lineCount;
                            int32_t leftover = freeCross - perLine * lineCount;
                            for (int32_t i = 0; i < lineCount; ++i)
                            {
                                lines[i].crossSize += perLine;
                                if (leftover > 0) { lines[i].crossSize += 1; leftover--; }
                            }
                        }
                        break;
                    }
                    case AlignContent::SpaceBetween:
                        if (lineCount > 1)
                            betweenGap = flexLayout.gap + freeCross / (lineCount - 1);
                        break;
                    case AlignContent::SpaceAround:
                    {
                        int32_t slot = (lineCount > 0) ? (freeCross / lineCount) : 0;
                        startOffset = slot / 2;
                        betweenGap  = flexLayout.gap + slot;
                        break;
                    }
                    case AlignContent::SpaceEvenly:
                    {
                        int32_t slot = (lineCount > 0) ? (freeCross / (lineCount + 1)) : 0;
                        startOffset = slot;
                        betweenGap  = flexLayout.gap + slot;
                        break;
                    }
                }
            }

            const bool wrapReverse = (flexLayout.wrap == FlexWrap::WrapReverse);
            int32_t crossCursor = wrapReverse
                ? parentContentRect.position[1] + parentContentRect.size[1] - startOffset
                : parentContentRect.position[1] + startOffset;

            IntRect contentRect {};
            for (const auto& line : lines)
            {
                int32_t lineStart = 0;
                if (wrapReverse)
                {
                    crossCursor -= line.crossSize;
                    lineStart = crossCursor;
                    crossCursor -= betweenGap;
                }
                else
                {
                    lineStart = crossCursor;
                    crossCursor += line.crossSize + betweenGap;
                }

                auto lineSpan = childRects.subspan(line.start, line.count);
                IntRect lineRect = parentContentRect;
                lineRect.position[1] = lineStart;
                lineRect.size[1]     = line.crossSize;
                for (ElementRect& childElem : lineSpan)
                {
                    if (!childElem.constraints.IsInFlow())
                        continue;
                    childElem.clipRect = clipRect;
                    LayoutHelpers::Align<1>(flexLayout.alignItems, lineRect, childElem);
                    contentRect = IntRect::Union(contentRect, childElem.rect - parentContentRect.position);
                }
            }
            return contentRect;
        }
    }
}

