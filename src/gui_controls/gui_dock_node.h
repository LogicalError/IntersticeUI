#pragma once

#include <gui_core/gui_id.h>
#include <gui_core/gui_base_types.h>
#include <gui_core/gui_units.h>

#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tachyon
{
    class IDManager;


    /// Split direction and which child is pinned to splitSize; the other child flexes.
    enum class DockSplitMode : uint8_t
    {
        LeftPinned,    ///< horizontal split: first child (left) pinned, right flexes
        RightPinned,   ///< horizontal split: second child (right) pinned, left flexes
        TopPinned,     ///< vertical   split: first child (top) pinned, bottom flexes
        BottomPinned,  ///< vertical   split: second child (bottom) pinned, top flexes
    };

    /// True for LeftPinned / RightPinned (children side by side).
    constexpr bool IsHorizontalSplit(DockSplitMode m)
    {
        return m == DockSplitMode::LeftPinned || m == DockSplitMode::RightPinned;
    }
    /// True for RightPinned / BottomPinned (splitSize applies to the second child).
    constexpr bool PinsSecondChild(DockSplitMode m)
    {
        return m == DockSplitMode::RightPinned || m == DockSplitMode::BottomPinned;
    }

    /// Edge zone of a point within a dock content area (drop preview and split-on-drop).
    enum class DockDropZone : uint8_t
    {
        None,       ///< outside the rect entirely
        Left,
        Right,
        Top,
        Bottom,
        Center      ///< the interior, not near any edge
    };

    /// Docking tree node: a leaf with panel tabs, or a split with two children where
    /// splitSize applies to the pinned child. No application dependencies, so it's unit-testable.
    struct DockNode
    {
        /// ID of the node's container element; assigned lazily in BeginDockingContainer.
        ID   nodeID = ID::Invalid();

        /// ID of a leaf's tab bar. Distinct from nodeID, since the tab bar is a child of the
        /// node's container and FindElementRefByID returns the first match.
        ID   tabBarID = ID::Invalid();
        bool isLeaf = true;             ///< false = a split with `first` and `second`

        // ---- Leaf fields ----
        std::vector<ID> panelIds;       ///< the leaf's tabs, in order (Panel IDs in the DockingLayout)
        int selectedTab = 0;            ///< index into panelIds of the visible panel
        int selectedTabBeforeDrag = 0; ///< restored on drag-out

        /// Content element of the visible tab; written during construction, read by
        /// SyncDockingScrollOffsets after dispatch.
        ID lastContentID      = ID::Invalid();
        /// Panel that lastContentID belongs to. Stored because a tab click during dispatch has
        /// already changed selectedTab while the scroll handle still belongs to the old tab.
        ID lastContentPanelID = ID::Invalid();

        /// Last frame's content rect (screen space), for hit-testing during construction,
        /// when this frame's rects aren't laid out yet. The other cached* rects work the same.
        IntRect cachedContentRect {};
        IntRect cachedLeafRect {};      ///< tab bar + content
        IntRect cachedTabBarRect {};    ///< tab bar
        IntRect cachedSplitRect {};     ///< split container, to clamp the split size
        std::vector<IntRect> cachedTabRects {}; ///< per tab, for the insertion preview

        // ---- Split fields (only when !isLeaf) ----
        DockSplitMode mode = DockSplitMode::LeftPinned;  ///< direction and pinned child
        CssPx splitSize     = 200;   ///< pinned child's size; `mode` says which child that is
        CssPx minFirstSize  = 80;    ///< first child's minimum when dragging the sizer
        CssPx minSecondSize = 80;    ///< second child's minimum when dragging the sizer
        std::unique_ptr<DockNode> first;   ///< left / top child
        std::unique_ptr<DockNode> second;  ///< right / bottom child
    };

    // ---- Factory helpers ----------------------------------------------------

    /// A leaf with the given panel tabs.
    std::unique_ptr<DockNode> MakeLeaf(std::initializer_list<ID> panelIds = {});
    /// A split node owning `first` and `second`; sizes in CSS px.
    std::unique_ptr<DockNode> MakeSplit(DockSplitMode mode,
                                        CssPx splitSize,
                                        std::unique_ptr<DockNode> first,
                                        std::unique_ptr<DockNode> second,
                                        CssPx minFirstSize = 80,
                                        CssPx minSecondSize = 80);

    // ---- Pure tree operations -----------------------------------------------

    /// Moves a tab within a leaf, keeping the selected tab selected. No-op on invalid input.
    void ReorderLeafTabs(DockNode& node, int from, int to);

    /// Releases a tree's static node and tab-bar IDs; call before destroying it.
    void ReleaseDockTreeIDs(DockNode* node, IDManager* idManager);

    /// Removes a leaf's tab, clamping selectedTab. Returns true if the leaf is now empty.
    bool CloseLeafTab(DockNode& node, int tabIndex);

    /// Collapses empty leaves by promoting their siblings, bottom-up.
    /// An empty root stays an empty leaf, never nullptr.
    void CollapseEmptyLeaves(std::unique_ptr<DockNode>& rootRef, IDManager* idManager = nullptr);

    /// Removes the first occurrence of `panelId`, then collapses empty leaves.
    /// Returns true when the panel was found.
    bool RemovePanel(std::unique_ptr<DockNode>& rootRef, ID panelId, IDManager* idManager = nullptr);

    /// The node with `nodeID` under `root`; nullptr when not found (always for an Invalid ID).
    DockNode*       FindNode(DockNode* root, ID nodeID);
    const DockNode* FindNode(const DockNode* root, ID nodeID);

    /// An inclusive splitSize range, in CSS px.
    struct SplitSizeRange { int min; int max; };
    /// Allowed splitSize range: min = the pinned child's minimum,
    /// max = `containerExtent` - `sizerThickness` - the other child's minimum.
    /// Both arguments are CSS px. When the container is too small for both, returns {min, min}.
    SplitSizeRange ComputeSplitSizeRange(const DockNode& splitNode,
                                         int containerExtent,
                                         int sizerThickness);

    /// Returns splitSize clamped to that range (doesn't modify the node).
    int ClampSplitSize(const DockNode& splitNode,
                       int containerExtent,
                       int sizerThickness);

    /// Gives every node (and leaf tab bar) without an ID a static ID. Idempotent.
    void AssignMissingNodeIDs(DockNode* root, IDManager& idManager);

    // ---- Drop target result ----------------------------------------------------

    /// A drop location: the leaf under the cursor and the zone within its content area.
    struct DockDropTarget
    {
        DockNode*    leaf   = nullptr;              ///< null when nothing is under the cursor
        DockDropZone zone   = DockDropZone::None;   ///< an edge zone splits the leaf on drop
    };

    // ---- Drop-zone geometry ----------------------------------------------------

    /// The zone of `rect` that `point` is in. Edge strips are `edgeFraction` of the rect's
    /// size; corners go to the nearer edge. None outside the rect.
    DockDropZone ComputeDropZone(IntRect rect, int2 point, float edgeFraction = 0.25f);

    // ---- Tab drop position -----------------------------------------------------

    /// The cursor as the tab rows see it: off the rows (above, below, or in a gap
    /// between wrapped rows) it moves to the nearest row, so it is on one.
    int2 TabRowCursor(const DockNode& leaf, int2 cursor);

    /// Where a tab dropped at `cursor` goes (0 = before the first, count = after the
    /// last): after every tab on the rows above, and those on its row it is right of
    /// the middle of. Uses last frame's tab rects.
    int TabInsertionIndex(const DockNode& leaf, int2 cursor);

    // ---- Tree mutation for split-on-drop ---------------------------------------

    /// The unique_ptr that owns `target`, or nullptr.
    std::unique_ptr<DockNode>* FindNodeOwner(std::unique_ptr<DockNode>& rootRef, const DockNode* target);

    /// Replaces the leaf with a split: `droppedPanelId` goes into a new leaf on the `zone` side
    /// (Left/Right/Top/Bottom only). Returns the new leaf.
    DockNode* SplitLeafAtZone(std::unique_ptr<DockNode>& leafRef,
                               ID droppedPanelId,
                               DockDropZone zone,
                               CssPx initialSplitSize = 200);

    // ---- Layout save / restore ---------------------------------------------------
    /// Writes the tree as JSON, naming each panel by `keyOf`'s stable key (a title or type),
    /// never its runtime ID, so the layout survives restarts. Sizes are saved, per-frame
    /// caches aren't:
    ///
    ///     {"split":"LeftPinned","size":300,"minFirst":80,"minSecond":80,"first":{...},"second":{...}}
    ///     {"tabs":["Hierarchy","Viewport"],"selected":1}
    std::string SerializeDockTree(const DockNode& root, const std::function<std::string(ID)>& keyOf);

    /// Rebuilds a tree from SerializeDockTree output. `idOf` maps a key back to a live panel ID
    /// and returns nullopt for panels that no longer exist; those tabs are dropped and the
    /// emptied leaves collapse. Returns nullptr when the text is malformed, deeper than 32
    /// levels, or no panel survives, so the caller can fall back to its default layout. Panels
    /// the saved layout does not mention are not added; see UnplacedPanels().
    /// Each key is used at most once (a repeated key is dropped).
    std::unique_ptr<DockNode> ParseDockTree(std::string_view text,
        const std::function<std::optional<ID>(std::string_view)>& idOf);

    /// Every ID in `allPanels` that does not appear in the tree, in order. A caller restoring a
    /// saved layout uses this to place panels added since the layout was saved.
    std::vector<ID> UnplacedPanels(const DockNode& root, const std::vector<ID>& allPanels);

} // namespace tachyon
