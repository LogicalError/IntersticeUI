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
    enum class VisualColorStyleIndex : uint16_t { Invalid = 0 };


    // ------------------------------------------------------------------------
    // Element State
    // ------------------------------------------------------------------------

    enum class ElementState : uint8_t
    {
        None      = 0,
        Active    = 1 << 0,  // Being pressed/interacted with
        Hover     = 1 << 1,
        Focused   = 1 << 2,
        Selected  = 1 << 3,
        Selection = 1 << 4,  // Text-selection highlight modifier - combines with Active/Hover/Focused/Selected
        Disabled  = 1 << 5,  // Exclusive - collapses any other visual bits

        VisualMask = Active | Hover | Focused | Selected | Selection | Disabled
    };
    INTERSTICE_ENUM_OPERATORS(ElementState)

    // ------------------------------------------------------------------------
    // Box Model Types
    // ------------------------------------------------------------------------

    // Four-sided values for box model (margin, padding, border)
    struct BoxEdges
    {
        uint16_t top = 0;
        uint16_t right = 0;
        uint16_t bottom = 0;
        uint16_t left = 0;

        constexpr BoxEdges() = default;
        constexpr BoxEdges(uint16_t all) : top(all), right(all), bottom(all), left(all) {}
        constexpr BoxEdges(uint16_t vertical, uint16_t horizontal) 
            : top(vertical), right(horizontal), bottom(vertical), left(horizontal) {}
        constexpr BoxEdges(uint16_t t, uint16_t r, uint16_t b, uint16_t l) 
            : top(t), right(r), bottom(b), left(l) {}

        constexpr uint16_t Horizontal() const { return left + right; }
        constexpr uint16_t Vertical() const { return top + bottom; }
        constexpr bool IsZero() const { return top == 0 && right == 0 && bottom == 0 && left == 0; }

        bool operator==(const BoxEdges&) const noexcept = default;
    };

    // Box corners
    struct BoxCorners
    {
        uint16_t topLeft = 0;
        uint16_t topRight = 0;
        uint16_t bottomRight = 0;
        uint16_t bottomLeft = 0;

        constexpr BoxCorners() = default;
        constexpr BoxCorners(uint16_t all)
            : topLeft(all), topRight(all), bottomRight(all), bottomLeft(all) {}
        constexpr BoxCorners(uint16_t tl, uint16_t tr, uint16_t br, uint16_t bl)
            : topLeft(tl), topRight(tr), bottomRight(br), bottomLeft(bl) {}

        constexpr bool IsZero() const 
        { 
            return topLeft == 0 && topRight == 0 && bottomRight == 0 && bottomLeft == 0; 
        }

        bool operator==(const BoxCorners&) const noexcept = default;
    };

    // CSS box-shadow layers per state; the first is drawn on top.
    inline constexpr int kMaxBoxShadows = 2;

    // One CSS box-shadow layer, outside the border box. A transparent color is no shadow.
    // Inset is for the focus ring only: an outline `spread` wide inside the border box.
    struct BoxShadow
    {
        int16_t  x      = 0;
        int16_t  y      = 0;
        uint16_t blur   = 0;
        int16_t  spread = 0;
        color4b  color  {};
        bool     inset  = false;

        constexpr bool IsNone() const { return color.a == 0; }
        bool operator==(const BoxShadow&) const noexcept = default;
    };

    // Absolute/fixed positioning
    struct EdgeInset
    {
        // "Not set". Insets can be negative (-1 is a real offset), so the
        // sentinel sits at the bottom of the range, where no real inset lands.
        static constexpr int16_t kAuto = std::numeric_limits<int16_t>::min();

        int16_t top    = kAuto;
        int16_t right  = kAuto;
        int16_t bottom = kAuto;
        int16_t left   = kAuto;

        constexpr EdgeInset() = default;
        constexpr EdgeInset(int16_t all) : top(all), right(all), bottom(all), left(all) {}
        constexpr EdgeInset(int16_t vertical, int16_t horizontal)
            : top(vertical), right(horizontal), bottom(vertical), left(horizontal) {}
        constexpr EdgeInset(int16_t t, int16_t r, int16_t b, int16_t l)
            : top(t), right(r), bottom(b), left(l) {}

        bool operator==(const EdgeInset&) const noexcept = default;
    };

    // ------------------------------------------------------------------------
    // Layout Properties
    // ------------------------------------------------------------------------

    enum class FlexDirection : uint8_t
    {
        Row,            // Horizontal, left to right
        RowReverse,     // Horizontal, right to left
        Column,         // Vertical, top to bottom
        ColumnReverse   // Vertical, bottom to top
    };

    enum class FlexWrap : uint8_t
    {
        NoWrap,         // Single line
        Wrap,           // Multi-line
        WrapReverse     // Multi-line, reversed
    };

    enum class AlignItems : uint8_t
    {
        Auto,           // align-self only: inherit from container's align-items
        FlexStart,      // Align to start of cross axis
        FlexEnd,        // Align to end of cross axis
        Center,         // Center on cross axis
        Stretch         // Stretch to fill container
    };

    enum class JustifyContent : uint8_t
    {
        FlexStart,      // Pack to start
        FlexEnd,        // Pack to end
        Center,         // Pack to center
        SpaceBetween,   // Space between items
        SpaceAround,    // Space around items
        SpaceEvenly     // Equal space between all items
    };

    // Distributes cross-axis space between wrap lines (align-items aligns within a line).
    // Only applies when flex-wrap != NoWrap.
    enum class AlignContent : uint8_t
    {
        FlexStart,      // Pack lines to start (default here; no stretching)
        FlexEnd,        // Pack lines to end
        Center,         // Pack lines to center
        Stretch,        // Lines grow to fill the container's cross axis
        SpaceBetween,   // Space between lines
        SpaceAround,    // Space around lines
        SpaceEvenly     // Equal space between all lines
    };

    // Position and display properties
    enum class PositionType : uint8_t
    {
        Relative,   // Normal flow, relative to natural position
        Anchored,   // Anchored to another element
        Absolute,   // Out of flow, content space - scrolls with parent (like CSS position:absolute)
        Fixed,      // Out of flow, overlay space - immune to parent scroll (like CSS position:fixed)
		Centered,   // Centered in parent container
		Sticky      // In-flow like Relative; clamped against the parent's scroll viewport edge (like CSS position:sticky)
    };

    enum class Display : uint8_t
    {
        Flex,       // Flexbox container
        Grid,       // Grid container
        None        // Not rendered, removed from layout
    };

    enum class Overflow : uint8_t
    {
        Auto,       // Content clipped, scrollbars shown when needed
        Hidden,     // Content clipped, no scrollbars
        Visible     // Content not clipped
    };

    enum class Visibility : uint8_t
    {
        Visible,    // Rendered normally
        Collapsed   // Not rendered, removed from layout
    };

    enum class IconSide : uint8_t
    {
        None = 0,

        Left,
        Right,
        Top,
        Bottom
    };

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
        CenterCenter       = CenterHorz | CenterVert,
		
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

    // Single-axis anchor placement (4-bit value matching the per-nibble encoding in AnchorPlacement)
    enum class AnchorPlacementAxis : uint8_t
    {
        None       = 0,
        OutsideMin = 1, // Outside near edge (left/top)
        CenterMin  = 2, // Centered on near edge
        InsideMin  = 3, // Inside from near edge
        Center     = 4, // Centered in target
        InsideMax  = 5, // Inside from far edge (right/bottom)
        CenterMax  = 6, // Centered on far edge
        OutsideMax = 7, // Outside far edge (right/bottom)
    };

    // Flip a single-axis placement (Min<->Max)
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

    // Extract per-axis placement from combined AnchorPlacement
    constexpr AnchorPlacementAxis GetHorizontalAxis(AnchorPlacement p) { return (AnchorPlacementAxis)((uint8_t)p & 0x0F); }
    constexpr AnchorPlacementAxis GetVerticalAxis(AnchorPlacement p)   { return (AnchorPlacementAxis)(((uint8_t)p >> 4) & 0x0F); }

    // Combine two per-axis placements into AnchorPlacement
    constexpr AnchorPlacement MakeAnchorPlacement(AnchorPlacementAxis h, AnchorPlacementAxis v)
    {
        return (AnchorPlacement)((uint8_t)h | ((uint8_t)v << 4));
    }


    // ------------------------------------------------------------------------
    // Flex Container Properties
    // ------------------------------------------------------------------------

    struct FlexProperties
    {
        static constexpr int16_t kAuto = -1;

        uint16_t flexGrow = 0;
        uint16_t flexShrink = 0;
        int16_t  flexBasis = kAuto; // -1 for 'auto'

        uint16_t gap = 0;  // Spacing between flex items

        FlexDirection  direction      : 2 = FlexDirection::Row;
        FlexWrap       wrap           : 2 = FlexWrap::NoWrap;
        AlignItems     alignItems     : 3 = AlignItems::FlexStart;
        JustifyContent justifyContent : 3 = JustifyContent::FlexStart;
        AlignContent   alignContent   : 3 = AlignContent::FlexStart;

        bool operator==(const FlexProperties&) const noexcept = default;
    };

    // ------------------------------------------------------------------------
    // Grid Track Definition
    // ------------------------------------------------------------------------

    struct GridTrack
    {
        enum class Type : uint8_t
        {
            Fixed,      // value is in pixels (e.g., 100.0f)
            Fractional, // fractional unit (e.g., 1.0f = 1fr)
            Auto,       // fits content
            Default,    // defers to the element's style (only valid in LayoutOverride)
        };

        Type  type = Type::Auto;
        float value = 1.0f;  // Meaning depends on type: pixels for Fixed, fraction for Fractional
        // Fractional tracks only: size = max(minPx, content) + fractional share of free space.
        float minPx = 0.0f;

        static constexpr GridTrack Auto()                  { return { .type = Type::Auto,       .value = 1.0f  }; }
        static constexpr GridTrack Fixed(float value)      { return { .type = Type::Fixed,      .value = value }; }
        static constexpr GridTrack Fractional(float value, float minPx = 0.0f)
                                                           { return { .type = Type::Fractional, .value = value, .minPx = minPx }; }
        static constexpr GridTrack Default()               { return { .type = Type::Default,    .value = 0.0f  }; }

        bool operator==(const GridTrack& other) const noexcept = default;
    };

    // ------------------------------------------------------------------------
    // Grid Container Properties
    // ------------------------------------------------------------------------
    
    struct GridAxisProperties
    {
        static constexpr int16_t kAuto = -1;        

        GridTrack track = GridTrack::Default();

        // Explicit placement (-1 = auto-place)
        int16_t start{ kAuto };
        int16_t span{ 1 };
        int16_t gap{ 0 };

        bool operator==(const GridAxisProperties&) const noexcept = default;
    };

    struct GridProperties
    {
        GridAxisProperties column{};
        GridAxisProperties row{};

        // Alignment properties
        JustifyContent justifyContent = JustifyContent::FlexStart;
        AlignItems     alignItems     = AlignItems::Stretch;
        JustifyContent justifyItems   = JustifyContent::FlexStart;
        AlignItems     alignItemsSelf = AlignItems::Stretch;

        bool operator==(const GridProperties&) const noexcept = default;
    };

    // ------------------------------------------------------------------------
    // Container for layout type specific properties
    // ------------------------------------------------------------------------

    struct LayoutData
    {
        FlexProperties flex {};
        GridProperties grid {};

        // CSS size properties: width/height (auto = -1), min/max constraints
        static constexpr int32_t kAutoSize     = -1;
        static constexpr int32_t kUnboundedMax = std::numeric_limits<int32_t>::max();

        int32_t width     = kAutoSize;
        int32_t height    = kAutoSize;
        int32_t minWidth  = 0;
        int32_t maxWidth  = kUnboundedMax;
        int32_t minHeight = 0;
        int32_t maxHeight = kUnboundedMax;

        bool operator==(const LayoutData&) const noexcept = default;
    };

    // ------------------------------------------------------------------------
    // Break Behavior (CSS break-after: auto | always | avoid | left | right)
    // ------------------------------------------------------------------------

    enum class BreakBehavior : uint8_t
    {
        Auto,        // No forced breaks, no keep-with-next
        Always,      // Force a line break after this element (CSS: always / right)
        Before,      // Force a line break before this element (CSS: left)
        Avoid,       // Prevent a line break between this and the next element (CSS: avoid)
    };

    // ------------------------------------------------------------------------
    // Mouse Cursor Types
    // ------------------------------------------------------------------------

    enum class MouseCursor : uint8_t
    {
        Default,        // Default arrow cursor
        Text,           // Text editing I-beam
        Wait,           // Wait/busy cursor
        Crosshair,      // Crosshair for precision selection
        Pointer,        // Pointing hand for clickable items
        ColResize,      // CSS col-resize: edge of a column being moved (left/right)
        RowResize,      // CSS row-resize: edge of a row being moved (up/down)
        EwResize,       // CSS ew-resize:  bidirectional east-west arrow (left/right movement)
        NsResize,       // CSS ns-resize:  bidirectional north-south arrow (up/down movement)
        NwseResize,     // CSS nwse-resize: diagonal (northwest-southeast)
        NeswResize,     // CSS nesw-resize: diagonal (northeast-southwest)
        Move,           // CSS move:       4-way arrow (free movement)
        NotAllowed,     // CSS not-allowed

        None,           // No cursor (invisible)
    };

    // ------------------------------------------------------------------------
    // Color Styles
    // ------------------------------------------------------------------------

    enum class ColorType : uint16_t
    {
        SolidColor               = 0,
        StretchedTexture         = 1,
        RepeatedTexture          = 2,
        LinearGradient           = 3,
        CircularAngularGradient  = 4,
        EllipsoidAngularGradient = 5,
        RadialGradient           = 6,
        InterpolatedTriangle     = 7,
        InterpolatedBox          = 8,
        SDFGradient              = 9
    };

    class VisualColorStyle;
    class ColorStyle
    {
    public:
        static constexpr int16_t one = 15360;

        Gradient  gradient{};
        short4    transformationX = { one, 0, 0, 0 };
        short4    transformationY = { 0, one, 0, 0 };
        ColorType type            = (ColorType)0;
        short2    animSpeed       = { 0, 0 }; // half-float x=U/gradient speed, y=V speed (textures only)

        void SetTransformation(float3 x, float3 y);
        void SetAnimSpeed(float x, float y = 0.0f);
        float3x2 GetTransformation() const;

        // Bakes the GPU-facing style and acquires its atlas slot. The caller owns
        // outHandle and must Release it (or hand it to RegisterVisualColorStyle).
        // Texture types return an invalid handle.
        VisualColorStyle ToVisualStyle(GradientManager& gradientManager,
                                       ImageIndex textureIndex,
                                       GradientHandle& outHandle) const;


        static ColorStyle Create(color4b color);
        static ColorStyle Create(color4f color);
        static ColorStyle Create(color4b c0, color4b c1, ColorType type);
        static ColorStyle Create(color4f c0, color4f c1, ColorType type);
        static ColorStyle Create(color4b c0, color4b c1, color4b c2, ColorType type);
        static ColorStyle Create(color4f c0, color4f c1, color4f c2, ColorType type);
        static ColorStyle Create(color4b c0, color4b c1, color4b c2, color4b c3, ColorType type);
        static ColorStyle Create(color4f c0, color4f c1, color4f c2, color4f c3, ColorType type);
        static ColorStyle Create(Gradient gradient, ColorType type);
    };

    class alignas(16) VisualColorStyle
    {
    public:
        static constexpr int16_t one = 15360;

        short4     transformationX = { one, 0, 0, 0 };
        short4     transformationY = { 0, one, 0, 0 };

        ushort2    colorUV         = {};
        ColorType  type            = (ColorType)0;

        // Logical gradient atlas page (0..N-1) that colorUV refers to. Resolved
        // to a physical shader-bindless image index by the renderer just before
        // uploading (see GraphicsContext::PrepareVisualColorStylesBuffer).
        uint16_t   gradientPage    = 0;

        ImageIndex textureIndex    = ImageIndex::Invalid;

        short2     animSpeed      = { 0, 0 }; // half-float x=U/gradient speed, y=V speed (textures only)

        void SetTransformation(float3 x, float3 y);
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

        // Factory helpers. Each allocates a gradient-atlas slot via the supplied
        // manager and returns the handle through outHandle - the caller owns it.
        static VisualColorStyle Create(GradientManager& gradientManager, color4b color, GradientHandle& outHandle);
        static VisualColorStyle Create(GradientManager& gradientManager, color4f color, GradientHandle& outHandle);
        static VisualColorStyle Create(GradientManager& gradientManager, color4b color1, color4b color2, ColorType type, GradientHandle& outHandle);
        static VisualColorStyle Create(GradientManager& gradientManager, color4f color1, color4f color2, ColorType type, GradientHandle& outHandle);
        static VisualColorStyle Create(GradientManager& gradientManager, color4b color1, color4b color2, color4b color3, ColorType type, GradientHandle& outHandle);
        static VisualColorStyle Create(GradientManager& gradientManager, color4f color1, color4f color2, color4f color3, ColorType type, GradientHandle& outHandle);
        static VisualColorStyle Create(GradientManager& gradientManager, color4b color1, color4b color2, color4b color3, color4b color4, ColorType type, GradientHandle& outHandle);
        static VisualColorStyle Create(GradientManager& gradientManager, color4f color1, color4f color2, color4f color3, color4f color4, ColorType type, GradientHandle& outHandle);
        static VisualColorStyle Create(GradientManager& gradientManager, Gradient gradient, ColorType type, GradientHandle& outHandle);
    };


    // ------------------------------------------------------------------------
    // Complete Style Definition
    // ------------------------------------------------------------------------

    struct StyleState
    {
        TextStyle textStyle {};

        // Visual properties
        ColorStyle backgroundColor {};
        ColorStyle borderColors[4] {};  // [0]=top, [1]=right, [2]=bottom, [3]=left
        ColorStyle textColor {};
        ColorStyle iconColor {};        // -interstice-icon-color; none = the text color
        BoxShadow  boxShadow[kMaxBoxShadows] {};

        // Shorthand helper - sets all four edges to the same color.
        void SetBorderColor(const ColorStyle& c) { borderColors[0] = borderColors[1] = borderColors[2] = borderColors[3] = c; }
        void SetBorderColor(const ColorStyle& t, const ColorStyle& r, const ColorStyle& b, const ColorStyle& l)
        { 
            borderColors[0] = t; borderColors[1] = r; borderColors[2] = b; borderColors[3] = l;
        }
    };

    // User-facing style object with all layout and visual properties.
    // Can be "baked" into optimized LayoutStyle and VisualBoxStyle pairs.
    struct Style
    {
        static constexpr int16_t kAuto = -1;

        static constexpr int kNormal    = 0;
        static constexpr int kActive    = 1;
        static constexpr int kHover     = 2;
        static constexpr int kFocus     = 4;
        static constexpr int kSelected  = 8;
        static constexpr int kSelection = 16; // Text-selection highlight modifier - combines with the bits above (slots 16..31)
        static constexpr int kDisabled  = 32; // Exclusive - slot 32 only; cannot be mixed with other bits

        static constexpr int kStateCount = kDisabled + 1;

        StyleState      states[kStateCount] {};
        TextLayoutStyle textLayoutStyle {};

        // Box model
        BoxEdges        margin{};
        BoxEdges        padding{};
        BoxEdges        borderWidths{};
        BoxCorners      borderRadius{};
        float           borderDashSize   = 0.0f; // pixels per dash (0 = solid)
        float           borderDashOffset = 0.0f; // pixel offset into dash pattern
        BoxShadow       focusRing {};            // :focus-visible box-shadow, over the state's shadows

        // Flex, Grid properties etc.
        LayoutData      layoutData {};

        uint16_t        iconTextGap = 0; // Spacing between icon and text
        uint16_t        iconSize    = 0; // Icon render size (px); 0 = use natural size
        int16_t         iconOffsetY = 0; // Vertical pixel nudge for the icon rect; positive = up, negative = down

        // Display
        ImageIndex      textureIndex = ImageIndex::Invalid;
        Display         display      : 3 = Display::Flex;
        Visibility      visibility   : 2 = Visibility::Visible;
        Overflow        overflowX    : 2 = Overflow::Auto;
        Overflow        overflowY    : 2 = Overflow::Auto;
        IconSide        iconSide     : 3 = IconSide::Left;
        AlignItems      alignSelf      : 3 = AlignItems::Auto; // Per-item cross-axis override (Auto = inherit from container)
        BreakBehavior   breakBehavior  : 2 = BreakBehavior::Auto;
        bool            clampScroll    : 1 = true;  // Clamp scroll offset to content bounds (false = infinite/unclamped scroll)

        // CSS `overflow` shorthand: sets both axes.
        constexpr void SetOverflow(Overflow o) noexcept { overflowX = o; overflowY = o; }

        // Input/interaction properties
        MouseCursor     cursor = MouseCursor::Default;
    };


    // ------------------------------------------------------------------------
    // Optimized Runtime Styles
    // ------------------------------------------------------------------------

    // Layout-only properties - immutable, optimized form
    struct LayoutStyle
    {
        static constexpr int16_t kAuto = -1;

        // Box model
        BoxEdges        margin{};
        BoxEdges        padding{};
        BoxEdges        borderWidths{};

        // Flex, Grid properties etc.
        LayoutData      layoutData{};

        uint16_t        iconTextGap = 0; // Spacing between icon and text
        uint16_t        iconSize    = 0; // Icon render size (px); 0 = use natural size
        int16_t         iconOffsetY = 0; // Vertical pixel nudge for an icon-only element; positive = up, negative = down

        // Display
        Display         display      : 3 = Display::Flex;
        Visibility      visibility   : 2 = Visibility::Visible;
        Overflow        overflowX    : 2 = Overflow::Visible;
        Overflow        overflowY    : 2 = Overflow::Visible;
        IconSide        iconSide     : 3 = IconSide::Left;
        AlignItems      alignSelf      : 3 = AlignItems::Auto; // Per-item cross-axis override (Auto = inherit from container)
        BreakBehavior   breakBehavior  : 2 = BreakBehavior::Auto;
        bool            clampScroll    : 1 = true;  // Clamp scroll offset to content bounds (false = infinite/unclamped scroll)

        MouseCursor     cursor = MouseCursor::Default;

        // Hashes a subset of the fields (no padding or bitfield bytes); operator== settles collisions.
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

        constexpr bool IsInFlow() const
        {
            return display != Display::None && 
                   visibility != Visibility::Collapsed;
        }

        constexpr bool IsFlexContainer() const
        {
            return display == Display::Flex;
        }

        // Minimum horizontal space required (margin + border + padding)
        constexpr int16_t GetMinWidth() const
        {
            return margin.Horizontal() + borderWidths.Horizontal() + padding.Horizontal();
        }

        // Minimum vertical space required (margin + border + padding)
        constexpr int16_t GetMinHeight() const
        {
            return margin.Vertical() + borderWidths.Vertical() + padding.Vertical();
        }

        // Combined minimum size
        constexpr short2 GetMinSize() const 
        { 
            return { GetMinWidth(), GetMinHeight() }; 
        }

        // Given a BORDER BOX rect (margin already removed), compute the content/padding box.
        // Only subtracts border + padding. Use after ApplyStyleOffset has converted rect to border box.
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

        // Given a MARGIN BOX rect (includes margin in size), compute the content/padding box.
        // Subtracts margin + border + padding. Use before ApplyStyleOffset.
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

        // Border-box rect (outer minus margin)
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

        // Padding-box rect (outer minus margin and border)
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

        inline int32_t GetBorderBoxWidth() const noexcept
        {
            return (int32_t)(padding.Horizontal() + borderWidths.Horizontal());
        }
    };

    // A BoxShadow in pixels with its color registered. No color is no shadow.
    struct VisualShadow
    {
        int16_t               x      = 0;
        int16_t               y      = 0;
        uint16_t              blur   = 0;
        int16_t               spread = 0;
        VisualColorStyleIndex color  = VisualColorStyleIndex::Invalid;

        constexpr bool IsNone() const { return color == VisualColorStyleIndex::Invalid; }
        bool operator==(const VisualShadow&) const noexcept = default;
    };

    // A state's shadows; the ring is drawn only while focus is from the keyboard, outside
    // the box or, for an inset ring, as a border-only box style over it.
    struct VisualShadowStyle
    {
        VisualShadow layers[kMaxBoxShadows] {};
        VisualShadow ring {};
        uint32_t     insetRing = 0; // 1 + flat GPU index of the outline's box style, 0 = none

        bool operator==(const VisualShadowStyle&) const noexcept = default;
    };

    // Visual-only box properties - immutable, optimized form
    struct alignas(16) VisualBoxStyle
    {
        BoxEdges   borderWidths{};                                          // 8 bytes
        BoxCorners borderRadius{};                                          // 8 bytes

        // Per-edge border colors: [0]=top, [1]=right, [2]=bottom, [3]=left
        VisualColorStyleIndex borderColorIndex[4] = {
            VisualColorStyleIndex::Invalid, VisualColorStyleIndex::Invalid,
            VisualColorStyleIndex::Invalid, VisualColorStyleIndex::Invalid
        };                                                                  // 8 bytes
        VisualColorStyleIndex backgroundColorIndex = VisualColorStyleIndex::Invalid; // 2 bytes
        uint16_t borderDashSize   = 0;                                     // 2 bytes (pixels, 0 = solid)
        uint16_t borderDashOffset = 0;                                     // 2 bytes (pixels, start offset)
        uint16_t shadowIndex      = 0;                                     // 2 bytes (1-based VisualShadowStyle, 0 = none; CPU only)
        // Total: 32 bytes

        // Compute stable hash
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

    // Visual-only text properties - immutable, optimized form
    struct VisualTextStyle
    {
        TextStyle             textStyle {};
        TextLayoutStyle       textLayoutStyle {};
        VisualColorStyleIndex textColorIndex = VisualColorStyleIndex::Invalid;
        VisualColorStyleIndex iconColorIndex = VisualColorStyleIndex::Invalid; // the text color unless set
        uint16_t              iconTextGap = 0; // Spacing between icon and text
        uint16_t              iconSize    = 0; // Icon render size (px); 0 = use natural size
        int16_t               iconOffsetY = 0; // Vertical pixel nudge for the icon rect; positive = up, negative = down
        IconSide              iconSide = IconSide::Left;

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