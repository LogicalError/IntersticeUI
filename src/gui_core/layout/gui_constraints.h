#pragma once

#include <gui_core/gui_id.h>
#include <gui_core/gui_base_types.h>
#include <gui_core/gui_units.h>
#include <gui_core/style/gui_style.h>

namespace tachyon
{
    // ------------------------------------------------------------------------
    // Layout constraints - optional per-element size/position overrides
    // ------------------------------------------------------------------------

    /// Per-axis size constraint: an explicit size, or a min / max clamp on the content-driven size.
    struct SizeConstraintAxis
    {
        static constexpr int32_t kAuto      = -1;                                  ///< No explicit size
        static constexpr int32_t kUnbounded = std::numeric_limits<int32_t>::max(); ///< No maximum

        int32_t value = kAuto;       ///< Explicit size (kAuto = content-driven)
        int32_t min   = 0;           ///< Lower clamp for the content-driven size
        int32_t max   = kUnbounded;  ///< Upper clamp for the content-driven size
        /// Which of value / min / max are CSS px, scaled to pixels when the element is added.
        uint8_t cssMask = 0;
        static constexpr uint8_t kValueCss = 1, kMinCss = 2, kMaxCss = 4;

        /// Setters take CSS px (scaled when the element is added) or Pixels (used as is).
        constexpr void SetValue(CssPx v)  { value = v.value; cssMask = uint8_t(cssMask | kValueCss); }
        constexpr void SetValue(Pixels v) { value = v.value; cssMask = uint8_t(cssMask & ~kValueCss); }
        constexpr void SetMin(CssPx v)    { min = v.value;   cssMask = uint8_t(cssMask | kMinCss); }
        constexpr void SetMin(Pixels v)   { min = v.value;   cssMask = uint8_t(cssMask & ~kMinCss); }
        constexpr void SetMax(CssPx v)    { max = v.value;   cssMask = uint8_t(cssMask | kMaxCss); }
        constexpr void SetMax(Pixels v)   { max = v.value;   cssMask = uint8_t(cssMask & ~kMaxCss); }

        /// CSS px to pixels, with `px` (the style manager's Px).
        template<class PxFn> constexpr void ResolveCss(const PxFn& px)
        {
            if ((cssMask & kValueCss) && value != kAuto)     value = px(value);
            if (cssMask & kMinCss)                           min   = px(min);
            if ((cssMask & kMaxCss) && max != kUnbounded)    max   = px(max);
            cssMask = 0;
        }

        /// True when an explicit size is set.
        constexpr bool HasFixed() const { return value != kAuto; }

        /// Clamps `s` to [min, max].
        constexpr int32_t Clamp(int32_t s) const { return std::clamp(s, min, max); }

        /// The explicit size if set, else `s` clamped to [min, max].
        constexpr int32_t Apply(int32_t s) const { return HasFixed() ? value : Clamp(s); }

        bool operator==(const SizeConstraintAxis&) const noexcept = default;
    };

    /// Mirrors the horizontal part of a placement (e.g. right-of becomes left-of); vertical is kept.
    AnchorPlacement FlipHorizontal(AnchorPlacement p);
    /// Mirrors the vertical part of a placement (e.g. below becomes above); horizontal is kept.
    AnchorPlacement FlipVertical(AnchorPlacement p);

    /// What an anchored element does when its placement would leave the work area.
    enum class AutoFlipMode : uint8_t
    {
        None            = 0, ///< Keep the placement as given
        Flip            = 1, ///< Flip placement, then clamp to workArea
        FlipAvoidTarget = 2  ///< Flip, then shrink to avoid overlapping the anchor target
    };

    /// Where an element goes on one axis: in the parent's flow (Relative, Centered, Sticky) or
    /// out of it (Absolute, Fixed, Anchored).
    struct PositionConstraintAxis
    {
        static constexpr int16_t kAuto = EdgeInset::kAuto; ///< Inset not set

