// GUI Style System: defines the visual and layout properties of elements.
//
// Styles follow a CSS-like box model with margin, padding, and border.
// Each element can have layout styles (sizing, positioning, flex/grid properties)
// and visual styles (colors, gradients, borders, text styling) that vary by
// element state (hover, active, focused, selected, disabled).
//
// Styles are defined in .css theme files and managed by StyleManager/StyleSheet.

#pragma once

#include <gui_core/gui_base_types.h>
#include <gui_core/gui_hash.h>
#include <gui_core/gui_enum_operators.h>
#include <gui_core/style/gui_style_text.h>
#include <gui_core/style/gui_style_gradient.h>
#include <gui_core/graphics/gui_image.h>

namespace interstice
{
    class GradientManager;
    struct GradientHandle;

	// ------------------------------------------------------------------------
	// Style Indices
	// ------------------------------------------------------------------------
    /// Index into StyleManager's color style pool; Invalid (0) draws nothing (transparent).
    enum class VisualColorStyleIndex : uint16_t { Invalid = 0 };


    // ------------------------------------------------------------------------
    // Element State
    // ------------------------------------------------------------------------

    /// Interaction state bits of an element; they pick the Style::states variant it draws with.
    enum class ElementState : uint8_t
    {
        None      = 0,
        Active    = 1 << 0,  ///< being pressed or dragged (CSS: :active)
        Hover     = 1 << 1,  ///< under the mouse (CSS: :hover)
        Focused   = 1 << 2,  ///< has keyboard focus (CSS: :focus)
        Selected  = 1 << 3,  ///< selected item (CSS: :selected)
        Selection = 1 << 4,  ///< text-selection highlight; combines with Active, Hover, Focused, Selected
        Disabled  = 1 << 5,  ///< exclusive: overrides every other visual bit

        VisualMask = Active | Hover | Focused | Selected | Selection | Disabled
    };
    INTERSTICE_ENUM_OPERATORS(ElementState)

    // ------------------------------------------------------------------------
    // Box Model Types
    // ------------------------------------------------------------------------

    /// Per-side values of the box model (margin, padding, border widths).
    struct BoxEdges
    {
        uint16_t top = 0;
        uint16_t right = 0;
        uint16_t bottom = 0;
        uint16_t left = 0;

        constexpr BoxEdges() = default;
        /// All four edges the same.
        constexpr BoxEdges(uint16_t all) : top(all), right(all), bottom(all), left(all) {}
        /// Top/bottom and left/right, like the two-value CSS shorthand.
        constexpr BoxEdges(uint16_t vertical, uint16_t horizontal) 
            : top(vertical), right(horizontal), bottom(vertical), left(horizontal) {}
        /// In CSS order: top, right, bottom, left.
        constexpr BoxEdges(uint16_t t, uint16_t r, uint16_t b, uint16_t l) 
            : top(t), right(r), bottom(b), left(l) {}

        constexpr uint16_t Horizontal() const { return left + right; } ///< left + right
        constexpr uint16_t Vertical() const { return top + bottom; }   ///< top + bottom
        /// All edges are 0.
        constexpr bool IsZero() const { return top == 0 && right == 0 && bottom == 0 && left == 0; }

        bool operator==(const BoxEdges&) const noexcept = default;
    };

    /// Per-corner values (border radius).
    struct BoxCorners
    {
        uint16_t topLeft = 0;
        uint16_t topRight = 0;
        uint16_t bottomRight = 0;
        uint16_t bottomLeft = 0;

        constexpr BoxCorners() = default;
        /// All four corners the same.
        constexpr BoxCorners(uint16_t all)
            : topLeft(all), topRight(all), bottomRight(all), bottomLeft(all) {}
        /// In CSS order: top-left, top-right, bottom-right, bottom-left.
        constexpr BoxCorners(uint16_t tl, uint16_t tr, uint16_t br, uint16_t bl)
            : topLeft(tl), topRight(tr), bottomRight(br), bottomLeft(bl) {}

        /// All corners are 0 (square).
        constexpr bool IsZero() const 
        { 
            return topLeft == 0 && topRight == 0 && bottomRight == 0 && bottomLeft == 0; 
        }

        bool operator==(const BoxCorners&) const noexcept = default;
    };

    /// CSS box-shadow layers per state; the first is drawn on top.
    inline constexpr int kMaxBoxShadows = 2;

    /// One CSS box-shadow layer, outside the border box. A transparent color is no shadow.
    /// Inset is for the focus ring only: an outline `spread` wide inside the border box.
    struct BoxShadow
    {
        int16_t  x      = 0;     ///< horizontal offset in CSS px
        int16_t  y      = 0;     ///< vertical offset in CSS px
        uint16_t blur   = 0;     ///< blur radius in CSS px
        int16_t  spread = 0;     ///< CSS px the shadow grows by (shrinks when negative)
        color4b  color  {};      ///< transparent = no shadow
        bool     inset  = false; ///< an outline inside the border box; :focus-visible only

        /// Transparent: no shadow.
        constexpr bool IsNone() const { return color.a == 0; }
        bool operator==(const BoxShadow&) const noexcept = default;
    };

    /// Per-side offsets of a positioned element (CSS: top, right, bottom, left), in pixels.
    struct EdgeInset
    {
        /// "Not set". Insets can be negative, so the sentinel is the lowest int16_t.
        static constexpr int16_t kAuto = std::numeric_limits<int16_t>::min();

        int16_t top    = kAuto;
        int16_t right  = kAuto;
        int16_t bottom = kAuto;
        int16_t left   = kAuto;

