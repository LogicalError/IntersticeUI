#pragma once

#include <gui_core/elements/gui_element.h>
#include <gui_core/layout/gui_constraints.h>

#include <algorithm>
#include <span>
#include <vector>

namespace interstice
{
	struct int2;
	class ElementStorage;
	class OSWindowManager;
    class ITextSystem;

    struct LayoutHelpers
    {
        static int2 ClampScrollOffsetToContent(int2 childOffset, const ElementRect& parent);

        /// A leaf's MARGIN BOX at its natural (max-content) size. `minContentWidth` gets the
        /// margin box width flex shrink stops at: its text's min-content when the text
        /// overflows visibly, else 0.
        static int2 CalculateLeafSize(ITextSystem& textSystem, const Element &element,
                                      const ElementRect &elementRect, int32_t& minContentWidth);
        
        static int32_t RecalculateContentHeight(ITextSystem& textSystem,
                                                const Element &element, const ElementRect &elementRect);

        static void ApplyHorizontalStyleSize(ElementRect& elementRect, const int32_t contentWidth);
        static void ApplyVerticalStyleSize(ElementRect& elementRect, const int32_t contentHeight);
        static void ApplyHorizontalStyleOffset(ElementRect& elementRect);
        static void ApplyVerticalStyleOffset(ElementRect& elementRect);

        static void ApplyHorizontalPositionConstraints(OSWindowManager& osWindowManager, ElementStorage& storage, ElementRect& elementRect, const ElementRef& elementRef);
        static void ApplyVerticalPositionConstraints(OSWindowManager& osWindowManager, ElementStorage& storage, ElementRect& elementRect, const ElementRef& elementRef);

        template <int axis> requires(axis == 0 || axis == 1)
        static inline int32_t CalcLayoutAdd(std::span<const ElementRect> childRects, int32_t& count)
        {
            int32_t size = 0;
            count = 0;
            for (const ElementRect& childElem : childRects)
            {
                if (!childElem.constraints.IsInFlowAxis(axis))
                    continue;
                size += childElem.rect.size[axis];
                count++;
            }
            return size;
        }

        /// The smallest margin-box size an item may be flexed to on `axis`: its CSS min (border
        /// box) plus its margin, and never below its own chrome (margin + border + padding,
        /// which would give a negative border box) or, horizontally, its minContentWidth.
        template <int axis> requires(axis == 0 || axis == 1)
        static inline int32_t FlexMinSize(const ElementRect& c)
        {
            const SizeConstraintAxis& size = (axis == 0) ? c.constraints.width : c.constraints.height;
            if (!c.layoutStyle) return std::max<int32_t>(0, size.min);
            const LayoutStyle& s   = *c.layoutStyle;
            const int32_t margin   = (axis == 0) ? s.margin.Horizontal() : s.margin.Vertical();
            const int32_t chrome   = (axis == 0) ? s.GetMinWidth()       : s.GetMinHeight();
            const int32_t content  = (axis == 0) ? c.minContentWidth     : 0;
            return std::max({ chrome, int32_t(size.min + margin), content });
        }

        /// The largest margin-box size an item may be flexed to on `axis`: its CSS max plus its
        /// margin (kUnbounded when unset), never below FlexMinSize.
        template <int axis> requires(axis == 0 || axis == 1)
        static inline int32_t FlexMaxSize(const ElementRect& c)
        {
            const SizeConstraintAxis& size = (axis == 0) ? c.constraints.width : c.constraints.height;
            if (size.max == SizeConstraintAxis::kUnbounded) return SizeConstraintAxis::kUnbounded;
            const int32_t margin = !c.layoutStyle ? 0
                : (axis == 0) ? c.layoutStyle->margin.Horizontal() : c.layoutStyle->margin.Vertical();
            return std::max<int32_t>(FlexMinSize<axis>(c), size.max + margin);
        }

        /// Sets each in-flow item's size on `axis` to its flex-basis, the starting main size
        /// before free space is shared out. An explicit size on the axis wins over the basis.
        template <int axis> requires(axis == 0 || axis == 1)
        static inline void ApplyFlexBasis(std::span<ElementRect> items)
        {
            for (ElementRect& c : items)
            {
                if (!c.constraints.IsInFlowAxis(axis) || !c.layoutStyle) continue;
                if (c.constraints.HasFixedAxis(axis)) continue;
                const int16_t basis = c.layoutStyle->layoutData.flex.flexBasis;
                if (basis >= 0)
                    c.rect.size[axis] = basis;
            }
        }

