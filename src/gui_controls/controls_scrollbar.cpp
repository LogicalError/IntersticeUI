#include <gui_controls/gui_application.h>

#include <algorithm>

namespace interstice
{

    namespace
    {
        // Scroll-axis length in span-and-clip mode (no explicit trackLength): the
        // parent's clipRect crops it, and scroll math uses the parent's innerRect.
        constexpr int32_t kScrollbarAxisSpan = 32767;

        // Cross-axis inset of the thumb per side, in CSS pixels. Visual only; the hit region is the full track.
        constexpr int32_t kScrollbarThumbMargin = 2;

        // Per-side border + padding of an element's layout style (zero if missing).
        // Anchored positioning is relative to the border box, so this pulls bars inside the innerRect.
        struct Insets { int16_t top=0, right=0, bottom=0, left=0; };

        // Rounded division for POSITIVE inputs. Must match MakeScrollbarHandler's rounding
        // so thumb->scroll->thumb round-trips; truncation makes small drags move nothing.
        inline int32_t DivRound(int64_t num, int64_t den)
        {
            if (den <= 0) return 0;
            return static_cast<int32_t>((num + den / 2) / den);
        }
    }

    static Insets InnerInsetsOf(interstice::GUIApplication& app, ID elementID)
    {
        Insets result{};
        const auto ref = app.elementStorage.FindElementRefByID(elementID);
        if (!ref.IsValid()) return result;
        const auto* elem = app.elementStorage.GetElementPtr(ref);
        if (!elem) return result;
        const auto* ls = app.styleManager.GetLayoutStyle(elem->styleRef.layoutStyleIndex);
        if (!ls) return result;
        result.top    = static_cast<int16_t>(ls->borderWidths.top    + ls->padding.top);
        result.right  = static_cast<int16_t>(ls->borderWidths.right  + ls->padding.right);
        result.bottom = static_cast<int16_t>(ls->borderWidths.bottom + ls->padding.bottom);
        result.left   = static_cast<int16_t>(ls->borderWidths.left   + ls->padding.left);
        return result;
    }

    // Per-axis helpers.
    namespace {
        inline int32_t axisExtent(bool vertical, int2 v) { return vertical ? v.y : v.x; }
        // thickness / margin: the track's cross size and the thumb's inset from it.
        inline IntRect axisRect (bool vertical, int32_t alongOffset, int32_t alongSize, int32_t thickness, int32_t margin)
        {
            const int32_t cross = thickness - 2 * margin;
            return vertical
                ? IntRect{ .position = int2{ margin, alongOffset }, .size = int2{ cross, alongSize } }
                : IntRect{ .position = int2{ alongOffset, margin }, .size = int2{ alongSize, cross } };
        }
    }

    // Emit one bar (track + thumb custom-draw). trackLength 0 = span-and-clip; > 0 = explicit
    // scroll-axis size. The thumb's custom-draw hides itself when there's no overflow.
    static void EmitScrollbarBar(GUIApplication& app, ID scrollBarID, ID containerID,
                                 bool vertical, const Insets& inset, int32_t trackLength)
    {
        const int32_t axisLen        = (trackLength > 0) ? trackLength : kScrollbarAxisSpan;
        const int32_t thickness      = app.theme.scrollbarThickness;
        const bool    measureFromSelf = (trackLength > 0);

        const StyleRef trackStyleRef = vertical
            ? app.theme.scrollbarVerticalTrackStyleRef
            : app.theme.scrollbarHorizontalTrackStyleRef;
        const StyleRef thumbStyle = vertical
            ? app.theme.scrollbarVerticalThumbStyleRef
            : app.theme.scrollbarHorizontalThumbStyleRef;

        interstice::Element track{};
        track.id        = scrollBarID;
        track.styleRef  = trackStyleRef;
        track.state     = app.guiStateManager.GetCurrentState(scrollBarID);
        track.eventHandler = MakeScrollbarHandler(app.guiStateManager, app.elementStorage,
            containerID, vertical, trackLength);

        const auto layout = vertical
            ? LayoutOverride{}
                .Size(thickness, axisLen)
                .Anchored(containerID,
                          AnchorPlacementAxis::InsideMax, static_cast<int16_t>(-inset.right),
                          AnchorPlacementAxis::InsideMin, inset.top)
                .NoGrow().NoShrink()
            : LayoutOverride{}
                .Size(axisLen, thickness)
                .Anchored(containerID,
                          AnchorPlacementAxis::InsideMin, inset.left,
                          AnchorPlacementAxis::InsideMax, static_cast<int16_t>(-inset.bottom))
                .NoGrow().NoShrink();

        const auto trackRef = app.RegisterElement(std::move(track), interstice::LayoutOverride{layout});

        // Thumb: painted by a custom-draw on the track; no overflow, no thumb.
        auto result = app.elementStorage.AddCustomDraw(trackRef,
            [&app, containerID, thumbStyle, vertical, measureFromSelf, thickness](CustomDrawContext& ctx)
            {
                const auto parentRef    = ctx.GetParentRef(ctx.Ref());
                const int32_t contentExtent = axisExtent(vertical, ctx.GetContentSize(parentRef));
                const int32_t visibleExtent = axisExtent(vertical, ctx.GetInnerSize(parentRef));
                if (visibleExtent <= 0 || contentExtent <= visibleExtent) return;

                int32_t trackExtent = visibleExtent;
                if (measureFromSelf)
                {
                    trackExtent = axisExtent(vertical, ctx.ElementInnerRect().size);
                    if (trackExtent <= 0) return;
                }

                int32_t thumbExtent = static_cast<int32_t>(
                    static_cast<int64_t>(trackExtent) * visibleExtent / contentExtent);
                thumbExtent = std::clamp<int32_t>(thumbExtent,
                    std::min<int32_t>(app.styleManager.Px(kScrollbarMinThumbLength), trackExtent), trackExtent);

                auto& sm = app.guiStateManager.GetScrollManagement();
                const auto handle = sm.GetScrollOffsetHandle(containerID);
                int32_t scrollPos = 0;
                if (sm.IsScrollOffsetHandleValid(handle))
                    scrollPos = -axisExtent(vertical, sm.GetScrollOffset(handle)); // NEGATIVE offset = scrolled forward

                const int32_t scrollRange    = contentExtent - visibleExtent;
                const int32_t maxThumbOffset = trackExtent - thumbExtent;
                const int32_t thumbOffset    = (scrollRange > 0)
                    ? DivRound(static_cast<int64_t>(scrollPos) * maxThumbOffset, scrollRange)
                    : 0;

                ctx.RenderBox(axisRect(vertical, thumbOffset, thumbExtent, thickness, app.styleManager.Px(kScrollbarThumbMargin)),
                              thumbStyle.visualBoxStyleIndex, ElementState::None);
            });
        VERIFY(result);
    }

