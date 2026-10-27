#pragma once

// ---------------------------------------------------------------------------
// Toolbar dock - frames a central content area with up to four sides of
// reorderable toolbar items, which can also be dragged between sides. Drop
// feedback matches the dock-tab drop indicator (controls_docking.cpp).
//
// The caller owns the data model (per-side item lists passed each frame) and
// applies changes from callbacks. ToolbarDockState holds the IDs, rect cache,
// ghost window and drop animation.
//
// The rect cache exists because the drag visualizer runs during construction,
// before layout; it reads last frame's rects (like DockNode::cachedLeafRect).
// ---------------------------------------------------------------------------

#include <gui_core/gui_id.h>
#include <gui_core/gui_base_types.h>

#include <chrono>
#include <functional>
#include <string_view>
#include <vector>

namespace tachyon
{
    /// @addtogroup controls_docking
    /// @{

    class GUIApplication;

    /// A side of a toolbar dock; indexes the per-side arrays.
    enum class ToolbarSide : uint8_t { Top = 0, Right = 1, Bottom = 2, Left = 3 };
    inline constexpr int kToolbarSideCount = 4;   ///< number of ToolbarSide values

    /// Per-instance state for one toolbar dock. The caller owns this object
    /// (one per dock area). Allocate IDs once via InitializeToolbarDockState
    /// and release via ReleaseToolbarDockState.
    struct ToolbarDockState
    {
        ID      groupIDs[kToolbarSideCount] {};
        ID      ghostWindowID    = ID::Invalid();
        /// Outer dock container; its rect gives empty (invisible) sides a drop rect.
        ID      dockContainerID  = ID::Invalid();

        /// Last frame's side rects (screen space), written by SyncToolbarDockRects at the end
        /// of the frame; drag visualization reads them during construction, before layout.
        IntRect cachedSideRects[kToolbarSideCount] {};
        IntRect cachedDockRect  {};   ///< last frame's dock container rect (screen space)

        // Drop overlay animation between sides
        bool    dropAnimHasTarget = false;
        IntRect dropAnimCurrent {};
        IntRect dropAnimTarget  {};
        std::chrono::steady_clock::time_point dropAnimLastTime {};
    };

    /// Allocates the state's static IDs; call once before the first RenderToolbarDock.
    void InitializeToolbarDockState(ToolbarDockState& state, GUIApplication& app);
    /// Releases the IDs allocated by InitializeToolbarDockState.
    void ReleaseToolbarDockState  (ToolbarDockState& state, GUIApplication& app);

    /// One toolbar item. `renderBody` emits its contents, wrapped by the dock in
    /// a reorderable item and BeginToolbar / EndToolbar.
    struct ToolbarItem
    {
        ID                    id        = ID::Invalid();
        std::string           debugName {};   ///< stable name; saved UI state places items by it
        std::function<void()> renderBody;
    };

    /// Callbacks through which RenderToolbarDock changes the caller's data; the dock never
    /// touches that data itself.
    struct ToolbarDockCallbacks
    {
        /// An item moved within `side` from index `from` to `to`.
        std::function<void(ToolbarSide side, int from, int to)>                       onReorder;
        /// Item `srcIdx` of `srcSide` was dropped on `tgtSide`: the caller appends it to
        /// `tgtSide` and erases it from `srcSide`.
        std::function<void(ToolbarSide srcSide, int srcIdx, ToolbarSide tgtSide)>     onCrossSideDrop;
        /// Released outside every side: the caller moves the item to its floating list (new
        /// static windowID, spawnPosition = `mousePos`) and erases it from `srcSide`.
        std::function<void(ToolbarSide srcSide, int srcIdx, int2 mousePos)>           onTearOff;
    };

    /// A torn-off toolbar in its own OS window, owned by the caller. `windowID` is a
    /// static ID created on tear-off and released when the window closes.
    struct FloatingToolbarItem
    {
        ToolbarItem item;
        ID          windowID      = ID::Invalid();
        int2        spawnPosition {};                  ///< screen pixels, used on the first frame
        bool        positioned    = false;             ///< once true, the OS manages the position
        std::string title         {};
        ToolbarSide homeSide      = ToolbarSide::Top;  ///< where closing the window docks it back
    };

    /// Renders the 5-zone scaffold (top / left | content | right / bottom) with a
    /// reorderable group per side. `itemsPerSide` is indexed by (int)ToolbarSide;
    /// `renderContent` is called once between the left and right sides to emit the
    /// central content.
    void RenderToolbarDock(
        GUIApplication&                                       app,
        ToolbarDockState&                                     state,
        const std::vector<ToolbarItem>                        (&itemsPerSide)[kToolbarSideCount],
        const ToolbarDockCallbacks&                           callbacks,
        std::function<void()>                                 renderContent);
    /// Enables dragging a floating toolbar back onto a dock side; both fields are
    /// required, otherwise the drag handle only moves the OS window.
    struct FloatingToolbarOptions
    {
        /// Dock whose cachedSideRects are hit-tested on MouseUp (caller-owned; nullptr disables).
        ToolbarDockState* dockState = nullptr;
        /// Released over `tgtSide`, or the window closed (`tgtSide` = its homeSide): the caller
        /// moves the item into that side, releases the windowID and erases the floating entry.
        std::function<void(ID windowID, ToolbarSide tgtSide)> onDockBack;
    };

    // EmitFloatingToolbarWindows is a GUIApplication member (it needs the protected
    // BeginWindow / EndWindow): `app.EmitFloatingToolbarWindows(items, opts)`.

    /// @}
} // namespace tachyon