        constexpr EdgeInset() = default;
        /// All four insets the same.
        constexpr EdgeInset(int16_t all) : top(all), right(all), bottom(all), left(all) {}
        /// Top/bottom and left/right.
        constexpr EdgeInset(int16_t vertical, int16_t horizontal)
            : top(vertical), right(horizontal), bottom(vertical), left(horizontal) {}
        /// In CSS order: top, right, bottom, left.
        constexpr EdgeInset(int16_t t, int16_t r, int16_t b, int16_t l)
            : top(t), right(r), bottom(b), left(l) {}

        bool operator==(const EdgeInset&) const noexcept = default;
    };

    // ------------------------------------------------------------------------
    // Layout Properties
    // ------------------------------------------------------------------------

    /// Main axis of a flex container (CSS: flex-direction).
    enum class FlexDirection : uint8_t
    {
        Row,            ///< Horizontal, left to right
        RowReverse,     ///< Horizontal, right to left
        Column,         ///< Vertical, top to bottom
        ColumnReverse   ///< Vertical, bottom to top
    };

    /// Whether flex items wrap onto more lines (CSS: flex-wrap).
    enum class FlexWrap : uint8_t
    {
        NoWrap,         ///< Single line
        Wrap,           ///< Multi-line
        WrapReverse     ///< Multi-line, reversed
    };

    /// Cross-axis alignment of items (CSS: align-items, align-self).
    enum class AlignItems : uint8_t
    {
        Auto,           ///< align-self only: inherit from container's align-items
        FlexStart,      ///< Align to start of cross axis
        FlexEnd,        ///< Align to end of cross axis
        Center,         ///< Center on cross axis
        Stretch         ///< Stretch to fill container
    };

    /// Main-axis distribution of items (CSS: justify-content; grids also justify-items).
    enum class JustifyContent : uint8_t
    {
        FlexStart,      ///< Pack to start
        FlexEnd,        ///< Pack to end
        Center,         ///< Pack to center
        SpaceBetween,   ///< Space between items
        SpaceAround,    ///< Space around items
        SpaceEvenly     ///< Equal space between all items
    };

    /// Distributes cross-axis space between wrap lines (align-items aligns within a line).
    /// Only applies when flex-wrap != NoWrap.
    enum class AlignContent : uint8_t
    {
        FlexStart,      ///< Pack lines to start (default here; no stretching)
        FlexEnd,        ///< Pack lines to end
        Center,         ///< Pack lines to center
        Stretch,        ///< Lines grow to fill the container's cross axis
        SpaceBetween,   ///< Space between lines
        SpaceAround,    ///< Space around lines
        SpaceEvenly     ///< Equal space between all lines
    };

    /// How an element is positioned; modeled on CSS position, but set in code, not CSS.
    enum class PositionType : uint8_t
    {
        Relative,   ///< in flow, offset from its natural position
        Anchored,   ///< placed relative to another element (AnchorPlacement)
        Absolute,   ///< out of flow in content space; scrolls with the parent
        Fixed,      ///< out of flow in overlay space; doesn't scroll with the parent
		Centered,   ///< centered in the parent
		Sticky      ///< in flow like Relative, clamped to the parent's scroll viewport edge
    };

    /// How an element lays out its children (CSS: display).
    enum class Display : uint8_t
    {
        Flex,       ///< Flexbox container
        Grid,       ///< Grid container
        None        ///< Not rendered, removed from layout
    };

    /// What happens to content larger than the box (CSS: overflow, overflow-x, overflow-y).
    enum class Overflow : uint8_t
    {
        Auto,       ///< Content clipped, scrollbars shown when needed
        Hidden,     ///< Content clipped, no scrollbars
        Visible     ///< Content not clipped
    };

    /// CSS: visibility.
    enum class Visibility : uint8_t
    {
        Visible,    ///< Rendered normally
        Collapsed   ///< Not rendered, removed from layout
    };

    /// Where an element's icon sits relative to its text (CSS: -interstice-icon-side).
    enum class IconSide : uint8_t
    {
        None = 0,

        Left,
        Right,
        Top,
        Bottom
    };

    /// Where an anchored element sits relative to its target: one horizontal and one
    /// vertical AnchorPlacementAxis value, one per nibble. Outside = beyond the target's
    /// edge, Inside = within it, Center* = centered on that edge.
    enum class AnchorPlacement : uint8_t
    {
        None = 0,

        // Horizontal
        OutsideLeft    = (1 << 0),
        CenterLeft     = (2 << 0),
        InsideLeft     = (3 << 0),
        CenterHorz     = (4 << 0),
        InsideRight    = (5 << 0),
        CenterRight    = (6 << 0),
        OutsideRight   = (7 << 0),

        HorizontalMask = (0xF << 0),

        // Vertical
        OutsideTop     = (1 << 4),
        CenterTop      = (2 << 4),
        InsideTop      = (3 << 4),
        CenterVert     = (4 << 4),
        InsideBottom   = (5 << 4),
        CenterBottom   = (6 << 4),
        OutsideBottom  = (7 << 4),
                       
        VerticalMask   = (0xF << 4),

        // Common combinations
        CenterCenter      = CenterHorz | CenterVert,
		
        InsideTopLeft      = InsideTop    | InsideLeft,
        InsideTopRight     = InsideTop    | InsideRight,
        InsideBottomLeft   = InsideBottom | InsideLeft,
        InsideBottomRight  = InsideBottom | InsideRight,