        PositionType        type            = PositionType::Relative;
        AnchorPlacementAxis anchorPlacement = AnchorPlacementAxis::None; ///< Anchored only: where along the target
        AutoFlipMode        autoFlip        = AutoFlipMode::None;        ///< Anchored only
        ID                  anchorTarget    = ID::Invalid();             ///< Anchored only: the element to anchor to
        int16_t             near = kAuto;  ///< left/top inset, or anchor offset (pixels)
        int16_t             far  = kAuto;  ///< right/bottom inset (unused for anchored)

        /// Relative, Centered and Sticky take part in the parent's flex/grid flow.
        constexpr bool IsInFlow() const { return type == PositionType::Relative || type == PositionType::Centered || type == PositionType::Sticky; }

        /// Default: relative, no offset.
        static constexpr PositionConstraintAxis Auto()     { return {}; }
        /// Centered in the parent.
        static constexpr PositionConstraintAxis Centered() { return { .type = PositionType::Centered }; }

        /// In flow, shifted by `near` (or back by `far` when near is unset).
        static constexpr PositionConstraintAxis Relative()                             { return { .type = PositionType::Relative, .near = 0, .far = 0 }; }
        static constexpr PositionConstraintAxis Relative(int16_t nearVal)              { return { .type = PositionType::Relative, .near = nearVal }; }
        static constexpr PositionConstraintAxis Relative(int16_t nearVal, int16_t farVal) { return { .type = PositionType::Relative, .near = nearVal, .far = farVal }; }

        /// Out of flow, inset `near` from the parent's start or `far` from its end; scrolls with the parent.
        static constexpr PositionConstraintAxis Absolute(int16_t nearVal = kAuto, int16_t farVal = kAuto) { return { .type = PositionType::Absolute, .near = nearVal, .far = farVal }; }
        /// Like Absolute, but in overlay space: not moved by the parent's scroll.
        static constexpr PositionConstraintAxis Fixed(int16_t nearVal = kAuto, int16_t farVal = kAuto)    { return { .type = PositionType::Fixed,    .near = nearVal, .far = farVal }; }

        /// Placed relative to the `target` element's rect, plus `offset` pixels.
        static constexpr PositionConstraintAxis Anchored(ID target, AnchorPlacementAxis placement, int16_t offset = 0, AutoFlipMode flip = AutoFlipMode::None)
        {
            return { .type = PositionType::Anchored, .anchorPlacement = placement, .autoFlip = flip, .anchorTarget = target, .near = offset };
        }

        /// In flow, but scrolling doesn't move it past `threshold` pixels inside the parent's
        /// padding box, like CSS position: sticky.
        static constexpr PositionConstraintAxis Sticky(int16_t threshold = 0)
        {
            return { .type = PositionType::Sticky, .near = threshold };
        }

        /// Changes only the position type, keeping the insets.
        constexpr PositionConstraintAxis& WithType(PositionType t) { type = t; return *this; }

        /// Positions `rect` on `axis` (0 = x, 1 = y) against targetRect (the parent's, or the anchor's).
        void Apply(IntRect& rect, int axis, const IntRect& targetRect) const;

        bool operator==(const PositionConstraintAxis&) const noexcept = default;
    };

    /// Default defers to the element's style.
    enum class TriState : uint8_t { Default = 0, False = 1, True = 2 };

    /// kDefault = use style value, >= 0 = override.
    struct FlexOverride
    {
        static constexpr int16_t kDefault = -1; ///< Use the style's value

        int16_t grow   = kDefault; ///< flex-grow override
        int16_t shrink = kDefault; ///< flex-shrink override

        constexpr bool HasGrow()   const { return grow   != kDefault; }
        constexpr bool HasShrink() const { return shrink != kDefault; }

        bool operator==(const FlexOverride&) const noexcept = default;
    };

    /// Per-element overrides of style layout properties.
    struct LayoutOverride
    {
        SizeConstraintAxis width {}, height {};   ///< Size constraints; set with the size builders

        PositionConstraintAxis x {}, y {};        ///< Position constraints; set with the position builders

