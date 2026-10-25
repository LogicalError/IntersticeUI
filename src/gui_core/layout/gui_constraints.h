#pragma once

#include <gui_core/gui_id.h>
#include <gui_core/gui_base_types.h>
#include <gui_core/gui_units.h>
#include <gui_core/style/gui_style.h>

namespace interstice
{
    // ------------------------------------------------------------------------
    // Layout constraints - optional per-element size/position overrides
    // ------------------------------------------------------------------------

    struct SizeConstraintAxis
    {
        static constexpr int32_t kAuto      = -1;
        static constexpr int32_t kUnbounded = std::numeric_limits<int32_t>::max();

        int32_t value = kAuto;       // Explicit size (kAuto = content-driven)
        int32_t min   = 0;
        int32_t max   = kUnbounded;
        // Which of value / min / max are CSS px, scaled to pixels when the element is added.
        uint8_t cssMask = 0;
        static constexpr uint8_t kValueCss = 1, kMinCss = 2, kMaxCss = 4;

        constexpr void SetValue(CssPx v)  { value = v.value; cssMask = uint8_t(cssMask | kValueCss); }
        constexpr void SetValue(Pixels v) { value = v.value; cssMask = uint8_t(cssMask & ~kValueCss); }
        constexpr void SetMin(CssPx v)    { min = v.value;   cssMask = uint8_t(cssMask | kMinCss); }
        constexpr void SetMin(Pixels v)   { min = v.value;   cssMask = uint8_t(cssMask & ~kMinCss); }
        constexpr void SetMax(CssPx v)    { max = v.value;   cssMask = uint8_t(cssMask | kMaxCss); }
        constexpr void SetMax(Pixels v)   { max = v.value;   cssMask = uint8_t(cssMask & ~kMaxCss); }

        // CSS px to pixels, with `px` (the style manager's Px).
        template<class PxFn> constexpr void ResolveCss(const PxFn& px)
        {
            if ((cssMask & kValueCss) && value != kAuto)     value = px(value);
            if (cssMask & kMinCss)                           min   = px(min);
            if ((cssMask & kMaxCss) && max != kUnbounded)    max   = px(max);
            cssMask = 0;
        }

        constexpr bool HasFixed() const { return value != kAuto; }

        constexpr int32_t Clamp(int32_t s) const { return std::clamp(s, min, max); }

        constexpr int32_t Apply(int32_t s) const { return HasFixed() ? value : Clamp(s); }

        bool operator==(const SizeConstraintAxis&) const noexcept = default;
    };

    AnchorPlacement FlipHorizontal(AnchorPlacement p);
    AnchorPlacement FlipVertical(AnchorPlacement p);

    enum class AutoFlipMode : uint8_t
    {
        None            = 0,
        Flip            = 1, // Flip placement, then clamp to workArea
        FlipAvoidTarget = 2  // Flip, then shrink to avoid overlapping the anchor target
    };

    // Per-axis position constraint
    struct PositionConstraintAxis
    {
        static constexpr int16_t kAuto = EdgeInset::kAuto;

        PositionType        type            = PositionType::Relative;
        AnchorPlacementAxis anchorPlacement = AnchorPlacementAxis::None;
        AutoFlipMode        autoFlip        = AutoFlipMode::None;
        ID                  anchorTarget    = ID::Invalid();
        int16_t             near = kAuto;  // left/top inset, or anchor offset
        int16_t             far  = kAuto;  // right/bottom inset (unused for anchored)

        constexpr bool IsInFlow() const { return type == PositionType::Relative || type == PositionType::Centered || type == PositionType::Sticky; }

        static constexpr PositionConstraintAxis Auto()     { return {}; }
        static constexpr PositionConstraintAxis Centered() { return { .type = PositionType::Centered }; }

        static constexpr PositionConstraintAxis Relative()                             { return { .type = PositionType::Relative, .near = 0, .far = 0 }; }
        static constexpr PositionConstraintAxis Relative(int16_t nearVal)              { return { .type = PositionType::Relative, .near = nearVal }; }
        static constexpr PositionConstraintAxis Relative(int16_t nearVal, int16_t farVal) { return { .type = PositionType::Relative, .near = nearVal, .far = farVal }; }

        static constexpr PositionConstraintAxis Absolute(int16_t nearVal = kAuto, int16_t farVal = kAuto) { return { .type = PositionType::Absolute, .near = nearVal, .far = farVal }; }
        static constexpr PositionConstraintAxis Fixed(int16_t nearVal = kAuto, int16_t farVal = kAuto)    { return { .type = PositionType::Fixed,    .near = nearVal, .far = farVal }; }