		OutsideTopLeft     = OutsideTop    | OutsideLeft,
        OutsideTopRight    = OutsideTop    | OutsideRight,
		OutsideBottomLeft  = OutsideBottom | OutsideLeft,
        OutsideBottomRight = OutsideBottom | OutsideRight,
    };
    INTERSTICE_ENUM_OPERATORS(AnchorPlacement);

    /// Anchor placement along one axis; the 4-bit value of one AnchorPlacement nibble.
    enum class AnchorPlacementAxis : uint8_t
    {
        None       = 0,
        OutsideMin = 1, ///< beyond the near edge (left / top)
        CenterMin  = 2, ///< centered on the near edge
        InsideMin  = 3, ///< inside, against the near edge
        Center     = 4, ///< centered in the target
        InsideMax  = 5, ///< inside, against the far edge (right / bottom)
        CenterMax  = 6, ///< centered on the far edge
        OutsideMax = 7, ///< beyond the far edge (right / bottom)
    };

    /// Mirrors a placement: Min <-> Max; Center and None are unchanged.
    constexpr AnchorPlacementAxis FlipAxis(AnchorPlacementAxis p)
    {
        switch (p)
        {
            case AnchorPlacementAxis::OutsideMin: return AnchorPlacementAxis::OutsideMax;
            case AnchorPlacementAxis::OutsideMax: return AnchorPlacementAxis::OutsideMin;
            case AnchorPlacementAxis::InsideMin:  return AnchorPlacementAxis::InsideMax;
            case AnchorPlacementAxis::InsideMax:  return AnchorPlacementAxis::InsideMin;
            case AnchorPlacementAxis::CenterMin:  return AnchorPlacementAxis::CenterMax;
            case AnchorPlacementAxis::CenterMax:  return AnchorPlacementAxis::CenterMin;
            default: return p;
        }
    }

    /// Horizontal part of an AnchorPlacement.
    constexpr AnchorPlacementAxis GetHorizontalAxis(AnchorPlacement p) { return (AnchorPlacementAxis)((uint8_t)p & 0x0F); }
    /// Vertical part of an AnchorPlacement.
    constexpr AnchorPlacementAxis GetVerticalAxis(AnchorPlacement p)   { return (AnchorPlacementAxis)(((uint8_t)p >> 4) & 0x0F); }

    /// Combines a horizontal and a vertical placement.
    constexpr AnchorPlacement MakeAnchorPlacement(AnchorPlacementAxis h, AnchorPlacementAxis v)
    {
        return (AnchorPlacement)((uint8_t)h | ((uint8_t)v << 4));
    }


    // ------------------------------------------------------------------------
    // Flex Container Properties
    // ------------------------------------------------------------------------

    /// Flexbox properties: the container's (direction, wrap, alignment, gap) and the
    /// item's own (grow, shrink, basis). Lengths are in CSS px in a Style.
    struct FlexProperties
    {
        static constexpr int16_t kAuto = -1; ///< flexBasis 'auto'

        uint16_t flexGrow = 0;   ///< CSS: flex-grow (whole numbers)
        uint16_t flexShrink = 0; ///< CSS: flex-shrink (whole numbers)
        int16_t  flexBasis = kAuto; ///< CSS: flex-basis; kAuto = auto

        uint16_t gap = 0;  ///< CSS: gap, between flex items

        FlexDirection  direction      : 2 = FlexDirection::Row;          ///< CSS: flex-direction
        FlexWrap       wrap           : 2 = FlexWrap::NoWrap;            ///< CSS: flex-wrap
        AlignItems     alignItems     : 3 = AlignItems::FlexStart;       ///< CSS: align-items
        JustifyContent justifyContent : 3 = JustifyContent::FlexStart;  ///< CSS: justify-content
        AlignContent   alignContent   : 3 = AlignContent::FlexStart;     ///< CSS: align-content

        bool operator==(const FlexProperties&) const noexcept = default;
    };

    // ------------------------------------------------------------------------
    // Grid Track Definition
    // ------------------------------------------------------------------------

    /// Size of one grid column or row (CSS: -interstice-grid-column-track / -row-track).
    struct GridTrack
    {
        /// How value is read.
        enum class Type : uint8_t
        {
            Fixed,      ///< value is in pixels (e.g., 100.0f)
            Fractional, ///< fractional unit (e.g., 1.0f = 1fr)
            Auto,       ///< fits content
            Default,    ///< defers to the element's style (only valid in LayoutOverride)
        };

        Type  type = Type::Auto;
        float value = 1.0f;  ///< Meaning depends on type: pixels for Fixed, fraction for Fractional
        /// Fractional tracks only: size = max(minPx, content) + fractional share of free space.
        float minPx = 0.0f;

        /// Fits its content.
        static constexpr GridTrack Auto()                  { return { .type = Type::Auto,       .value = 1.0f  }; }
        /// `value` pixels (CSS px in a Style).
        static constexpr GridTrack Fixed(float value)      { return { .type = Type::Fixed,      .value = value }; }
        /// `value` fr of the free space, on top of max(minPx, content).
        static constexpr GridTrack Fractional(float value, float minPx = 0.0f)
                                                           { return { .type = Type::Fractional, .value = value, .minPx = minPx }; }
        /// Defers to the element's style (LayoutOverride only).
        static constexpr GridTrack Default()               { return { .type = Type::Default,    .value = 0.0f  }; }

        bool operator==(const GridTrack& other) const noexcept = default;
    };

    // ------------------------------------------------------------------------
    // Grid Container Properties
    // ------------------------------------------------------------------------
    