        FlexOverride flex {};                     ///< flex-grow / flex-shrink overrides

        /// Grid item overrides; Default tracks and start < 0 keep the style's values.
        struct
        {
            GridAxisProperties column{ .track = GridTrack::Default() };
            GridAxisProperties row{ .track = GridTrack::Default() };
        } grid;

        /// Icon layout overrides (-1 = use style)
        int16_t  iconTextGapOverride       = -1;
        IconSide iconSideOverride      : 3 = IconSide::None; ///< None = use style

        /// 0 = use style, otherwise BreakBehavior + 1
        uint8_t  breakBehaviorOverride : 3 = 0;
        TriState clampScroll           : 2 = TriState::Default; ///< Override of the style's scroll clamping

        /// Set by ElementStorage::Add for display:none / visibility:collapsed; such
        /// elements take no space in the parent's flex/grid.
        bool     outOfFlow = false;


        LayoutOverride() = default;

        // --- Static factories ---

        /// No overrides: everything comes from the style.
        static LayoutOverride Auto()                           { return {}; }
        /// Sizes in CSS px (scaled by the UI scale) or in Pixels (as they are); see gui_units.h.
        static LayoutOverride FixedSize(CssPx w, CssPx h)      { LayoutOverride lo; lo.width.SetValue(w); lo.height.SetValue(h); return lo; }
        static LayoutOverride FixedSize(Pixels w, Pixels h)    { LayoutOverride lo; lo.width.SetValue(w); lo.height.SetValue(h); return lo; }
        static LayoutOverride FixedSize(int2 pixels)           { return FixedSize(Pixels{ pixels.x }, Pixels{ pixels.y }); }   // int2 is pixels
        static LayoutOverride FixedWidth(CssPx w)              { LayoutOverride lo; lo.width.SetValue(w); return lo; }
        static LayoutOverride FixedWidth(Pixels w)             { LayoutOverride lo; lo.width.SetValue(w); return lo; }
        static LayoutOverride FixedHeight(CssPx h)             { LayoutOverride lo; lo.height.SetValue(h); return lo; }
        static LayoutOverride FixedHeight(Pixels h)            { LayoutOverride lo; lo.height.SetValue(h); return lo; }

        // --- Size convenience ---
        /// Whether an explicit width / height is set; axis 0 = width, 1 = height.
        constexpr bool HasFixedWidth()  const { return width.HasFixed(); }
        constexpr bool HasFixedHeight() const { return height.HasFixed(); }
        constexpr bool HasFixedAxis(int axis) const { return axis == 0 ? width.HasFixed() : height.HasFixed(); }
        /// Both width and height are explicit.
        constexpr bool IsFullyConstrained() const { return width.HasFixed() && height.HasFixed(); }

        /// Clamps a size to the min / max constraints, ignoring the explicit size.
        constexpr int32_t ClampWidth(int32_t w)  const { return width.Clamp(w); }
        constexpr int32_t ClampHeight(int32_t h) const { return height.Clamp(h); }

        /// The explicit sizes; kAuto on axes without one.
        int2 Size() const { return int2{width.value, height.value}; }
        /// The explicit size where set, else `s` clamped to min / max.
        int2 ApplySize(int2 s) const { return int2{width.Apply(s.x), height.Apply(s.y)}; }
        int32_t ApplyWidth(int32_t s) const { return width.Apply(s); }
        int32_t ApplyHeight(int32_t s) const { return height.Apply(s); }

        // --- Position convenience ---
        /// True when the element takes part in the parent's flow on that axis (see PositionConstraintAxis::IsInFlow).
        constexpr bool IsInFlowX()            const { return !outOfFlow && x.IsInFlow(); }
        constexpr bool IsInFlowY()            const { return !outOfFlow && y.IsInFlow(); }
        constexpr bool IsInFlow()             const { return !outOfFlow && x.IsInFlow() && y.IsInFlow(); }
        constexpr bool IsInFlowAxis(int axis) const { return !outOfFlow && (axis == 0 ? x.IsInFlow() : y.IsInFlow()); }