        static constexpr PositionConstraintAxis Anchored(ID target, AnchorPlacementAxis placement, int16_t offset = 0, AutoFlipMode flip = AutoFlipMode::None)
        {
            return { .type = PositionType::Anchored, .anchorPlacement = placement, .autoFlip = flip, .anchorTarget = target, .near = offset };
        }

        // In flow, but scroll can't push it past the parent's innerRect near edge + threshold.
        // threshold is stored in `near` and applied at the scroll-cascade clamp, not in Apply().
        static constexpr PositionConstraintAxis Sticky(int16_t threshold = 0)
        {
            return { .type = PositionType::Sticky, .near = threshold };
        }

        constexpr PositionConstraintAxis& WithType(PositionType t) { type = t; return *this; }

        // axis: 0=X, 1=Y.
        void Apply(IntRect& rect, int axis, const IntRect& targetRect) const;

        bool operator==(const PositionConstraintAxis&) const noexcept = default;
    };

    // Default defers to the element's style.
    enum class TriState : uint8_t { Default = 0, False = 1, True = 2 };

    // kDefault = use style value, >= 0 = override.
    struct FlexOverride
    {
        static constexpr int16_t kDefault = -1;

        int16_t grow   = kDefault;
        int16_t shrink = kDefault;

        constexpr bool HasGrow()   const { return grow   != kDefault; }
        constexpr bool HasShrink() const { return shrink != kDefault; }

        bool operator==(const FlexOverride&) const noexcept = default;
    };

    // Per-element overrides of style layout properties.
    struct LayoutOverride
    {
        SizeConstraintAxis width {}, height {};

        PositionConstraintAxis x {}, y {};

        FlexOverride flex {};

        struct
        {
            GridAxisProperties column{ .track = GridTrack::Default() };
            GridAxisProperties row{ .track = GridTrack::Default() };
        } grid;

        // Icon layout overrides (-1 = use style)
        int16_t  iconTextGapOverride       = -1;
        IconSide iconSideOverride      : 3 = IconSide::None;

        // 0 = use style, otherwise BreakBehavior + 1
        uint8_t  breakBehaviorOverride : 3 = 0;
        TriState clampScroll           : 2 = TriState::Default;

        // Set by ElementStorage::Add for display:none / visibility:collapsed; such
        // elements take no space in the parent's flex/grid.
        bool     outOfFlow = false;


        LayoutOverride() = default;

        // --- Static factories ---

        static LayoutOverride Auto()                           { return {}; }
        // Sizes in CSS px (scaled by the UI scale) or in Pixels (as they are); see gui_units.h.
        static LayoutOverride FixedSize(CssPx w, CssPx h)      { LayoutOverride lo; lo.width.SetValue(w); lo.height.SetValue(h); return lo; }
        static LayoutOverride FixedSize(Pixels w, Pixels h)    { LayoutOverride lo; lo.width.SetValue(w); lo.height.SetValue(h); return lo; }
        static LayoutOverride FixedSize(int2 pixels)           { return FixedSize(Pixels{ pixels.x }, Pixels{ pixels.y }); }   // int2 is pixels
        static LayoutOverride FixedWidth(CssPx w)              { LayoutOverride lo; lo.width.SetValue(w); return lo; }
        static LayoutOverride FixedWidth(Pixels w)             { LayoutOverride lo; lo.width.SetValue(w); return lo; }
        static LayoutOverride FixedHeight(CssPx h)             { LayoutOverride lo; lo.height.SetValue(h); return lo; }
        static LayoutOverride FixedHeight(Pixels h)            { LayoutOverride lo; lo.height.SetValue(h); return lo; }

        // --- Size convenience ---
        constexpr bool HasFixedWidth()  const { return width.HasFixed(); }
        constexpr bool HasFixedHeight() const { return height.HasFixed(); }
        constexpr bool HasFixedAxis(int axis) const { return axis == 0 ? width.HasFixed() : height.HasFixed(); }
        constexpr bool IsFullyConstrained() const { return width.HasFixed() && height.HasFixed(); }

        constexpr int32_t ClampWidth(int32_t w)  const { return width.Clamp(w); }
        constexpr int32_t ClampHeight(int32_t h) const { return height.Clamp(h); }

        int2 Size() const { return int2{width.value, height.value}; }
        int2 ApplySize(int2 s) const { return int2{width.Apply(s.x), height.Apply(s.y)}; }
        int32_t ApplyWidth(int32_t s) const { return width.Apply(s); }
        int32_t ApplyHeight(int32_t s) const { return height.Apply(s); }