    /// A grid item's placement and track request along one axis, plus the container's gap.
    struct GridAxisProperties
    {
        static constexpr int16_t kAuto = -1; ///< start: auto-place

        /// Size the item asks for its column / row; across the items in a track,
        /// Fixed beats Fractional beats Auto, and the larger value wins.
        GridTrack track = GridTrack::Default();

        int16_t start{ kAuto }; ///< first track the item covers; kAuto = auto-place
        int16_t span{ 1 }; ///< tracks the item covers
        int16_t gap{ 0 };  ///< container: space between tracks (CSS: column-gap / row-gap)

        bool operator==(const GridAxisProperties&) const noexcept = default;
    };

    /// Grid container and item properties.
    struct GridProperties
    {
        GridAxisProperties column{}; ///< CSS: grid-column, column-gap
        GridAxisProperties row{};    ///< CSS: grid-row, row-gap

        JustifyContent justifyContent = JustifyContent::FlexStart;    ///< CSS: justify-content
        AlignItems     alignItems     = AlignItems::Stretch;          ///< CSS: align-items
        JustifyContent justifyItems   = JustifyContent::FlexStart;    ///< CSS: justify-items, when alignItemsSelf isn't Stretch
        /// Stretch: items fill their cell's width (CSS: justify-items: stretch).
        AlignItems     alignItemsSelf = AlignItems::Stretch;

        bool operator==(const GridProperties&) const noexcept = default;
    };

    // ------------------------------------------------------------------------
    // Container for layout type specific properties
    // ------------------------------------------------------------------------

    /// Flex / grid properties and CSS sizes of a style. Sizes are in CSS px in a Style.
    struct LayoutData
    {
        FlexProperties flex {}; ///< used when display is Flex (and by flex items)
        GridProperties grid {}; ///< used when display is Grid (and by grid items)

        static constexpr int32_t kAutoSize     = -1; ///< width / height 'auto'
        static constexpr int32_t kUnboundedMax = std::numeric_limits<int32_t>::max(); ///< no max-width / max-height

        int32_t width     = kAutoSize;     ///< CSS: width
        int32_t height    = kAutoSize;     ///< CSS: height
        int32_t minWidth  = 0;             ///< CSS: min-width
        int32_t maxWidth  = kUnboundedMax; ///< CSS: max-width
        int32_t minHeight = 0;             ///< CSS: min-height
        int32_t maxHeight = kUnboundedMax; ///< CSS: max-height

        bool operator==(const LayoutData&) const noexcept = default;
    };

    // ------------------------------------------------------------------------
    // Break Behavior (CSS break-after: auto | always | avoid | left | right)
    // ------------------------------------------------------------------------

    /// Line breaks around an item in a wrapping flex container (CSS: break-after).
    enum class BreakBehavior : uint8_t
    {
        Auto,        ///< No forced breaks, no keep-with-next
        Always,      ///< Force a line break after this element (CSS: always / right)
        Before,      ///< Force a line break before this element (CSS: left)
        Avoid,       ///< Prevent a line break between this and the next element (CSS: avoid)
    };

    // ------------------------------------------------------------------------
    // Mouse Cursor Types
    // ------------------------------------------------------------------------

    /// Mouse cursor shown over an element (CSS: cursor).
    enum class MouseCursor : uint8_t
    {
        Default,        ///< Default arrow cursor
        Text,           ///< Text editing I-beam
        Wait,           ///< Wait/busy cursor
        Crosshair,      ///< Crosshair for precision selection
        Pointer,        ///< Pointing hand for clickable items
        ColResize,      ///< CSS col-resize: edge of a column being moved (left/right)
        RowResize,      ///< CSS row-resize: edge of a row being moved (up/down)
        EwResize,       ///< CSS ew-resize:  bidirectional east-west arrow (left/right movement)
        NsResize,       ///< CSS ns-resize:  bidirectional north-south arrow (up/down movement)
        NwseResize,     ///< CSS nwse-resize: diagonal (northwest-southeast)
        NeswResize,     ///< CSS nesw-resize: diagonal (northeast-southwest)
        Move,           ///< CSS move:       4-way arrow (free movement)
        NotAllowed,     ///< CSS not-allowed

        None,           ///< No cursor (invisible)
    };

    // ------------------------------------------------------------------------
    // Color Styles
    // ------------------------------------------------------------------------

    /// How a ColorStyle paints a shape. Gradient types sample the gradient ramp
    /// along a coordinate that the style's transformation and animSpeed move.
    enum class ColorType : uint16_t
    {
        SolidColor               = 0, ///< the first stop's color
        StretchedTexture         = 1, ///< Style::textureIndex stretched over the box
        RepeatedTexture          = 2, ///< Style::textureIndex tiled at its pixel size
        LinearGradient           = 3, ///< ramp along an axis (CSS: linear-gradient)
        CircularAngularGradient  = 4, ///< ramp by distance from the center
        EllipsoidAngularGradient = 5, ///< ramp by distance from the center, relative to the box size
        RadialGradient           = 6, ///< ramp by angle around the center (CSS: radial-gradient)
        InterpolatedTriangle     = 7, ///< three colors blended by vertex weights
        InterpolatedBox          = 8, ///< four corner colors blended across the box
        SDFGradient              = 9  ///< ramp by distance from the shape's edge
    };

    class VisualColorStyle;
    /// A fill: solid color, gradient or texture, as used for backgrounds, borders,
    /// text and icons. Register it with StyleManager (inside a Style, or with
    /// RegisterColorStyle) to get the GPU-side VisualColorStyle.
    class ColorStyle
    {
    public:
        static constexpr int16_t one = 15360; ///< 1.0 as a half float