        /// Positions `rect` on one axis against `targetRect` (the parent, or the anchor target).
        void ApplyHorizontal(IntRect& rect, const IntRect& targetRect) const { x.Apply(rect, 0, targetRect); }
        void ApplyVertical(IntRect& rect, const IntRect& targetRect)   const { y.Apply(rect, 1, targetRect); }

        // --- Builders (work on both lvalues and temporaries) ---
        #define TACHYON_LO_BUILDER(name, body) \
            LayoutOverride&  name &  { body return *this; } \
            LayoutOverride&& name && { body return std::move(*this); }

        /// Size, min and max size, in CSS px (scaled by the UI scale) or Pixels (as they are).
        ///@{
        TACHYON_LO_BUILDER(Size(CssPx w, CssPx h),          width.SetValue(w); height.SetValue(h);)
        TACHYON_LO_BUILDER(Size(Pixels w, Pixels h),        width.SetValue(w); height.SetValue(h);)
        TACHYON_LO_BUILDER(Size(int2 pixels),               width.SetValue(Pixels{ pixels.x }); height.SetValue(Pixels{ pixels.y });)
        TACHYON_LO_BUILDER(Width(CssPx w),                  width.SetValue(w);)
        TACHYON_LO_BUILDER(Width(Pixels w),                 width.SetValue(w);)
        TACHYON_LO_BUILDER(Height(CssPx h),                 height.SetValue(h);)
        TACHYON_LO_BUILDER(Height(Pixels h),                height.SetValue(h);)
        TACHYON_LO_BUILDER(MinWidth(CssPx w),               width.SetMin(w);)
        TACHYON_LO_BUILDER(MinWidth(Pixels w),              width.SetMin(w);)
        TACHYON_LO_BUILDER(MinHeight(CssPx h),              height.SetMin(h);)
        TACHYON_LO_BUILDER(MinHeight(Pixels h),             height.SetMin(h);)
        TACHYON_LO_BUILDER(MaxWidth(CssPx w),               width.SetMax(w);)
        TACHYON_LO_BUILDER(MaxWidth(Pixels w),              width.SetMax(w);)
        TACHYON_LO_BUILDER(MaxHeight(CssPx h),              height.SetMax(h);)
        TACHYON_LO_BUILDER(MaxHeight(Pixels h),             height.SetMax(h);)
        ///@}

        /// CSS px to pixels; ElementStorage::Add does it with the style manager's Px.
        template<class PxFn> void ResolveCss(const PxFn& px) { width.ResolveCss(px); height.ResolveCss(px); }

        /// Position builders, in pixels (near = left/top, far = right/bottom).
        /// Position() is out of flow in overlay space (PositionType::Fixed): it ignores the parent's scroll.
        ///@{
        TACHYON_LO_BUILDER(Position(int2 p),                                     x = PositionConstraintAxis::Fixed(int16_t(p.x)); y = PositionConstraintAxis::Fixed(int16_t(p.y));)
        TACHYON_LO_BUILDER(Position(int32_t px, int32_t py),                     x = PositionConstraintAxis::Fixed(int16_t(px)); y = PositionConstraintAxis::Fixed(int16_t(py));)
        TACHYON_LO_BUILDER(Position(int32_t t, int32_t r, int32_t b, int32_t l), x = PositionConstraintAxis::Fixed(int16_t(l), int16_t(r)); y = PositionConstraintAxis::Fixed(int16_t(t), int16_t(b));)
        TACHYON_LO_BUILDER(Position(const EdgeInset& i),                         x = PositionConstraintAxis::Fixed(i.left, i.right); y = PositionConstraintAxis::Fixed(i.top, i.bottom);)
        ///@}

        /// Centered in the parent on both axes.
        TACHYON_LO_BUILDER(Centered(),                                           x = PositionConstraintAxis::Centered(); y = PositionConstraintAxis::Centered();)

