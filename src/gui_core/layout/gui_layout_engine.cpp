#include <gui_core/layout/gui_layout_engine.h>
#include <gui_core/layout/gui_layout_flexbox.h>
#include <gui_core/layout/gui_layout_grid.h>
#include <gui_core/layout/gui_layout_helpers.h>

#include <gui_core/diagnostics/gui_profiler.h>

#include <gui_core/elements/gui_element_storage.h>

#include <gui_core/text/gui_text_system.h>

#include <gui_core/state/gui_scroll_management.h>

#include <algorithm>
#include <assert.h>
#include <cmath>
#include <limits>

namespace tachyon
{
    // Descendant X is final after horizontal positioning, so an X change made during vertical
    // positioning (e.g. FlipAvoidTarget placing a menu beside its trigger) shifts the subtree.
    static void ShiftDescendantsX(ElementStorage& storage, const ElementRef& parentRef, int32_t deltaX)
    {
        auto range = storage.GetChildRange(parentRef);
        if (range.Empty()) return;
        auto childRects = storage.GetChildRectSpanUnsafe(parentRef.depth + 1, range);
        for (uint32_t i = 0; i < static_cast<uint32_t>(childRects.size()); i++)
        {
            childRects[i].rect.x += deltaX;
            childRects[i].innerRect.x += deltaX;
            ShiftDescendantsX(storage, ElementRef{ parentRef.depth + 1, range.begin + i }, deltaX);
        }
    }

    void LayoutEngine::Layout(OSWindowManager& osWindowManager, ElementStorage &storage)
    {
        if (storage.Empty())
            return;

        // See gui_layout_engine.h. The first VerticalSizingPass gives wrap-column containers
        // a rough height for HorizontalPositioning; the second uses final widths.

        TACHYON_PROFILE_SCOPE("Layout");

        {
            //TACHYON_PROFILE_SCOPE("HorizontalSizingPass");
            HorizontalSizingPass(storage);
        }

        {
            //TACHYON_PROFILE_SCOPE("VerticalSizingPass");
            VerticalSizingPass(storage);
        }

        {
            //TACHYON_PROFILE_SCOPE("HorizontalPositioningPass");
            HorizontalPositioningPass(osWindowManager, storage);
        }

        {
            //TACHYON_PROFILE_SCOPE("VerticalSizingPass");
            VerticalSizingPass(storage);
        }

        {
            //TACHYON_PROFILE_SCOPE("VerticalPositioningPass");
            VerticalPositioningPass(osWindowManager, storage);
        }
    }

    // Bottom-up: rect.w = MARGIN BOX width. Leaves from content, containers from children.
    void LayoutEngine::HorizontalSizingPass(ElementStorage &storage)
    {
        for (auto &cursor : storage.BottomUpDepthScanRange())
        {
            ElementRect &parentRect = cursor.CurrentElementRect(storage);

            if (cursor.HasChildren())
            {
                int32_t contentWidth = 0;
                auto childRects = cursor.GetChildRects(storage);
                switch (parentRect.display)
                {
                    default: assert(false && "Unsupported display type"); break;
                    case Display::Flex: contentWidth = LayoutFlexbox::HorizontalSizing(parentRect, childRects); break;
                    case Display::Grid: contentWidth = LayoutGrid   ::HorizontalSizing(parentRect, childRects); break;
                    case Display::None: break;
                }

                if (parentRect.constraints.HasFixedWidth())
                {
                    contentWidth = parentRect.constraints.width.value;
                }

                LayoutHelpers::ApplyHorizontalStyleSize(parentRect,
                                                        contentWidth);
            } else 
            // leaf element
            {
                int32_t minContentWidth = 0;
                parentRect.rect.size = // both axes
                    LayoutHelpers::CalculateLeafSize(
                        m_textSystem, cursor.CurrentElement(storage),
                        parentRect, minContentWidth);
                parentRect.minContentWidth = int16_t(std::min<int32_t>(minContentWidth, parentRect.rect.w));
            }
        }
    }

    void LayoutEngine::VerticalSizingPass(ElementStorage &storage)
    {
        for (auto &cursor : storage.BottomUpDepthScanRange())
        {
            ElementRect &parentRect = cursor.CurrentElementRect(storage);

            if (cursor.HasChildren())
            {
                int32_t contentHeight = 0;
                auto childRects = cursor.GetChildRects(storage);
                switch (parentRect.display)
                {
                    default: assert(false && "Unsupported display type"); break;
                    case Display::Flex: contentHeight = LayoutFlexbox::VerticalSizing(parentRect, childRects); break;
                    case Display::Grid: contentHeight = LayoutGrid   ::VerticalSizing(parentRect, childRects); break;
                    case Display::None: break;
                }

                if (parentRect.constraints.HasFixedHeight())
                {
                    contentHeight = parentRect.constraints.height.value;
                }

                LayoutHelpers::ApplyVerticalStyleSize(parentRect,
                                                      contentHeight);
            } else
            // leaf element
            {
                // Leaf heights come from HorizontalSizingPass.
            }
        }
    }   