        /// Resolves flexible lengths on `axis` (CSS "resolve flexible lengths"):
        /// positive `freeSpace` grows the in-flow items by flex-grow, negative
        /// shrinks them by flex-shrink x size. Each item stays within
        /// [FlexMinSize, FlexMaxSize]; an item that hits a limit is frozen there
        /// and what it couldn't take is shared among the rest. Items with an
        /// explicit size on `axis` don't shrink. Shares use floor-boundary
        /// rounding so they sum exactly. Returns the total size change applied.
        template <int axis> requires(axis == 0 || axis == 1)
        static inline int32_t ResolveFlexibleLengths(std::span<ElementRect> items, int32_t freeSpace)
        {
            if (freeSpace == 0) return 0;
            const bool grow = freeSpace > 0;

            struct Item { uint64_t weight; int32_t base; int32_t target; bool active; };
            static thread_local std::vector<Item> state;
            state.assign(items.size(), Item{ 0, 0, 0, false });
            for (size_t i = 0; i < items.size(); ++i)
            {
                const ElementRect& c = items[i];
                if (!c.constraints.IsInFlowAxis(axis)) continue;
                const int32_t size = c.rect.size[axis];
                uint64_t weight = 0;
                if (grow)
                    weight = c.constraints.GetFlexGrow(c.layoutStyle);
                else if (!c.constraints.HasFixedAxis(axis))
                    weight = uint64_t(c.constraints.GetFlexShrink(c.layoutStyle)) * uint64_t(std::max<int32_t>(size, 0));
                if (weight == 0) continue;
                state[i] = Item{ weight, size, size, true };
            }

            int64_t remaining = freeSpace;
            int64_t applied   = 0;
            for (size_t pass = 0; pass <= items.size() && remaining != 0; ++pass)
            {
                uint64_t totalWeight = 0;
                for (const Item& it : state) if (it.active) totalWeight += it.weight;
                if (totalWeight == 0) break;

                uint64_t cumWeight = 0;
                int64_t  prevBound = 0;
                int64_t  violation = 0;   // sum of (clamped - target): > 0 = min violations, < 0 = max
                for (size_t i = 0; i < items.size(); ++i)
                {
                    Item& it = state[i];
                    if (!it.active) continue;
                    cumWeight += it.weight;
                    const int64_t bound = remaining * int64_t(cumWeight) / int64_t(totalWeight);
                    it.target = int32_t(it.base + (bound - prevBound));
                    prevBound = bound;
                    violation += std::clamp(it.target, FlexMinSize<axis>(items[i]), FlexMaxSize<axis>(items[i])) - it.target;
                }

                if (violation == 0)
                {
                    for (size_t i = 0; i < items.size(); ++i)
                        if (state[i].active) items[i].rect.size[axis] = state[i].target;
                    applied += remaining;
                    break;
                }

                // Freeze the items on the side of the violation at their limit; the
                // rest share what is left in the next pass.
                for (size_t i = 0; i < items.size(); ++i)
                {
                    Item& it = state[i];
                    if (!it.active) continue;
                    const int32_t clamped = std::clamp(it.target, FlexMinSize<axis>(items[i]), FlexMaxSize<axis>(items[i]));
                    if ((violation > 0 && clamped > it.target) || (violation < 0 && clamped < it.target))
                    {
                        items[i].rect.size[axis] = clamped;
                        remaining -= (clamped - it.base);
                        applied   += (clamped - it.base);
                        it.active  = false;
                    }
                }
            }
            return int32_t(applied);
        }

        template <int axis> requires(axis == 0 || axis == 1)
        static inline int32_t CalcLayoutMax(std::span<const ElementRect> childRects)
        {
            int32_t size = 0;
            for (const ElementRect& childElem : childRects)
            {
                if (!childElem.constraints.IsInFlowAxis(axis))
                    continue;
                size = std::max(size, static_cast<int32_t>(childElem.rect.size[axis]));
            }
            return size;
        }

        template <int axis> requires(axis == 0 || axis == 1)
        static inline void Place(std::span<ElementRect> childRects, int32_t start, int32_t spacing)
        {
            for (ElementRect& childElem : childRects)
            {
                if (!childElem.constraints.IsInFlowAxis(axis))
                    continue;
                childElem.rect.position[axis] = start;
                start += childElem.rect.size[axis] + spacing;
            }
        }

