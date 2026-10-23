#pragma once

// ---------------------------------------------------------------------------
// Toolbar dock — frames a central content area with up to four sides of
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

namespace interstice
{
    class GUIApplication;

    enum class ToolbarSide : uint8_t { Top = 0, Right = 1, Bottom = 2, Left = 3 };
    inline constexpr int kToolbarSideCount = 4;

    // Per-instance state for one toolbar dock. The caller owns this object
    // (one per dock area). Allocate IDs once via InitializeToolbarDockState
    // and release via ReleaseToolbarDockState.
    struct ToolbarDockState
    {
        ID      groupIDs[kToolbarSideCount] {};
        ID      ghostWindowID    = ID::Invalid();
        // Outer dock container; its rect gives empty (invisible) sides a drop rect.
        ID      dockContainerID  = ID::Invalid();

        // Previous-frame screen-space rects, populated by SyncToolbarDockRects
        // at end-of-frame. Read on the next frame during drag visualization.
        IntRect cachedSideRects[kToolbarSideCount] {};
        IntRect cachedDockRect  {};

        // Drop overlay animation between sides.
        bool    dropAnimHasTarget = false;
        IntRect dropAnimCurrent {};
        IntRect dropAnimTarget  {};
        std::chrono::steady_clock::time_point dropAnimLastTime {};
    };

    void InitializeToolbarDockState(ToolbarDockState& state, GUIApplication& app);
    void ReleaseToolbarDockState  (ToolbarDockState& state, GUIApplication& app);

    // One toolbar item. `renderBody` emits its contents, wrapped by the dock in
    // a reorderable item and BeginToolbar / EndToolbar.
    struct ToolbarItem
    {
        ID                    id        = ID::Invalid();
        std::string           debugName {};
        std::function<void()> renderBody;
    };

    // Callbacks invoked by RenderToolbarDock to mutate the caller's data.
    // - onReorder       : an item was moved within `side` from index `from` to `to`.
    // - onCrossSideDrop : an item was dragged from `srcSide[srcIdx]` and dropped
    //                     on `tgtSide` (caller appends to tgtSide and erases
    //                     from srcSide; the dock does not touch the caller's data).
    // - renderContent   : called once between the left and right sides; the
    //                     caller emits the central content (viewport, etc.).
    struct ToolbarDockCallbacks
    {
        std::function<void(ToolbarSide side, int from, int to)>                       onReorder;
        std::function<void(ToolbarSide srcSide, int srcIdx, ToolbarSide tgtSide)>     onCrossSideDrop;
        // Released outside every side: caller moves the item to its floating list
        // (new static windowID, spawnPosition = mousePos) and erases it from srcSide.
        std::function<void(ToolbarSide srcSide, int srcIdx, int2 mousePos)>           onTearOff;
    };

    // A torn-off toolbar in its own OS window, owned by the caller. `windowID` is a
    // static ID created on tear-off and released when the window closes.
    struct FloatingToolbarItem
    {
        ToolbarItem item;
        ID          windowID      = ID::Invalid();
        int2        spawnPosition {};
        bool        positioned    = false;
        std::string title         {};
        ToolbarSide homeSide      = ToolbarSide::Top;  // where closing the window docks it back
    };

    // Render the 5-zone scaffold (top / left | content | right / bottom) with a
    // reorderable group per side. `itemsPerSide` is indexed by (int)ToolbarSide.
    void RenderToolbarDock(
        GUIApplication&                                       app,
        ToolbarDockState&                                     state,
        const std::vector<ToolbarItem>                        (&itemsPerSide)[kToolbarSideCount],
        const ToolbarDockCallbacks&                           callbacks,
        std::function<void()>                                 renderContent);
    // Enables dragging a floating toolbar back onto a dock side; both fields are
    // required, otherwise the drag handle only moves the OS window.
    struct FloatingToolbarOptions
    {
        // Dock whose cachedSideRects are hit-tested on MouseUp (caller-owned; nullptr disables).
        ToolbarDockState* dockState = nullptr;
        // Released over `tgtSide`, or the window closed (tgtSide = its homeSide): caller
        // moves the item into that side, releases the windowID and erases the floating entry.
        std::function<void(ID windowID, ToolbarSide tgtSide)> onDockBack;
    };

    // EmitFloatingToolbarWindows is a GUIApplication member (it needs the protected
    // BeginWindow / EndWindow): `app.EmitFloatingToolbarWindows(items, opts)`.

} // namespace interstice