        // --- Position convenience ---
        constexpr bool IsInFlowX()            const { return !outOfFlow && x.IsInFlow(); }
        constexpr bool IsInFlowY()            const { return !outOfFlow && y.IsInFlow(); }
        constexpr bool IsInFlow()             const { return !outOfFlow && x.IsInFlow() && y.IsInFlow(); }
        constexpr bool IsInFlowAxis(int axis) const { return !outOfFlow && (axis == 0 ? x.IsInFlow() : y.IsInFlow()); }

        void ApplyHorizontal(IntRect& rect, const IntRect& targetRect) const { x.Apply(rect, 0, targetRect); }
        void ApplyVertical(IntRect& rect, const IntRect& targetRect)   const { y.Apply(rect, 1, targetRect); }

        // --- Builders (work on both lvalues and temporaries) ---
        #define INTERSTICE_LO_BUILDER(name, body) \
            LayoutOverride&  name &  { body return *this; } \
            LayoutOverride&& name && { body return std::move(*this); }

        // Size builders: CSS px (scaled by the UI scale) or Pixels (as they are)
        INTERSTICE_LO_BUILDER(Size(CssPx w, CssPx h),          width.SetValue(w); height.SetValue(h);)
        INTERSTICE_LO_BUILDER(Size(Pixels w, Pixels h),        width.SetValue(w); height.SetValue(h);)
        INTERSTICE_LO_BUILDER(Size(int2 pixels),               width.SetValue(Pixels{ pixels.x }); height.SetValue(Pixels{ pixels.y });)
        INTERSTICE_LO_BUILDER(Width(CssPx w),                  width.SetValue(w);)
        INTERSTICE_LO_BUILDER(Width(Pixels w),                 width.SetValue(w);)
        INTERSTICE_LO_BUILDER(Height(CssPx h),                 height.SetValue(h);)
        INTERSTICE_LO_BUILDER(Height(Pixels h),                height.SetValue(h);)
        INTERSTICE_LO_BUILDER(MinWidth(CssPx w),               width.SetMin(w);)
        INTERSTICE_LO_BUILDER(MinWidth(Pixels w),              width.SetMin(w);)
        INTERSTICE_LO_BUILDER(MinHeight(CssPx h),              height.SetMin(h);)
        INTERSTICE_LO_BUILDER(MinHeight(Pixels h),             height.SetMin(h);)
        INTERSTICE_LO_BUILDER(MaxWidth(CssPx w),               width.SetMax(w);)
        INTERSTICE_LO_BUILDER(MaxWidth(Pixels w),              width.SetMax(w);)
        INTERSTICE_LO_BUILDER(MaxHeight(CssPx h),              height.SetMax(h);)
        INTERSTICE_LO_BUILDER(MaxHeight(Pixels h),             height.SetMax(h);)

        // CSS px to pixels; ElementStorage::Add does it with the style manager's Px.
        template<class PxFn> void ResolveCss(const PxFn& px) { width.ResolveCss(px); height.ResolveCss(px); }

        // Position builders (near = left/top, far = right/bottom)
        INTERSTICE_LO_BUILDER(Position(int2 p),                                     x = PositionConstraintAxis::Fixed(int16_t(p.x)); y = PositionConstraintAxis::Fixed(int16_t(p.y));)
        INTERSTICE_LO_BUILDER(Position(int32_t px, int32_t py),                     x = PositionConstraintAxis::Fixed(int16_t(px)); y = PositionConstraintAxis::Fixed(int16_t(py));)
        INTERSTICE_LO_BUILDER(Position(int32_t t, int32_t r, int32_t b, int32_t l), x = PositionConstraintAxis::Fixed(int16_t(l), int16_t(r)); y = PositionConstraintAxis::Fixed(int16_t(t), int16_t(b));)
        INTERSTICE_LO_BUILDER(Position(const EdgeInset& i),                         x = PositionConstraintAxis::Fixed(i.left, i.right); y = PositionConstraintAxis::Fixed(i.top, i.bottom);)

        INTERSTICE_LO_BUILDER(Centered(),                                           x = PositionConstraintAxis::Centered(); y = PositionConstraintAxis::Centered();)