        Gradient  gradient{};                         ///< colors; SolidColor uses the first stop
        short4    transformationX = { one, 0, 0, 0 }; ///< first row of the 2x3 gradient transform (half floats)
        short4    transformationY = { 0, one, 0, 0 }; ///< second row of the 2x3 gradient transform (half floats)
        ColorType type            = (ColorType)0; ///< how the fill is painted
        short2    animSpeed       = { 0, 0 }; ///< half floats, see SetAnimSpeed

        /// Sets the 2x3 transform applied to gradient coordinates, row by row.
        void SetTransformation(float3 x, float3 y);
        /// Scroll speed over time: x along the gradient (or texture U), y texture V.
        void SetAnimSpeed(float x, float y = 0.0f);
        /// The 2x3 gradient transform as floats.
        float3x2 GetTransformation() const;

        /// Bakes the GPU-facing style and acquires its atlas slot. The caller owns
        /// outHandle and must Release it (or hand it to RegisterVisualColorStyle).
        /// Texture types return an invalid handle.
        VisualColorStyle ToVisualStyle(GradientManager& gradientManager,
                                       ImageIndex textureIndex,
                                       GradientHandle& outHandle) const;


        /// A SolidColor style.
        static ColorStyle Create(color4b color);
        /// A SolidColor style.
        static ColorStyle Create(color4f color);
        /// Two stops at 0 and 1. The color overloads build the gradient like
        /// Gradient::Linear; the type can't be SolidColor or one that takes another
        /// color count (InterpolatedTriangle takes 3, InterpolatedBox 4).
        static ColorStyle Create(color4b c0, color4b c1, ColorType type);
        /// Two stops at 0 and 1.
        static ColorStyle Create(color4f c0, color4f c1, ColorType type);
        /// Three stops at 0, 0.5 and 1.
        static ColorStyle Create(color4b c0, color4b c1, color4b c2, ColorType type);
        /// Three stops at 0, 0.5 and 1.
        static ColorStyle Create(color4f c0, color4f c1, color4f c2, ColorType type);
        /// Four stops at 0, 0.25, 0.5 and 1.
        static ColorStyle Create(color4b c0, color4b c1, color4b c2, color4b c3, ColorType type);
        /// Four stops at 0, 0.25, 0.5 and 1.
        static ColorStyle Create(color4f c0, color4f c1, color4f c2, color4f c3, ColorType type);
        /// Any gradient with any type.
        static ColorStyle Create(Gradient gradient, ColorType type);
    };

    /// GPU form of a ColorStyle, uploaded as is: the gradient is baked into an atlas
    /// slot that colorUV / gradientPage point at. StyleManager owns the atlas slot of
    /// registered ones.
    class alignas(16) VisualColorStyle
    {
    public:
        static constexpr int16_t one = 15360; ///< 1.0 as a half float

        short4     transformationX = { one, 0, 0, 0 }; ///< as ColorStyle, with the gradient angle applied
        short4     transformationY = { 0, one, 0, 0 }; ///< as ColorStyle, with the gradient angle applied

        ushort2    colorUV         = {}; ///< texel of the gradient in its atlas page
        ColorType  type            = (ColorType)0;

        /// Logical gradient atlas page (0..GradientManager::PageCount()-1) that colorUV is in;
        /// the renderer maps it to its bindless image slot when uploading.
        uint16_t   gradientPage    = 0;

        ImageIndex textureIndex    = ImageIndex::Invalid; ///< texture of the texture types

        short2     animSpeed      = { 0, 0 }; ///< half floats, see ColorStyle::SetAnimSpeed

        /// See ColorStyle::SetTransformation.
        void SetTransformation(float3 x, float3 y);
        /// See ColorStyle::SetAnimSpeed.
        void SetAnimSpeed(float x, float y = 0.0f);

        uint64_t GetHashCode() const noexcept
        {
            return Hash::Combine(Hash::Calculate(transformationX),
                                 Hash::Calculate(transformationY),
                                 Hash::Calculate(textureIndex),
                                 Hash::Calculate(colorUV),
                                 Hash::Calculate(gradientPage),
                                 Hash::Calculate(type),
                                 Hash::Calculate(animSpeed));
        }

        bool operator==(const VisualColorStyle& other) const noexcept
        {
            return transformationX == other.transformationX &&
                   transformationY == other.transformationY &&
                   textureIndex == other.textureIndex &&
                   colorUV == other.colorUV &&
                   gradientPage == other.gradientPage &&
                   type == other.type &&
                   animSpeed == other.animSpeed;
        }

        /// @name Factories
        /// As the ColorStyle::Create overloads, baked: each acquires a gradient atlas slot
        /// from `gradientManager` and returns it in `outHandle`, which the caller owns.
        ///@{
        static VisualColorStyle Create(GradientManager& gradientManager, color4b color, GradientHandle& outHandle);
        static VisualColorStyle Create(GradientManager& gradientManager, color4f color, GradientHandle& outHandle);
        static VisualColorStyle Create(GradientManager& gradientManager, color4b color1, color4b color2, ColorType type, GradientHandle& outHandle);
        static VisualColorStyle Create(GradientManager& gradientManager, color4f color1, color4f color2, ColorType type, GradientHandle& outHandle);
        static VisualColorStyle Create(GradientManager& gradientManager, color4b color1, color4b color2, color4b color3, ColorType type, GradientHandle& outHandle);
        static VisualColorStyle Create(GradientManager& gradientManager, color4f color1, color4f color2, color4f color3, ColorType type, GradientHandle& outHandle);
        static VisualColorStyle Create(GradientManager& gradientManager, color4b color1, color4b color2, color4b color3, color4b color4, ColorType type, GradientHandle& outHandle);
        static VisualColorStyle Create(GradientManager& gradientManager, color4f color1, color4f color2, color4f color3, color4f color4, ColorType type, GradientHandle& outHandle);
        static VisualColorStyle Create(GradientManager& gradientManager, Gradient gradient, ColorType type, GradientHandle& outHandle);
        ///@}
    };