    // Top-down: X positions and flex distribution.
    // Entry: rect.w = margin box. Exit: rect.x/w = border box, rect.y/h still margin box.
    void LayoutEngine::HorizontalPositioningPass(OSWindowManager& osWindowManager, ElementStorage &storage)
    {
        for (auto &cursor : storage.TopDownDepthScanRange())
        {
            ElementRect& parentRect = cursor.CurrentElementRect(storage);
            LayoutHelpers::ApplyHorizontalPositionConstraints(osWindowManager, storage, parentRect, cursor.CurrentRef());

            assert(parentRect.layoutStyle != nullptr);
            parentRect.innerRect = parentRect.layoutStyle->GetContentRect(parentRect.rect);
            if (cursor.parentDepth == 0) parentRect.clipRect = parentRect.innerRect;

            if (cursor.HasChildren())
            {
                auto childRects = cursor.GetChildRects(storage);
                switch (parentRect.display)
                {
				    default: assert(false && "Unsupported display type"); break;
                    case Display::Flex: LayoutFlexbox::HorizontalPositioning(parentRect, childRects); break;
                    case Display::Grid: LayoutGrid   ::HorizontalPositioning(parentRect, childRects); break;
                    case Display::None: break;
                }
            }

            LayoutHelpers::ApplyHorizontalStyleOffset(parentRect);
        }
    } 