    // Cap for the bottom-right corner where both scrollbars meet. Paints nothing
    // unless the parent overflows on both axes.
    static void EmitScrollbarCornerCap(GUIApplication& app, ID containerID, const Insets& inset)
    {
        (void)app.idManager.PushParent(containerID);
        const ID cornerID = app.idManager.AllocateDynamicID("_scrollbar_corner");
        (void)app.idManager.PopParent();
        const StyleRef capStyle = app.theme.scrollbarVerticalTrackStyleRef;
        interstice::Element corner{};
        corner.id       = cornerID;
        corner.styleRef = capStyle;
        corner.flags    = ElementFlags::NoInput;
        corner.state    = app.guiStateManager.GetCurrentState(cornerID);
        auto cRes = app.elementStorage.Add(std::move(corner),
            interstice::LayoutOverride{}
                .Anchored(containerID,
                          AnchorPlacementAxis::InsideMax, static_cast<int16_t>(-inset.right),
                          AnchorPlacementAxis::InsideMax, static_cast<int16_t>(-inset.bottom))
                .Size(app.theme.scrollbarThickness, app.theme.scrollbarThickness)
                .NoGrow().NoShrink());
        VERIFY(cRes.status);
        VERIFY(app.windowRegistry.Add(cRes.ref));
        auto cdRes = app.elementStorage.AddCustomDraw(cRes.ref,
            [&app, capStyle](CustomDrawContext& ctx)
            {
                const auto parentRef = ctx.GetParentRef(ctx.Ref());
                const auto inner   = ctx.GetInnerSize(parentRef);
                const auto content = ctx.GetContentSize(parentRef);
                const bool vOverflow = inner.y > 0 && content.y > inner.y;
                const bool hOverflow = inner.x > 0 && content.x > inner.x;
                if (!(vOverflow && hOverflow)) return;
                ctx.RenderBox(IntRect{ .position = int2{0,0}, .size = ctx.ElementInnerRect().size },
                              capStyle.visualBoxStyleIndex, ElementState::None);
            });
        VERIFY(cdRes);
    }

    void GUIApplication::VerticalScrollbar(ID scrollBarID, ID containerID)
    {
        if (containerID == ID::Invalid()) return;
        const Insets inset = InnerInsetsOf(*this, containerID);
        EmitScrollbarBar(*this, scrollBarID, containerID, /*vertical*/ true, inset, /*trackLength*/0);
    }

    void GUIApplication::HorizontalScrollbar(ID scrollBarID, ID containerID)
    {
        if (containerID == ID::Invalid()) return;
        const Insets inset = InnerInsetsOf(*this, containerID);
        EmitScrollbarBar(*this, scrollBarID, containerID, /*vertical*/ false, inset, /*trackLength*/0);
    }

    // Emits the bars and corner cap per the overflow flags. Bars must be skipped, not just
    // hidden: a track captures input even when it paints nothing. `lastInnerSize` is the
    // parent's latest innerRect, used to keep the bars from overlapping each other.
    void GUIApplication::ScrollbarOverlay(ID containerID,
                                          int2 lastInnerSize,
                                          ID vertScrollBarID, ID horzScrollBarID,
                                          bool emitVert, bool emitHorz)
    {
        if (containerID == ID::Invalid()) return;

        const Insets inset = InnerInsetsOf(*this, containerID);

        if (vertScrollBarID == ID::Invalid())
        {
            (void)idManager.PushParent(containerID);
            vertScrollBarID = idManager.AllocateDynamicID("vertical_scrollbar");
            (void)idManager.PopParent();
        }
        if (horzScrollBarID == ID::Invalid())
        {
            (void)idManager.PushParent(containerID);
            horzScrollBarID = idManager.AllocateDynamicID("horizontal_scrollbar");
            (void)idManager.PopParent();
        }

        // Leave room for the corner only when both bars are shown.
        const int32_t thickness   = theme.scrollbarThickness;
        const int32_t cornerInset = (emitVert && emitHorz) ? thickness : 0;
        const int32_t vLen = std::max<int32_t>(thickness + 1, lastInnerSize.y - cornerInset);
        const int32_t hLen = std::max<int32_t>(thickness + 1, lastInnerSize.x - cornerInset);

        if (emitVert) EmitScrollbarBar(*this, vertScrollBarID, containerID, /*vertical*/ true,  inset, vLen);
        if (emitHorz) EmitScrollbarBar(*this, horzScrollBarID, containerID, /*vertical*/ false, inset, hLen);
        if (emitVert && emitHorz) EmitScrollbarCornerCap(*this, containerID, inset);
    }
}