    // ------------------------------------------------------------------------
    // Complete Style Definition
    // ------------------------------------------------------------------------

    /// The visual properties of one element state (normal, :hover, :active, ...).
    /// Lengths are in CSS px.
    struct StyleState
    {
        TextStyle textStyle {}; ///< font and text metrics

        ColorStyle backgroundColor {};  ///< CSS: background
        ColorStyle borderColors[4] {};  ///< [0]=top, [1]=right, [2]=bottom, [3]=left
        ColorStyle textColor {};        ///< CSS: color; when it has no stops, textStyle.color is used
        ColorStyle iconColor {};        ///< -interstice-icon-color; none = the text color
        BoxShadow  boxShadow[kMaxBoxShadows] {}; ///< CSS: box-shadow; the first is drawn on top

        /// Sets all four border colors to `c`.
        void SetBorderColor(const ColorStyle& c) { borderColors[0] = borderColors[1] = borderColors[2] = borderColors[3] = c; }
        /// Per-edge border colors, in CSS order.
        void SetBorderColor(const ColorStyle& t, const ColorStyle& r, const ColorStyle& b, const ColorStyle& l)
        { 
            borderColors[0] = t; borderColors[1] = r; borderColors[2] = b; borderColors[3] = l;
        }
    };

    /// A complete style, as parsed from CSS or built in code: layout properties plus the
    /// visual properties of each state, in CSS px. StyleManager::Register bakes it into a
    /// LayoutStyle, VisualBoxStyle and VisualTextStyle at the UI scale.
    struct Style
    {
        static constexpr int16_t kAuto = -1;

        /// @name State slots
        /// Indices into states: a combination of the ElementState bits Active, Hover,
        /// Focused, Selected and Selection; Disabled has a slot of its own.
        ///@{
        static constexpr int kNormal    = 0;
        static constexpr int kActive    = 1;
        static constexpr int kHover     = 2;
        static constexpr int kFocus     = 4;
        static constexpr int kSelected  = 8;
        static constexpr int kSelection = 16; ///< text-selection highlight; combines with the bits above (16..31)
        static constexpr int kDisabled  = 32; ///< exclusive: slot 32 only, never combined

        static constexpr int kStateCount = kDisabled + 1;
        ///@}

        StyleState      states[kStateCount] {}; ///< one per state slot (kNormal ... kDisabled)
        TextLayoutStyle textLayoutStyle {};     ///< shared by all states

        BoxEdges        margin{};       ///< CSS px
        BoxEdges        padding{};      ///< CSS px
        BoxEdges        borderWidths{}; ///< CSS px
        BoxCorners      borderRadius{}; ///< CSS px
        float           borderDashSize   = 0.0f; ///< dash length in CSS px; 0 = solid
        float           borderDashOffset = 0.0f; ///< offset into the dash pattern, CSS px
        BoxShadow       focusRing {};            ///< :focus-visible box-shadow, over the state's shadows

        LayoutData      layoutData {}; ///< flex, grid and size properties

        uint16_t        iconTextGap = 0; ///< space between icon and text, CSS px
        uint16_t        iconSize    = 0; ///< icon size in CSS px; 0 = its natural size
        int16_t         iconOffsetY = 0; ///< vertical nudge of the icon in CSS px; positive = up

        ImageIndex      textureIndex = ImageIndex::Invalid; ///< texture of the texture color types
        Display         display      : 3 = Display::Flex;       ///< CSS: display
        Visibility      visibility   : 2 = Visibility::Visible; ///< CSS: visibility
        Overflow        overflowX    : 2 = Overflow::Auto;      ///< CSS: overflow-x
        Overflow        overflowY    : 2 = Overflow::Auto;      ///< CSS: overflow-y
        IconSide        iconSide     : 3 = IconSide::Left;      ///< CSS: -interstice-icon-side
        AlignItems      alignSelf      : 3 = AlignItems::Auto; ///< CSS: align-self; Auto = the container's align-items
        BreakBehavior   breakBehavior  : 2 = BreakBehavior::Auto; ///< CSS: break-after
        bool            clampScroll    : 1 = true;  ///< clamp the scroll offset to the content; false = unbounded

        /// CSS `overflow` shorthand: sets both axes.
        constexpr void SetOverflow(Overflow o) noexcept { overflowX = o; overflowY = o; }

        MouseCursor     cursor = MouseCursor::Default; ///< CSS: cursor
    };


    // ------------------------------------------------------------------------
    // Optimized Runtime Styles
    // ------------------------------------------------------------------------

    /// The layout part of a registered Style, in pixels; shared by every style with the same
    /// values. StyleManager::GetLayoutStyle returns it.
    struct LayoutStyle
    {
        static constexpr int16_t kAuto = -1;

        BoxEdges        margin{};       ///< pixels
        BoxEdges        padding{};      ///< pixels
        BoxEdges        borderWidths{}; ///< pixels

        LayoutData      layoutData{};   ///< flex, grid and size properties, in pixels