        /// In flow, shifted by the given offsets from the laid-out position.
        ///@{
        TACHYON_LO_BUILDER(Relative(),                                           x = PositionConstraintAxis::Relative(); y = PositionConstraintAxis::Relative();)
        TACHYON_LO_BUILDER(Relative(const EdgeInset& i),                         x = PositionConstraintAxis::Relative(i.left, i.right); y = PositionConstraintAxis::Relative(i.top, i.bottom);)
        TACHYON_LO_BUILDER(Relative(int32_t t, int32_t r, int32_t b, int32_t l), x = PositionConstraintAxis::Relative(int16_t(l), int16_t(r)); y = PositionConstraintAxis::Relative(int16_t(t), int16_t(b));)
        TACHYON_LO_BUILDER(Relative(int32_t px, int32_t py),                     x = PositionConstraintAxis::Relative(int16_t(px)); y = PositionConstraintAxis::Relative(int16_t(py));)
        TACHYON_LO_BUILDER(Relative(const int2& p),                              x = PositionConstraintAxis::Relative(int16_t(p.x)); y = PositionConstraintAxis::Relative(int16_t(p.y));)
        ///@}

        /// Out of flow, inset from the parent's edges; scrolls with the parent's content.
        ///@{
        TACHYON_LO_BUILDER(Absolute(const EdgeInset& i),                         x = PositionConstraintAxis::Absolute(i.left, i.right); y = PositionConstraintAxis::Absolute(i.top, i.bottom);)
        TACHYON_LO_BUILDER(Absolute(int32_t t, int32_t r, int32_t b, int32_t l), x = PositionConstraintAxis::Absolute(int16_t(l), int16_t(r)); y = PositionConstraintAxis::Absolute(int16_t(t), int16_t(b));)
        TACHYON_LO_BUILDER(Absolute(int32_t px, int32_t py),                     x = PositionConstraintAxis::Absolute(int16_t(px)); y = PositionConstraintAxis::Absolute(int16_t(py));)
        TACHYON_LO_BUILDER(Absolute(const int2& p),                              x = PositionConstraintAxis::Absolute(int16_t(p.x)); y = PositionConstraintAxis::Absolute(int16_t(p.y));)
        ///@}

        /// Placed next to / inside the `target` element per the placement, plus an offset in pixels.
        /// `af` flips the placement when it would leave the work area.
        ///@{
        TACHYON_LO_BUILDER(Anchored(ID target, AnchorPlacement pl, const int2& off = {}, AutoFlipMode af = AutoFlipMode::None), x = PositionConstraintAxis::Anchored(target, GetHorizontalAxis(pl), int16_t(off.x), af); y = PositionConstraintAxis::Anchored(target, GetVerticalAxis(pl), int16_t(off.y), af);)
        TACHYON_LO_BUILDER(Anchored(ID target, AnchorPlacement pl, int32_t ox, int32_t oy, AutoFlipMode af = AutoFlipMode::None), x = PositionConstraintAxis::Anchored(target, GetHorizontalAxis(pl), int16_t(ox), af); y = PositionConstraintAxis::Anchored(target, GetVerticalAxis(pl), int16_t(oy), af);)
        TACHYON_LO_BUILDER(Anchored(ID target, AnchorPlacementAxis px, int32_t ox, AnchorPlacementAxis py, int32_t oy, AutoFlipMode af = AutoFlipMode::None), x = PositionConstraintAxis::Anchored(target, px, int16_t(ox), af); y = PositionConstraintAxis::Anchored(target, py, int16_t(oy), af);)
        ///@}

        /// Sets one axis's constraint, or only its PositionType.
        ///@{
        TACHYON_LO_BUILDER(PositionX(PositionConstraintAxis ax), x = ax;)
        TACHYON_LO_BUILDER(PositionY(PositionConstraintAxis ay), y = ay;)
        TACHYON_LO_BUILDER(WithTypeX(PositionType t), x.type = t;)
        TACHYON_LO_BUILDER(WithTypeY(PositionType t), y.type = t;)
        ///@}