        /// Content size along `axis` of an element with a fixed size on that axis.
        /// The fixed size is the border box, so only padding and border come off
        /// (the margin lies outside it).
        template <int axis> requires(axis == 0 || axis == 1)
        static inline int32_t FixedContentSize(const ElementRect& rect)
        {
            const LayoutStyle& s = *rect.layoutStyle;
            if constexpr (axis == 0) return rect.constraints.width.value  - s.padding.Horizontal() - s.borderWidths.Horizontal();
            else                     return rect.constraints.height.value - s.padding.Vertical()   - s.borderWidths.Vertical();
        }

        template <int axis> requires(axis == 0 || axis == 1)
        static inline void ReversePlace(std::span<ElementRect> childRects, int32_t start, int32_t spacing)
        {
            for (ElementRect& childElem : childRects)
            {
                if (!childElem.constraints.IsInFlowAxis(axis))
                    continue;
                start -= childElem.rect.size[axis];
                childElem.rect.position[axis] = start;
                start -= spacing;
            }
        }
                
        /// Positions the children on the main `axis` per `justify`. `totalContentSize`
        /// includes the gaps between in-flow children. `reverse`
        /// places children from the far edge (row-/column-reverse), whose main
        /// start is that edge - so flex-end packs toward the near one.
        template <int axis, bool reverse> requires(axis == 0 || axis == 1)
        static void JustifyChildren(std::span<ElementRect> childRects, 
                                          JustifyContent justify, int16_t gap, const IntRect &parentContentRect,
                                          int totalContentSize, int totalChildrenInFlow)
        {
            const int32_t freeSpace = parentContentRect.size[axis] - totalContentSize;

            // Without free space the distributing modes fall back as in CSS:
            // space-between to flex-start, space-around / space-evenly to center.
            if (freeSpace < 0)
            {
                if (justify == JustifyContent::SpaceBetween) justify = JustifyContent::FlexStart;
                else if (justify == JustifyContent::SpaceAround || justify == JustifyContent::SpaceEvenly) justify = JustifyContent::Center;
            }

            // Offset of the first child from the main start, and the space between children.
            int32_t offset = 0, spacing = gap;
            switch (justify)
            {                
                default:
                case JustifyContent::FlexStart:     break;
                case JustifyContent::FlexEnd:       offset = freeSpace; break;
                case JustifyContent::Center:        offset = freeSpace / 2; break;
                case JustifyContent::SpaceBetween:
                    if (totalChildrenInFlow > 1) spacing += freeSpace / (totalChildrenInFlow - 1);
                    break;
                case JustifyContent::SpaceAround:
                    if (totalChildrenInFlow > 0) { spacing += freeSpace / totalChildrenInFlow; offset = (spacing - gap) / 2; }
                    break;
                case JustifyContent::SpaceEvenly:
                    offset   = freeSpace / (totalChildrenInFlow + 1);
                    spacing += offset;
                    break;
            }

            if constexpr (reverse)
                ReversePlace<axis>(childRects, parentContentRect.Max(axis) - offset, spacing);
            else
                Place<axis>(childRects, parentContentRect.position[axis] + offset, spacing);
        }
        
        template <int axis> requires(axis == 0 || axis == 1)
        static void Align(AlignItems align, const IntRect &parentContentRect, ElementRect& childRect)
        {
            // Per-item align-self overrides the container's align-items
            if (childRect.layoutStyle && childRect.layoutStyle->alignSelf != AlignItems::Auto)
                align = childRect.layoutStyle->alignSelf;

            switch (align)
            {
                default:
                case AlignItems::FlexStart: { childRect.rect.position[axis] = parentContentRect.position[axis]; break; }
                case AlignItems::FlexEnd:   { childRect.rect.position[axis] = (parentContentRect.position[axis] + parentContentRect.size[axis]) - childRect.rect.size[axis]; break; }
                case AlignItems::Center:    { childRect.rect.position[axis] = parentContentRect.position[axis] + (parentContentRect.size[axis] - childRect.rect.size[axis]) / 2; break; }
                case AlignItems::Stretch:
                {
                    childRect.rect.position[axis] = parentContentRect.position[axis];
                    // Do not override a fixed size constraint on the cross axis.
                    if (!childRect.constraints.HasFixedAxis(axis))
                    {
                        // Never below the child's min, so an overflow:auto parent
                        // still sees content wider than itself.
                        int32_t s = parentContentRect.size[axis];
                        s = (axis == 0) ? childRect.constraints.ClampWidth(s)
                                        : childRect.constraints.ClampHeight(s);
                        childRect.rect.size[axis] = s;
                    }
                    break;
                }
            }
        }

    };
} // namespace interstice