        INTERSTICE_LO_BUILDER(Relative(),                                           x = PositionConstraintAxis::Relative(); y = PositionConstraintAxis::Relative();)
        INTERSTICE_LO_BUILDER(Relative(const EdgeInset& i),                         x = PositionConstraintAxis::Relative(i.left, i.right); y = PositionConstraintAxis::Relative(i.top, i.bottom);)
        INTERSTICE_LO_BUILDER(Relative(int32_t t, int32_t r, int32_t b, int32_t l), x = PositionConstraintAxis::Relative(int16_t(l), int16_t(r)); y = PositionConstraintAxis::Relative(int16_t(t), int16_t(b));)
        INTERSTICE_LO_BUILDER(Relative(int32_t px, int32_t py),                     x = PositionConstraintAxis::Relative(int16_t(px)); y = PositionConstraintAxis::Relative(int16_t(py));)
        INTERSTICE_LO_BUILDER(Relative(const int2& p),                              x = PositionConstraintAxis::Relative(int16_t(p.x)); y = PositionConstraintAxis::Relative(int16_t(p.y));)

        INTERSTICE_LO_BUILDER(Absolute(const EdgeInset& i),                         x = PositionConstraintAxis::Absolute(i.left, i.right); y = PositionConstraintAxis::Absolute(i.top, i.bottom);)
        INTERSTICE_LO_BUILDER(Absolute(int32_t t, int32_t r, int32_t b, int32_t l), x = PositionConstraintAxis::Absolute(int16_t(l), int16_t(r)); y = PositionConstraintAxis::Absolute(int16_t(t), int16_t(b));)
        INTERSTICE_LO_BUILDER(Absolute(int32_t px, int32_t py),                     x = PositionConstraintAxis::Absolute(int16_t(px)); y = PositionConstraintAxis::Absolute(int16_t(py));)
        INTERSTICE_LO_BUILDER(Absolute(const int2& p),                              x = PositionConstraintAxis::Absolute(int16_t(p.x)); y = PositionConstraintAxis::Absolute(int16_t(p.y));)

        INTERSTICE_LO_BUILDER(Anchored(ID target, AnchorPlacement pl, const int2& off = {}, AutoFlipMode af = AutoFlipMode::None), x = PositionConstraintAxis::Anchored(target, GetHorizontalAxis(pl), int16_t(off.x), af); y = PositionConstraintAxis::Anchored(target, GetVerticalAxis(pl), int16_t(off.y), af);)
        INTERSTICE_LO_BUILDER(Anchored(ID target, AnchorPlacement pl, int32_t ox, int32_t oy, AutoFlipMode af = AutoFlipMode::None), x = PositionConstraintAxis::Anchored(target, GetHorizontalAxis(pl), int16_t(ox), af); y = PositionConstraintAxis::Anchored(target, GetVerticalAxis(pl), int16_t(oy), af);)
        INTERSTICE_LO_BUILDER(Anchored(ID target, AnchorPlacementAxis px, int32_t ox, AnchorPlacementAxis py, int32_t oy, AutoFlipMode af = AutoFlipMode::None), x = PositionConstraintAxis::Anchored(target, px, int16_t(ox), af); y = PositionConstraintAxis::Anchored(target, py, int16_t(oy), af);)

        // Per-axis position builders
        INTERSTICE_LO_BUILDER(PositionX(PositionConstraintAxis ax), x = ax;)
        INTERSTICE_LO_BUILDER(PositionY(PositionConstraintAxis ay), y = ay;)
        INTERSTICE_LO_BUILDER(WithTypeX(PositionType t), x.type = t;)
        INTERSTICE_LO_BUILDER(WithTypeY(PositionType t), y.type = t;)

        // See PositionConstraintAxis::Sticky.
        INTERSTICE_LO_BUILDER(StickyX(int16_t threshold = 0), x = PositionConstraintAxis::Sticky(threshold);)
        INTERSTICE_LO_BUILDER(StickyY(int16_t threshold = 0), y = PositionConstraintAxis::Sticky(threshold);)
        INTERSTICE_LO_BUILDER(Sticky(int16_t thresholdX = 0, int16_t thresholdY = 0),
                   x = PositionConstraintAxis::Sticky(thresholdX);
                   y = PositionConstraintAxis::Sticky(thresholdY);)

        // Flow control builders
        INTERSTICE_LO_BUILDER(BreakAfter(bool v = true),   breakBehaviorOverride = v ? (uint8_t)BreakBehavior::Always + 1 : (uint8_t)BreakBehavior::Auto + 1;)
        INTERSTICE_LO_BUILDER(BreakBefore(bool v = true),  breakBehaviorOverride = v ? (uint8_t)BreakBehavior::Before + 1 : (uint8_t)BreakBehavior::Auto + 1;)
        INTERSTICE_LO_BUILDER(KeepWithNext(bool v = true), breakBehaviorOverride = v ? (uint8_t)BreakBehavior::Avoid  + 1 : (uint8_t)BreakBehavior::Auto + 1;)