        uint16_t        iconTextGap = 0; ///< space between icon and text, pixels
        uint16_t        iconSize    = 0; ///< icon size in pixels; 0 = its natural size
        int16_t         iconOffsetY = 0; ///< vertical nudge of an icon-only element in pixels; positive = up

        // Display
        Display         display      : 3 = Display::Flex;          ///< see Style::display
        Visibility      visibility   : 2 = Visibility::Visible;    ///< see Style::visibility
        Overflow        overflowX    : 2 = Overflow::Visible;      ///< see Style::overflowX
        Overflow        overflowY    : 2 = Overflow::Visible;      ///< see Style::overflowY
        IconSide        iconSide     : 3 = IconSide::Left;         ///< see Style::iconSide
        AlignItems      alignSelf      : 3 = AlignItems::Auto; ///< see Style::alignSelf
        BreakBehavior   breakBehavior  : 2 = BreakBehavior::Auto;  ///< see Style::breakBehavior
        bool            clampScroll    : 1 = true;  ///< see Style::clampScroll

        MouseCursor     cursor = MouseCursor::Default; ///< see Style::cursor

        /// Hash of a subset of the fields, for deduplication; operator== settles collisions.
        uint64_t GetHashCode() const noexcept
        {
            const auto& ld = layoutData;
            return Hash::Combine(margin, padding, borderWidths,
                                 ld.width, ld.height, ld.minWidth, ld.maxWidth, ld.minHeight, ld.maxHeight,
                                 iconTextGap, iconSize, iconOffsetY,
                                 uint8_t(display), uint8_t(visibility), uint8_t(overflowX), uint8_t(overflowY),
                                 uint8_t(alignSelf), cursor);
        }

        bool operator==(const LayoutStyle &other) const noexcept = default;

        /// Takes part in layout: not display: none and not collapsed.
        constexpr bool IsInFlow() const
        {
            return display != Display::None && 
                   visibility != Visibility::Collapsed;
        }

        /// display is Flex.
        constexpr bool IsFlexContainer() const
        {
            return display == Display::Flex;
        }

        /// Horizontal margin + border + padding: the narrowest the element can be.
        constexpr int16_t GetMinWidth() const
        {
            return margin.Horizontal() + borderWidths.Horizontal() + padding.Horizontal();
        }

        /// Vertical margin + border + padding: the shortest the element can be.
        constexpr int16_t GetMinHeight() const
        {
            return margin.Vertical() + borderWidths.Vertical() + padding.Vertical();
        }

        /// GetMinWidth and GetMinHeight together.
        constexpr short2 GetMinSize() const 
        { 
            return { GetMinWidth(), GetMinHeight() }; 
        }

        /// The content box of a border-box rect: `borderBox` minus border and padding.
        IntRect GetPaddingBoxContentRect(const IntRect& borderBox) const
        {
            int16_t left   = borderWidths.left   + padding.left;
            int16_t right  = borderWidths.right  + padding.right;
            int16_t top    = borderWidths.top    + padding.top;
            int16_t bottom = borderWidths.bottom + padding.bottom;

            int16_t w = std::max<int16_t>(0, static_cast<uint16_t>(borderBox.w - static_cast<uint32_t>(left + right)));
            int16_t h = std::max<int16_t>(0, static_cast<uint16_t>(borderBox.h - static_cast<uint32_t>(top + bottom)));

            return IntRect
            {
                int16_t(borderBox.x + left),
                int16_t(borderBox.y + top),
                w, h
            };
        }

        /// The content box of a margin-box rect: `outer` minus margin, border and padding.
        IntRect GetContentRect(const IntRect& outer) const
        {
            int16_t left   = margin.left   + borderWidths.left   + padding.left;
            int16_t right  = margin.right  + borderWidths.right  + padding.right;
            int16_t top    = margin.top    + borderWidths.top    + padding.top;
            int16_t bottom = margin.bottom + borderWidths.bottom + padding.bottom;

            int16_t w = std::max<int16_t>(0, static_cast<int16_t>(outer.w - static_cast<int32_t>(left + right)));
            int16_t h = std::max<int16_t>(0, static_cast<int16_t>(outer.h - static_cast<int32_t>(top + bottom)));

            return IntRect
            { 
                int16_t(outer.x + left), 
                int16_t(outer.y + top), 
                w, h 
            };
        }

        /// The border box of a margin-box rect: `outer` minus margin.
        IntRect GetBorderRect(const IntRect& outer) const
        {
            int16_t w = std::max<int16_t>(0, static_cast<int16_t>(outer.w - margin.Horizontal()));
            int16_t h = std::max<int16_t>(0, static_cast<int16_t>(outer.h - margin.Vertical()));

            return IntRect
            { 
                int16_t(outer.x + margin.left), 
                int16_t(outer.y + margin.top), 
                w, h 
            };
        }

        /// The padding box of a margin-box rect: `outer` minus margin and border.
        IntRect GetPaddingRect(const IntRect& outer) const
        {
            int16_t l = margin.left   + borderWidths.left;
            int16_t r = margin.right  + borderWidths.right;
            int16_t t = margin.top    + borderWidths.top;
            int16_t b = margin.bottom + borderWidths.bottom;

            int16_t w = std::max<int16_t>(0, static_cast<int16_t>(outer.w - static_cast<int32_t>(l + r)));
            int16_t h = std::max<int16_t>(0, static_cast<int16_t>(outer.h - static_cast<int32_t>(t + b)));

            return IntRect
            { 
                int16_t(outer.x + l), 
                int16_t(outer.y + t), 
                w, h 
            };
        }