    // Top-down: Y positions, scroll offsets, clip rects and contentBounds.
    // Entry: rect.x/w = border box, rect.y/h = margin box. Exit: rect = border box.
    void LayoutEngine::VerticalPositioningPass(OSWindowManager& osWindowManager, ElementStorage &storage)
    {
        for (auto &cursor : storage.TopDownDepthScanRange())
        {
            ElementRect& parentRect = cursor.CurrentElementRect(storage);
            const int32_t preVerticalX = parentRect.rect.x;
            {
                LayoutHelpers::ApplyVerticalPositionConstraints(osWindowManager, storage, parentRect, cursor.CurrentRef());
            }

            // Vertical constraints may move X (FlipAvoidTarget); descendants follow.
            const int32_t deltaX = parentRect.rect.x - preVerticalX;
            if (deltaX != 0)
            {
                ShiftDescendantsX(storage, cursor.CurrentRef(), deltaX);
            }

            // Fixed and Anchored ignore parent scroll (per axis). Must follow the position constraints.
            const PositionType ptX = parentRect.constraints.x.type;
            const PositionType ptY = parentRect.constraints.y.type;
            const bool scrollImmuneX = (ptX == PositionType::Fixed || ptX == PositionType::Anchored);
            const bool scrollImmuneY = (ptY == PositionType::Fixed || ptY == PositionType::Anchored);
            parentRect.rect.x += scrollImmuneX ? 0 : parentRect.scrollOffset.x;
            parentRect.rect.y += scrollImmuneY ? 0 : parentRect.scrollOffset.y;

            // Anchored X was resolved against the target's pre-scroll rect; re-resolve so it
            // tracks a target inside a scrolling container. The target is already final here.
            if (ptX == PositionType::Anchored)
            {
                const ElementRef anchorRef = storage.FindElementRefByID(parentRect.constraints.x.anchorTarget);
                if (anchorRef.IsValid())
                {
                    const int32_t prevX = parentRect.rect.x;
                    parentRect.constraints.x.Apply(parentRect.rect, /*axis*/0,
                                                   storage.GetElementRectUnsafe(anchorRef).rect);
                    parentRect.innerRect.x += parentRect.rect.x - prevX;
                }
            }

            assert(parentRect.layoutStyle != nullptr);

            // innerRect.x follows rect.x (FlipAvoidTarget shift and parent scroll).
            const int32_t totalShiftX = deltaX + (scrollImmuneX ? 0 : parentRect.scrollOffset.x);
            if (totalShiftX != 0)
                parentRect.innerRect.x += totalShiftX;

            {
                // innerRect y/h from the margin-box rect; x/w are already final.
                int16_t top    = parentRect.layoutStyle->margin.top    + parentRect.layoutStyle->borderWidths.top    + parentRect.layoutStyle->padding.top;
                int16_t bottom = parentRect.layoutStyle->margin.bottom + parentRect.layoutStyle->borderWidths.bottom + parentRect.layoutStyle->padding.bottom;
                parentRect.innerRect.y = parentRect.rect.y + top;
                parentRect.innerRect.h = std::max<int16_t>(0, static_cast<int16_t>(parentRect.rect.h - static_cast<int32_t>(top + bottom)));
            }
            if (cursor.parentDepth == 0 ||
                (parentRect.flags & ElementFlags::IsWindow) != ElementFlags::None)
                parentRect.clipRect = parentRect.innerRect;
            parentRect.contentBounds = IntRect{};

            // As in CSS, a non-visible overflow on either axis clips both; scrolling stays per axis.
            const Overflow overflowX = parentRect.layoutStyle->overflowX;
            const Overflow overflowY = parentRect.layoutStyle->overflowY;
            const bool     anyClip   = (overflowX != Overflow::Visible) || (overflowY != Overflow::Visible);
            const bool     isScrollableX = (overflowX == Overflow::Auto);
            const bool     isScrollableY = (overflowY == Overflow::Auto);
            const bool     isScrollable  = isScrollableX || isScrollableY;

            // Children's clipRect must be in the same space as their rect for hit-testing, so with a
            // contentTransform the parent's clip is inverse-transformed into the children's space.
            // Otherwise a child laid out outside innerRect but transformed into view can't be hit.
            auto inverseTransformChildClipRect = [&](IntRect cr) -> IntRect {
                if (parentRect.contentTransform.IsIdentity()) return cr;
                const float2 pivot{ (float)parentRect.innerRect.position.x,
                                    (float)parentRect.innerRect.position.y };
                const float3x2 inv = parentRect.contentTransform.Inverse();
                // AABB of the 4 corners: exact for translation, conservative for scale/rotate.
                const float2 corners[4] = {
                    { (float)cr.x,            (float)cr.y            },
                    { (float)(cr.x + cr.w),   (float)cr.y            },
                    { (float)cr.x,            (float)(cr.y + cr.h)   },
                    { (float)(cr.x + cr.w),   (float)(cr.y + cr.h)   },
                };
                float minX = std::numeric_limits<float>::max();
                float minY = std::numeric_limits<float>::max();
                float maxX = std::numeric_limits<float>::lowest();
                float maxY = std::numeric_limits<float>::lowest();
                for (const float2& c : corners)
                {
                    const float2 t = pivot + inv.TransformPoint(c - pivot);
                    if (t.x < minX) minX = t.x;
                    if (t.y < minY) minY = t.y;
                    if (t.x > maxX) maxX = t.x;
                    if (t.y > maxY) maxY = t.y;
                }
                return IntRect{
                    (int32_t)std::floor(minX),
                    (int32_t)std::floor(minY),
                    (int32_t)std::ceil(maxX - minX),
                    (int32_t)std::ceil(maxY - minY)
                };
            };

            if (cursor.HasChildren())
            {
                auto clipRect = anyClip
                    ? parentRect.innerRect.Intersection(parentRect.clipRect)
                    : parentRect.clipRect;
                clipRect = inverseTransformChildClipRect(clipRect);

                auto childRects = cursor.GetChildRects(storage);
                switch (parentRect.display)
                {
                    default: assert(false && "Unsupported display type"); break;
                    case Display::Flex: parentRect.contentBounds = LayoutFlexbox::VerticalPositioning(parentRect, clipRect, childRects); break;
                    case Display::Grid: parentRect.contentBounds = LayoutGrid   ::VerticalPositioning(parentRect, clipRect, childRects); break;
                    case Display::None: break;
                }

                // Absolute children (skipped by flex/grid) extend contentBounds, clipped to innerRect:
                // overlays such as column sizers use a huge height and must not inflate scroll extent.
                for (ElementRect& childRect : childRects)
                {
                    if (childRect.constraints.y.type == PositionType::Absolute)
                    {
                        childRect.constraints.ApplyVertical(childRect.rect, parentRect.rect);

                        // innerRect-local, like flexbox contentBounds
                        IntRect localRect = childRect.rect - parentRect.innerRect.position;

                        const int32_t visibleW = parentRect.innerRect.w;
                        const int32_t visibleH = parentRect.innerRect.h;
                        if (localRect.x + localRect.w > visibleW)
                            localRect.w = std::max(0, visibleW - localRect.x);
                        if (localRect.y + localRect.h > visibleH)
                            localRect.h = std::max(0, visibleH - localRect.y);

                        if (parentRect.contentBounds.IsEmpty())
                            parentRect.contentBounds = localRect;
                        else
                            parentRect.contentBounds = parentRect.contentBounds.Union(localRect);
                    }
                }

                auto scrollOffsetHandle = ScrollOffsetHandle::Invalid;
                if (isScrollable)
                {
                    Element& parentElement = cursor.CurrentElement(storage);
                    if (parentElement.id != ID::Invalid())
                    {
                        scrollOffsetHandle = m_scrollManagement.GetOrCreateScrollOffsetHandle(parentElement.id);
                    }
                }

                // Inherited scroll cascades on X only: children's X was placed in an earlier pass
                // (before any scroll shift), while their Y is placed in this pass against the
                // already-shifted innerRect. This element's own scroll applies on both axes.
                int2 childOffset{ parentRect.scrollOffset.x, 0 };
                if (scrollOffsetHandle != ScrollOffsetHandle::Invalid)
                {
                    // Scrollable: children are always clipped to innerRect.
                    clipRect = inverseTransformChildClipRect(
                        parentRect.innerRect.Intersection(parentRect.clipRect));
                    int2 ownOffset = m_scrollManagement.GetScrollOffset(scrollOffsetHandle);
                    // A stale offset must not move a non-scrolling axis.
                    if (!isScrollableX) ownOffset.x = 0;
                    if (!isScrollableY) ownOffset.y = 0;
                    if (parentRect.constraints.GetClampScroll(parentRect.layoutStyle))
                        ownOffset = LayoutHelpers::ClampScrollOffsetToContent(ownOffset, parentRect);

                    m_scrollManagement.SetScrollOffset(scrollOffsetHandle, ownOffset);
                    childOffset += ownOffset;
                }
                for (ElementRect& child : childRects)
                {
                    int2 childScroll = childOffset;

                    // Sticky: clamp the scroll the child applies to itself so it stays at or
                    // past parent innerRect + threshold.
                    if (child.constraints.x.type == PositionType::Sticky)
                    {
                        const int16_t thr = child.constraints.x.near != PositionConstraintAxis::kAuto
                            ? child.constraints.x.near : int16_t(0);
                        const int32_t minScrollX = (parentRect.innerRect.x + thr) - child.rect.x;
                        childScroll.x = std::max(childScroll.x, minScrollX);
                    }
                    if (child.constraints.y.type == PositionType::Sticky)
                    {
                        const int16_t thr = child.constraints.y.near != PositionConstraintAxis::kAuto
                            ? child.constraints.y.near : int16_t(0);
                        const int32_t minScrollY = (parentRect.innerRect.y + thr) - child.rect.y;
                        childScroll.y = std::max(childScroll.y, minScrollY);
                    }

                    // Windows reset their own clipRect in their iteration.
                    child.scrollOffset = childScroll;
                    child.clipRect = clipRect;
                }
            } else
            {
                // Leaf: contentBounds from the wrapped text.
                const Element& element = cursor.CurrentElement(storage);

                int32_t contentWidth = parentRect.innerRect.w;
                int32_t contentHeight = parentRect.innerRect.h;

                if (element.textID != TextID::Invalid)
                {
                    TextWrapContext wrapContext
                    {
                        .maxWidth = std::max(0, parentRect.innerRect.w)
                    };

                    const TextMeasurement wrappedMeasurement = m_textSystem.MeasureText(element.textID, wrapContext);
                    contentWidth = std::max(contentWidth, wrappedMeasurement.maxWidth);
                    contentHeight = std::max(contentHeight, wrappedMeasurement.maxHeight);
                }

                parentRect.contentBounds = IntRect
                {
                    .position = { 0, 0 },
                    .size = int2{ contentWidth, contentHeight }
                };

                if (isScrollable && element.id != ID::Invalid())
                {
                    ScrollOffsetHandle scrollOffsetHandle = m_scrollManagement.GetOrCreateScrollOffsetHandle(element.id);
                    int2 contentOffset = m_scrollManagement.GetScrollOffset(scrollOffsetHandle);
                    if (!isScrollableX) contentOffset.x = 0;
                    if (!isScrollableY) contentOffset.y = 0;
                    // A cache-hit scope is empty but its blitted content isn't: keep the offset.
                    if ((element.flags & ElementFlags::IsCachedScope) == ElementFlags::None)
                        contentOffset = LayoutHelpers::ClampScrollOffsetToContent(contentOffset, parentRect);

                    parentRect.innerRect += contentOffset;
                    m_scrollManagement.SetScrollOffset(scrollOffsetHandle, contentOffset);
                }
            }

            // Margin box -> border box on Y.
            LayoutHelpers::ApplyVerticalStyleOffset(parentRect);
        }
    }

} // namespace tachyon} // namespace tachyon