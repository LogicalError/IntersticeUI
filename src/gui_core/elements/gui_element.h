#pragma once

#include <gui_core/style/gui_style.h>
#include <gui_core/style/gui_style_ref.h>

#include <gui_core/layout/gui_constraints.h>

#include <gui_core/elements/gui_element_flags.h>

#include <gui_core/input/gui_input_handler_func.h>

#include <gui_core/gui_id.h>
#include <gui_core/gui_base_types.h>
#include <gui_core/gui_enum_operators.h>

#include <functional>
#include <vector>

namespace interstice
{
    class GUIRenderer;
    class ElementStorage;
    class CustomDrawContext;

    // ------------------------------------------------------------------------
    // Element - Core element data
    // ------------------------------------------------------------------------

    struct Element
    {
        ID id = ID::Invalid();

        // Per-frame input handler for immediate-mode widgets.
        EventHandler eventHandler;

        TextID textID = TextID::Invalid;
        IconID iconID = IconID::Invalid;

        StyleRef styleRef {};
        int16_t monitorIndex = -1; // Windows only: monitor the window is on
        ElementFlags flags = ElementFlags::None;
        ElementState state = ElementState::None;

		ImageIndex backgroundImage = ImageIndex::Invalid;
        // Background ColorStyle override (solid or gradient); Invalid = use the box style's bg.
        VisualColorStyleIndex colorStyleIndex {};
    };

    // ------------------------------------------------------------------------
    // ElementRect
    // ------------------------------------------------------------------------

    struct ElementRect
    {
        // Computed during layout.
        IntRect rect {};          // Border box (screen space)
        IntRect innerRect {};     // Padding box (screen space)
        IntRect clipRect {};      // Clip bounds (screen space)
        IntRect contentBounds {}; // Bounds of all children (parent-local space)
        int2 scrollOffset{};

        LayoutOverride  constraints;

        const LayoutStyle* layoutStyle{nullptr};

        // Applied to children at render/hit-test time; layout ignores it except for scroll clamping.
        float3x2 contentTransform;

        int16_t monitorIndex = -1; // Windows only: monitor the window is on
        ElementFlags flags = ElementFlags::None;
        Display display{};
        uint16_t wrapLineIndex = 0; // Row-wrap line, written by horizontal positioning, read by vertical positioning
        int16_t  minContentWidth = 0; // Leaf with visibly overflowing text: the margin box width flex shrink stops at (CSS min-content)
    };

    // Render-time callback. Coordinates are inner-rect-local LAYOUT space; the renderer's
    // transform applies the inner-rect offset and content transforms.
    // See gui_custom_draw_context.h for the coordinate-space contract.
    using CustomDrawCallback = std::function<void(CustomDrawContext& ctx)>;

    class PostLayoutContext;

    // Runs after layout, before rendering, with read-only access to final rects/transforms.
    using PostLayoutCallback = std::function<void(PostLayoutContext& ctx)>;

    // ------------------------------------------------------------------------
    // HierarchyInfo
    // ------------------------------------------------------------------------

    struct HierarchyInfo
    {
        inline static constexpr HierarchyInfo Initialize(uint32_t parentIndex)
        {
            return HierarchyInfo
            {
                .parentIndex      = parentIndex,
                .nextSiblingIndex = HierarchyInfo::kInvalidNextIndex,
                .childOffset      = 0,
                .childCount       = 0
            };
        }

        static constexpr uint32_t kInvalidNextIndex = std::numeric_limits<uint32_t>::max();

        uint32_t parentIndex = 0;                      // Parent index at depth - 1
        uint32_t nextSiblingIndex = kInvalidNextIndex; // Next sibling at same depth        
        uint32_t childOffset = 0;                      // First child index at depth + 1        
        uint32_t childCount = 0;
    };

    // ------------------------------------------------------------------------
    // ElementRef - Identifies an element by (depth, index)
    // ------------------------------------------------------------------------

    struct ElementRef
    {
        uint32_t depth = 0;
        
        // Index into the depth's array, which holds children of many parents.
        uint32_t index = 0; 

        inline constexpr bool IsRoot() const { return depth == 0; }
        inline constexpr bool IsValid() const { return depth != std::numeric_limits<uint32_t>::max() && index != std::numeric_limits<uint32_t>::max(); }
        static constexpr ElementRef Invalid() { return ElementRef{ std::numeric_limits<uint32_t>::max() , std::numeric_limits<uint32_t>::max() }; }
        inline bool operator==(const ElementRef& rhs) const { return depth == rhs.depth && index == rhs.index; }
        inline bool operator!=(const ElementRef& rhs) const { return !(*this == rhs); }

        // depth in high 32 bits, index in low 32 bits
        inline uint64_t HashCode() const noexcept
        {
            return (static_cast<uint64_t>(depth) << 32) | static_cast<uint64_t>(index);
        }
    };

    struct IndexRange
    {
        uint32_t begin = 0;
        uint32_t end   = 0; // exclusive
        
        inline bool Empty() const { return end == begin; }
        inline uint32_t Size() const { return end - begin; }

        bool operator==(const IndexRange& rhs) const = default;
        bool operator!=(const IndexRange& rhs) const = default;
    };

    // What a MoveChild did: the slot rotations it applied at each depth, so code
    // that stores ElementRefs (e.g. the window registry) can follow the elements.
    struct ElementMove
    {
        struct Rotation { uint32_t depth, begin, mid, end; }; // std::rotate(begin, mid, end)
        std::vector<Rotation> rotations;
        uint32_t childDepth = 0;          // depth of the moved siblings
        uint32_t siblingsBegin = 0;       // absolute slot range of the siblings that changed order
        uint32_t siblingsEnd = 0;

        ElementRef Remap(ElementRef ref) const
        {
            for (const Rotation& r : rotations)
            {
                if (ref.depth != r.depth || ref.index < r.begin || ref.index >= r.end) continue;
                ref.index = (ref.index < r.mid) ? ref.index + (r.end - r.mid) : ref.index - (r.mid - r.begin);
            }
            return ref;
        }
    };

    // Node in the flat post-draw linked list stored in ElementStorage.
    struct PostDrawEntry
    {
        CustomDrawCallback callback;
        ElementRef         ownerRef;                   // element that registered AddCustomDrawAfter
        uint32_t           nextIndex = UINT32_MAX;     // next entry index, UINT32_MAX = end of list
    };

    // `callback` fires before the children render; the after-list fires after the whole subtree.
    // Both use ancestor_accum * T(inner-rect.pos) * own.contentTransform. The afterDraw* fields
    // snapshot rects/clip/transform when the owner renders, so after-callbacks see the same space.
    struct ElementCustomDraw
    {
        CustomDrawCallback callback;                       // fires before this element's children
        uint32_t           afterHead = UINT32_MAX;         // head of after-draw list (set during construction)
        uint32_t           afterTail = UINT32_MAX;         // tail, for O(1) append

        IntRect            afterDrawClipRectScreen     = {};  // SCREEN-space scissor
        IntRect            afterDrawElementRectLayout  = {};  // LAYOUT-space border-box
        IntRect            afterDrawInnerRectLayout    = {};  // LAYOUT-space padding-box
        float3x2           afterDrawRenderTransform    = {};  // inner-rect-local LAYOUT -> SCREEN pixels
        float4             afterDrawPixelRect          = {};  // the element's border box in window pixels
        IntRect            afterDrawSnappedRect        = {};  // snapped pixel rect; custom-draw boxes snap to it
    };

} // namespace interstice