        // Flex override builders
        INTERSTICE_LO_BUILDER(FlexGrow(int16_t v),   flex.grow = v;)
        INTERSTICE_LO_BUILDER(FlexShrink(int16_t v), flex.shrink = v;)
        INTERSTICE_LO_BUILDER(NoGrow(),              flex.grow = 0;)
        INTERSTICE_LO_BUILDER(NoShrink(),            flex.shrink = 0;)

        // Icon layout builders
        INTERSTICE_LO_BUILDER(SetIconSide(IconSide s),       iconSideOverride = s;)
        INTERSTICE_LO_BUILDER(IconTextGap(int16_t gap),      iconTextGapOverride = gap;)

        INTERSTICE_LO_BUILDER(ClampScroll(bool v = true),    clampScroll = v ? TriState::True : TriState::False;)

        // Grid track builders
        INTERSTICE_LO_BUILDER(ColumnTrack(GridTrack t),                    grid.column.track = t;)
        INTERSTICE_LO_BUILDER(RowTrack(GridTrack t),                       grid.row.track = t;)
        INTERSTICE_LO_BUILDER(GridColumn(int16_t start, int16_t span = 1), grid.column.start = start; grid.column.span = span;)
        INTERSTICE_LO_BUILDER(GridRow(int16_t start, int16_t span = 1),    grid.row.start = start;    grid.row.span = span;)

        #undef INTERSTICE_LO_BUILDER

        // --- Resolvers (apply override or fall back to style) ---

        uint32_t GetFlexGrow(const LayoutStyle* ls) const
        {
            if (flex.grow != FlexOverride::kDefault) return (uint32_t)std::max<int16_t>(0, flex.grow);
            return ls ? ls->layoutData.flex.flexGrow : 0;
        }
        uint32_t GetFlexShrink(const LayoutStyle* ls) const
        {
            if (flex.shrink != FlexOverride::kDefault) return (uint32_t)std::max<int16_t>(0, flex.shrink);
            // Defaults to 0, unlike CSS (1), so widgets keep their intrinsic size;
            // opt in with `flex-shrink: 1`.
            return ls ? ls->layoutData.flex.flexShrink : 0;
        }

        BreakBehavior GetBreakBehavior(const LayoutStyle* ls) const { return breakBehaviorOverride != 0 ? (BreakBehavior)(breakBehaviorOverride - 1) : (ls ? ls->breakBehavior : BreakBehavior::Auto); }
        bool GetBreakBefore (const LayoutStyle* ls) const { return GetBreakBehavior(ls) == BreakBehavior::Before; }
        bool GetBreakAfter  (const LayoutStyle* ls) const { return GetBreakBehavior(ls) == BreakBehavior::Always; }
        bool GetKeepWithNext(const LayoutStyle* ls) const { return GetBreakBehavior(ls) == BreakBehavior::Avoid;  }
        bool GetClampScroll (const LayoutStyle* ls) const { return clampScroll != TriState::Default ? clampScroll == TriState::True : !ls || ls->clampScroll; }

        IconSide GetIconSide(const LayoutStyle* ls) const { return iconSideOverride != IconSide::None ? iconSideOverride : (ls ? ls->iconSide : IconSide::Left); }
        uint16_t GetIconTextGap(const LayoutStyle* ls) const { return iconTextGapOverride >= 0 ? (uint16_t)iconTextGapOverride : (ls ? ls->iconTextGap : 0); }

        // Style values with non-Default overrides applied.
        GridProperties GetGridItem(const LayoutStyle* layoutStyle) const
        {
            GridProperties result = layoutStyle ? layoutStyle->layoutData.grid : GridProperties{};
            if (grid.column.track.type != GridTrack::Type::Default) result.column.track = grid.column.track;
            if (grid.row.track.type    != GridTrack::Type::Default) result.row.track    = grid.row.track;
            if (grid.column.start >= 0) { result.column.start = grid.column.start; result.column.span = grid.column.span; }
            if (grid.row.start    >= 0) { result.row.start    = grid.row.start;    result.row.span    = grid.row.span;    }
            return result;
        }

        bool operator==(const LayoutOverride&) const noexcept = default;
    };

} // namespace interstice