        /// Horizontal padding plus border widths, in pixels.
        inline int32_t GetBorderBoxWidth() const noexcept
        {
            return (int32_t)(padding.Horizontal() + borderWidths.Horizontal());
        }
    };

    /// A BoxShadow in pixels with its color registered. No color is no shadow.
    struct VisualShadow
    {
        int16_t               x      = 0; ///< see BoxShadow, in pixels
        int16_t               y      = 0;
        uint16_t              blur   = 0;
        int16_t               spread = 0;
        VisualColorStyleIndex color  = VisualColorStyleIndex::Invalid;

        /// No color: no shadow.
        constexpr bool IsNone() const { return color == VisualColorStyleIndex::Invalid; }
        bool operator==(const VisualShadow&) const noexcept = default;
    };

    /// A state's shadows; the ring is drawn only while focus is from the keyboard, outside
    /// the box or, for an inset ring, as a border-only box style over it.
    struct VisualShadowStyle
    {
        VisualShadow layers[kMaxBoxShadows] {}; ///< the state's box-shadow layers
        VisualShadow ring {};                   ///< the focus ring (Style::focusRing)
        uint32_t     insetRing = 0; ///< 1 + flat GPU index of the outline's box style, 0 = none

        bool operator==(const VisualShadowStyle&) const noexcept = default;
    };

    /// The box part of one state of a registered Style, in pixels; uploaded to the GPU
    /// as is (32 bytes).
    struct alignas(16) VisualBoxStyle
    {
        BoxEdges   borderWidths{};                                          ///< pixels
        BoxCorners borderRadius{};                                          ///< pixels

        /// Per-edge border colors: [0]=top, [1]=right, [2]=bottom, [3]=left
        VisualColorStyleIndex borderColorIndex[4] = {
            VisualColorStyleIndex::Invalid, VisualColorStyleIndex::Invalid,
            VisualColorStyleIndex::Invalid, VisualColorStyleIndex::Invalid
        };                                                                  // 8 bytes
        VisualColorStyleIndex backgroundColorIndex = VisualColorStyleIndex::Invalid; ///< background color
        uint16_t borderDashSize   = 0;                                     ///< dash length in pixels; 0 = solid
        uint16_t borderDashOffset = 0;                                     ///< offset into the dash pattern, pixels
        /// 1-based index of the state's VisualShadowStyle, 0 = none; read on the CPU only.
        uint16_t shadowIndex      = 0;
        // Total: 32 bytes

        /// Hash for deduplication.
        uint64_t GetHashCode() const noexcept
        {
            return Hash::Combine(Hash::Calculate(borderWidths),
                                 Hash::Calculate(borderRadius),
                                 Hash::Calculate(borderColorIndex[0]),
                                 Hash::Calculate(borderColorIndex[1]),
                                 Hash::Calculate(borderColorIndex[2]),
                                 Hash::Calculate(borderColorIndex[3]),
                                 Hash::Calculate(backgroundColorIndex),
                                 Hash::Calculate(borderDashSize),
                                 Hash::Calculate(borderDashOffset),
                                 Hash::Calculate(shadowIndex));
        }

        bool operator==(const VisualBoxStyle& other) const noexcept
        {
            return borderWidths == other.borderWidths &&
                   borderRadius == other.borderRadius &&
                   borderColorIndex[0] == other.borderColorIndex[0] &&
                   borderColorIndex[1] == other.borderColorIndex[1] &&
                   borderColorIndex[2] == other.borderColorIndex[2] &&
                   borderColorIndex[3] == other.borderColorIndex[3] &&
                   backgroundColorIndex == other.backgroundColorIndex &&
                   borderDashSize   == other.borderDashSize &&
                   borderDashOffset == other.borderDashOffset &&
                   shadowIndex      == other.shadowIndex;
        }
    };

    /// The text part of one state of a registered Style, in pixels.
    struct VisualTextStyle
    {
        TextStyle             textStyle {};       ///< in pixels
        TextLayoutStyle       textLayoutStyle {}; ///< shared by all states
        VisualColorStyleIndex textColorIndex = VisualColorStyleIndex::Invalid; ///< registered text color
        VisualColorStyleIndex iconColorIndex = VisualColorStyleIndex::Invalid; ///< the text color unless set
        uint16_t              iconTextGap = 0; ///< space between icon and text, pixels
        uint16_t              iconSize    = 0; ///< icon size in pixels; 0 = its natural size
        int16_t               iconOffsetY = 0; ///< vertical nudge of the icon in pixels; positive = up
        IconSide              iconSide = IconSide::Left; ///< see Style::iconSide

        uint64_t GetHashCode() const noexcept
        {
            return Hash::Combine(textStyle.GetHashCode(),
                                 textLayoutStyle.GetHashCode(),
                                 Hash::Calculate(textColorIndex),
                                 Hash::Calculate(iconColorIndex),
                                 Hash::Calculate(iconTextGap),
                                 Hash::Calculate(iconSize),
                                 Hash::Calculate(iconOffsetY),
                                 Hash::Calculate(iconSide));
        }

        bool operator==(const VisualTextStyle& other) const noexcept
        {
            return textStyle         == other.textStyle &&
                   textLayoutStyle   == other.textLayoutStyle &&
                   textColorIndex    == other.textColorIndex &&
                   iconColorIndex    == other.iconColorIndex &&
                   iconTextGap       == other.iconTextGap &&
                   iconSize          == other.iconSize &&
                   iconOffsetY       == other.iconOffsetY &&
                   iconSide          == other.iconSide;
        }
    };
}