        /// See PositionConstraintAxis::Sticky.
        ///@{
        TACHYON_LO_BUILDER(StickyX(int16_t threshold = 0), x = PositionConstraintAxis::Sticky(threshold);)
        TACHYON_LO_BUILDER(StickyY(int16_t threshold = 0), y = PositionConstraintAxis::Sticky(threshold);)
        TACHYON_LO_BUILDER(Sticky(int16_t thresholdX = 0, int16_t thresholdY = 0),
                   x = PositionConstraintAxis::Sticky(thresholdX);
                   y = PositionConstraintAxis::Sticky(thresholdY);)
        ///@}

        /// Forces a line break after / before this element, or keeps it on the same line as the
        /// next one.
        ///@{
        TACHYON_LO_BUILDER(BreakAfter(bool v = true),   breakBehaviorOverride = v ? (uint8_t)BreakBehavior::Always + 1 : (uint8_t)BreakBehavior::Auto + 1;)
        TACHYON_LO_BUILDER(BreakBefore(bool v = true),  breakBehaviorOverride = v ? (uint8_t)BreakBehavior::Before + 1 : (uint8_t)BreakBehavior::Auto + 1;)
        TACHYON_LO_BUILDER(KeepWithNext(bool v = true), breakBehaviorOverride = v ? (uint8_t)BreakBehavior::Avoid  + 1 : (uint8_t)BreakBehavior::Auto + 1;)
        ///@}

        /// Overrides the style's flex-grow / flex-shrink.
        ///@{
        TACHYON_LO_BUILDER(FlexGrow(int16_t v),   flex.grow = v;)
        TACHYON_LO_BUILDER(FlexShrink(int16_t v), flex.shrink = v;)
        ///@}
        /// Shorthands for FlexGrow(0) / FlexShrink(0).
        ///@{
        TACHYON_LO_BUILDER(NoGrow(),              flex.grow = 0;)
        TACHYON_LO_BUILDER(NoShrink(),            flex.shrink = 0;)
        ///@}

        /// Overrides the style's icon side / icon-text gap (pixels).
        ///@{
        TACHYON_LO_BUILDER(SetIconSide(IconSide s),       iconSideOverride = s;)
        TACHYON_LO_BUILDER(IconTextGap(int16_t gap),      iconTextGapOverride = gap;)
        ///@}

        /// Overrides the style's scroll clamping (false = unclamped / infinite scroll).
        TACHYON_LO_BUILDER(ClampScroll(bool v = true),    clampScroll = v ? TriState::True : TriState::False;)

        /// Overrides the style's column / row track size.
        ///@{
        TACHYON_LO_BUILDER(ColumnTrack(GridTrack t),                    grid.column.track = t;)
        TACHYON_LO_BUILDER(RowTrack(GridTrack t),                       grid.row.track = t;)
        ///@}
        /// Explicit grid placement: start cell and span.
        ///@{
        TACHYON_LO_BUILDER(GridColumn(int16_t start, int16_t span = 1), grid.column.start = start; grid.column.span = span;)
        TACHYON_LO_BUILDER(GridRow(int16_t start, int16_t span = 1),    grid.row.start = start;    grid.row.span = span;)
        ///@}

        #undef TACHYON_LO_BUILDER

        // --- Resolvers (apply override or fall back to style) ---

        /// Resolvers take the element's layout style (may be null) and return the effective value.
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

        /// Style values with non-Default overrides applied.
        GridProperties GetGridItem(const LayoutStyle* layoutStyle) const
        {
            GridProperties result = layoutStyle ? layoutStyle->layoutData.grid : GridProperties{};
            if (grid.column.track.type != GridTrack::Type::Default) result.column.track = grid.column.track;
            if (grid.row.track.type    != GridTrack::Type::Default) result.row.track    = grid.row.track;
            if (grid.column.start >= 0) { result.column.start = grid.column.start; result.column.span = grid.column.span; }
            if (grid.row.start    >= 0) { result.row.start    = grid.row.start;    result.row.span    = grid.row.span;    }
            return result;
        }
    };

} // namespace tachyon