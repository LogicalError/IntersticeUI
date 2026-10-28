#include <gui_controls/gui_dock_node.h>

#include <gui_core/gui_id_manager.h>

#include <algorithm>
#include <cstdlib>
#include <charconv>
#include <utility>

namespace tachyon
{
    // ---- Factory helpers ----------------------------------------------------

    std::unique_ptr<DockNode> MakeLeaf(std::initializer_list<ID> panelIds)
    {
        auto node = std::make_unique<DockNode>();
        node->isLeaf = true;
        node->panelIds.assign(panelIds.begin(), panelIds.end());
        node->selectedTab = 0;
        return node;
    }

    std::unique_ptr<DockNode> MakeSplit(DockSplitMode mode,
                                        CssPx splitSize,
                                        std::unique_ptr<DockNode> first,
                                        std::unique_ptr<DockNode> second,
                                        CssPx minFirstSize,
                                        CssPx minSecondSize)
    {
        auto node = std::make_unique<DockNode>();
        node->isLeaf = false;
        node->mode = mode;
        node->splitSize = splitSize;
        node->minFirstSize = minFirstSize;
        node->minSecondSize = minSecondSize;
        node->first = std::move(first);
        node->second = std::move(second);
        return node;
    }

    // ---- Leaf operations ----------------------------------------------------

    void ReorderLeafTabs(DockNode& node, int from, int to)
    {
        if (!node.isLeaf) return;
        const int count = static_cast<int>(node.panelIds.size());
        if (from == to) return;
        if (from < 0 || from >= count) return;
        if (to   < 0 || to   >= count) return;

        ID moved = node.panelIds[from];
        node.panelIds.erase(node.panelIds.begin() + from);
        node.panelIds.insert(node.panelIds.begin() + to, moved);

        // Adjust selectedTab so whichever panel was selected stays selected.
        int sel = node.selectedTab;
        if (sel == from)
        {
            sel = to;
        }
        else if (from < to && sel > from && sel <= to)
        {
            sel -= 1;
        }
        else if (from > to && sel >= to && sel < from)
        {
            sel += 1;
        }
        node.selectedTab = sel;
    }

    bool CloseLeafTab(DockNode& node, int tabIndex)
    {
        if (!node.isLeaf) return node.panelIds.empty();
        const int count = static_cast<int>(node.panelIds.size());
        if (tabIndex < 0 || tabIndex >= count) return node.panelIds.empty();

        node.panelIds.erase(node.panelIds.begin() + tabIndex);

        const int newCount = static_cast<int>(node.panelIds.size());
        if (newCount == 0)
        {
            node.selectedTab = 0;
            return true;
        }

        // Keep the selected panel selected; if it was removed, select the next tab (clamped).
        int sel = node.selectedTab;
        if (tabIndex < sel)
            sel -= 1;
        if (sel >= newCount)
            sel = newCount - 1;
        if (sel < 0)
            sel = 0;
        node.selectedTab = sel;
        return false;
    }

    // ---- Collapse / remove --------------------------------------------------

    void ReleaseDockTreeIDs(DockNode* node, IDManager* idManager)
    {
        if (!node || !idManager) return;
        if (node->nodeID != ID::Invalid() && node->nodeID.GetMode() == IDMode::Static)
            idManager->ReleaseStaticID(node->nodeID);
        if (node->tabBarID != ID::Invalid() && node->tabBarID.GetMode() == IDMode::Static)
            idManager->ReleaseStaticID(node->tabBarID);
        if (!node->isLeaf)
        {
            ReleaseDockTreeIDs(node->first.get(), idManager);
            ReleaseDockTreeIDs(node->second.get(), idManager);
        }
    }

    // Collapses a subtree in place; nodeRef may be replaced.
    static void CollapseEmptyLeavesImpl(std::unique_ptr<DockNode>& nodeRef, IDManager* idManager)
    {
        if (!nodeRef) return;
        if (nodeRef->isLeaf) return;

        // Post-order, so cascading collapses work.
        CollapseEmptyLeavesImpl(nodeRef->first, idManager);
        CollapseEmptyLeavesImpl(nodeRef->second, idManager);

        bool firstEmpty  = nodeRef->first  && nodeRef->first->isLeaf  && nodeRef->first->panelIds.empty();
        bool secondEmpty = nodeRef->second && nodeRef->second->isLeaf && nodeRef->second->panelIds.empty();

        if (firstEmpty && secondEmpty)
        {
            // Both children empty: collapse into an empty leaf.
            ReleaseDockTreeIDs(nodeRef.get(), idManager);
            auto empty = std::make_unique<DockNode>();
            empty->isLeaf = true;
            nodeRef = std::move(empty);
            return;
        }

        if (firstEmpty)
        {
            ReleaseDockTreeIDs(nodeRef->first.get(), idManager);
            if (idManager && nodeRef->nodeID != ID::Invalid() && nodeRef->nodeID.GetMode() == IDMode::Static)
                idManager->ReleaseStaticID(nodeRef->nodeID);
            nodeRef = std::move(nodeRef->second);
            return;
        }

        if (secondEmpty)
        {
            ReleaseDockTreeIDs(nodeRef->second.get(), idManager);
            if (idManager && nodeRef->nodeID != ID::Invalid() && nodeRef->nodeID.GetMode() == IDMode::Static)
                idManager->ReleaseStaticID(nodeRef->nodeID);
            nodeRef = std::move(nodeRef->first);
            return;
        }
    }

    void CollapseEmptyLeaves(std::unique_ptr<DockNode>& rootRef, IDManager* idManager)
    {
        if (!rootRef)
        {
            // Caller expects a non-null root even when empty.
            rootRef = std::make_unique<DockNode>();
            rootRef->isLeaf = true;
            return;
        }
        CollapseEmptyLeavesImpl(rootRef, idManager);
    }

    // Returns true if something was removed from this subtree.
    static bool RemovePanelImpl(DockNode& node, ID panelId)
    {
        if (node.isLeaf)
        {
            auto it = std::find(node.panelIds.begin(), node.panelIds.end(), panelId);
            if (it == node.panelIds.end()) return false;

            const int removedIndex = static_cast<int>(it - node.panelIds.begin());
            node.panelIds.erase(it);

            const int newCount = static_cast<int>(node.panelIds.size());
            if (newCount == 0)
            {
                node.selectedTab = 0;
            }
            else
            {
                int sel = node.selectedTab;
                if (removedIndex < sel) sel -= 1;
                if (sel >= newCount)    sel = newCount - 1;
                if (sel < 0)            sel = 0;
                node.selectedTab = sel;
            }
            return true;
        }

        if (node.first && RemovePanelImpl(*node.first, panelId)) return true;
        if (node.second && RemovePanelImpl(*node.second, panelId)) return true;
        return false;
    }

    bool RemovePanel(std::unique_ptr<DockNode>& rootRef, ID panelId, IDManager* idManager)
    {
        if (!rootRef) return false;
        if (panelId == ID::Invalid()) return false;

        bool removed = RemovePanelImpl(*rootRef, panelId);
        if (removed)
            CollapseEmptyLeaves(rootRef, idManager);
        return removed;
    }

    // ---- Lookup --------------------------------------------------------------

    DockNode* FindNode(DockNode* root, ID nodeID)
    {
        if (!root) return nullptr;
        if (nodeID == ID::Invalid()) return nullptr;
        if (root->nodeID == nodeID) return root;
        if (!root->isLeaf)
        {
            if (DockNode* hit = FindNode(root->first.get(), nodeID)) return hit;
            if (DockNode* hit = FindNode(root->second.get(), nodeID)) return hit;
        }
        return nullptr;
    }

    const DockNode* FindNode(const DockNode* root, ID nodeID)
    {
        return FindNode(const_cast<DockNode*>(root), nodeID);
    }

    // ---- Split sizing --------------------------------------------------------

    SplitSizeRange ComputeSplitSizeRange(const DockNode& splitNode,
                                         int containerExtent,
                                         int sizerThickness)
    {
        // splitSize is the PINNED child's size, so its floor is that child's minimum
        // and the ceiling leaves the other child its own minimum.
        const bool pinSecond = PinsSecondChild(splitNode.mode);
        const int minPinned  = std::max(0, (pinSecond ? splitNode.minSecondSize : splitNode.minFirstSize).value);
        const int minOther   = std::max(0, (pinSecond ? splitNode.minFirstSize  : splitNode.minSecondSize).value);
        const int sizer      = std::max(0, sizerThickness);

        const int maxPinned = containerExtent - sizer - minOther;
        if (maxPinned < minPinned)
        {
            // Too small for both minimums: a stable {minPinned, minPinned} range.
            return { minPinned, minPinned };
        }
        return { minPinned, maxPinned };
    }

    int ClampSplitSize(const DockNode& splitNode,
                       int containerExtent,
                       int sizerThickness)
    {
        const SplitSizeRange range = ComputeSplitSizeRange(splitNode, containerExtent, sizerThickness);
        return std::clamp(splitNode.splitSize.value, range.min, range.max);
    }

    // ---- ID assignment -------------------------------------------------------

    void AssignMissingNodeIDs(DockNode* root, IDManager& idManager)
    {
        if (!root) return;
        // Static IDs, not dynamic (hash-based, can collide): EmitDockSplit finds child
        // elements by ID, so a collision would constrain the wrong element.
        if (root->nodeID == ID::Invalid())
            root->nodeID = idManager.CreateStaticID();
        if (root->isLeaf)
        {
            if (root->tabBarID == ID::Invalid())
                root->tabBarID = idManager.CreateStaticID();
        }
        else
        {
            AssignMissingNodeIDs(root->first.get(), idManager);
            AssignMissingNodeIDs(root->second.get(), idManager);
        }
    }

    // ---- Drop-zone geometry ----------------------------------------------------

    int2 TabRowCursor(const DockNode& leaf, int2 cursor)
    {
        int bestY = cursor.y, bestDistance = -1;
        for (const IntRect& r : leaf.cachedTabRects)
        {
            if (r.w <= 0)
                continue;   // in the overflow list, not the bar
            if (r.y <= cursor.y && cursor.y < r.y + r.h)
                return cursor;
            const int nearestY = cursor.y < r.y ? r.y : r.y + r.h - 1;
            const int distance = std::abs(nearestY - cursor.y);
            if (bestDistance < 0 || distance < bestDistance)
            {
                bestDistance = distance;
                bestY = nearestY;
            }
        }
        return { cursor.x, bestY };
    }

    int TabInsertionIndex(const DockNode& leaf, int2 cursor)
    {
        const int2 at = TabRowCursor(leaf, cursor);
        int index = 0;
        for (int i = 0; i < (int)leaf.cachedTabRects.size(); i++)
        {
            const IntRect& r = leaf.cachedTabRects[i];
            if (r.w <= 0)
                continue;   // in the overflow list, not the bar
            const bool rowAbove = r.y + r.h <= at.y;
            const bool onRow    = r.y <= at.y && at.y < r.y + r.h;
            if (rowAbove || (onRow && at.x >= r.x + r.w / 2))
                index = i + 1;
        }
        return index;
    }

    DockDropZone ComputeDropZone(IntRect rect, int2 point, float edgeFraction)
    {
        if (!rect.Contains(point))
            return DockDropZone::None;

        const float rx = static_cast<float>(point.x - rect.x);
        const float ry = static_cast<float>(point.y - rect.y);
        const float w  = static_cast<float>(rect.w);
        const float h  = static_cast<float>(rect.h);
        if (w <= 0 || h <= 0) return DockDropZone::Center;

        const float edgeW = w * edgeFraction;
        const float edgeH = h * edgeFraction;

        const bool inLeft   = rx < edgeW;
        const bool inRight  = rx > w - edgeW;
        const bool inTop    = ry < edgeH;
        const bool inBottom = ry > h - edgeH;

        if (!inLeft && !inRight && !inTop && !inBottom)
            return DockDropZone::Center;

        // Normalized distance into each strip (0 = at the edge); in a corner the closer edge wins.
        const float dLeft   = edgeW > 0 ? rx / edgeW : 1.0f;
        const float dRight  = edgeW > 0 ? (w - rx) / edgeW : 1.0f;
        const float dTop    = edgeH > 0 ? ry / edgeH : 1.0f;
        const float dBottom = edgeH > 0 ? (h - ry) / edgeH : 1.0f;

        float best = 2.0f;
        DockDropZone zone = DockDropZone::Center;
        if (inLeft   && dLeft   < best) { best = dLeft;   zone = DockDropZone::Left;   }
        if (inRight  && dRight  < best) { best = dRight;  zone = DockDropZone::Right;  }
        if (inTop    && dTop    < best) { best = dTop;    zone = DockDropZone::Top;    }
        if (inBottom && dBottom < best) { best = dBottom; zone = DockDropZone::Bottom; }
        return zone;
    }

    // ---- Tree mutation for split-on-drop ---------------------------------------

    std::unique_ptr<DockNode>* FindNodeOwner(std::unique_ptr<DockNode>& rootRef, const DockNode* target)
    {
        if (!rootRef || !target) return nullptr;
        if (rootRef.get() == target) return &rootRef;
        if (!rootRef->isLeaf)
        {
            if (auto* hit = FindNodeOwner(rootRef->first,  target)) return hit;
            if (auto* hit = FindNodeOwner(rootRef->second, target)) return hit;
        }
        return nullptr;
    }

    DockNode* SplitLeafAtZone(std::unique_ptr<DockNode>& leafRef,
                               ID droppedPanelId,
                               DockDropZone zone,
                               CssPx initialSplitSize)
    {
        if (!leafRef) return nullptr;

        // The dropped panel is the pinned side (keeps initialSplitSize).
        DockSplitMode mode;
        switch (zone)
        {
            case DockDropZone::Left:   mode = DockSplitMode::LeftPinned;   break; // new is first
            case DockDropZone::Right:  mode = DockSplitMode::RightPinned;  break; // new is second
            case DockDropZone::Top:    mode = DockSplitMode::TopPinned;    break; // new is first
            case DockDropZone::Bottom: mode = DockSplitMode::BottomPinned; break; // new is second
            default: return nullptr;   // reject before touching leafRef
        }

        auto newLeaf     = MakeLeaf({ droppedPanelId });
        DockNode* result = newLeaf.get();
        auto existingLeaf = std::move(leafRef);

        bool newIsFirst = (zone == DockDropZone::Left || zone == DockDropZone::Top);

        if (newIsFirst)
            leafRef = MakeSplit(mode, initialSplitSize, std::move(newLeaf), std::move(existingLeaf));
        else
            leafRef = MakeSplit(mode, initialSplitSize, std::move(existingLeaf), std::move(newLeaf));

        return result;
    }

    // ---- Layout save / restore ---------------------------------------------------

    namespace
    {
        const char* ModeName(DockSplitMode mode)
        {
            switch (mode)
            {
            case DockSplitMode::LeftPinned: return "LeftPinned";
            case DockSplitMode::RightPinned: return "RightPinned";
            case DockSplitMode::TopPinned: return "TopPinned";
            case DockSplitMode::BottomPinned: return "BottomPinned";
            }
            return "LeftPinned";
        }

        void WriteQuoted(std::string& out, std::string_view text)
        {
            out += '"';
            for (const char c : text)
            {
                if (c == '"' || c == '\\') { out += '\\'; out += c; }
                else if (static_cast<unsigned char>(c) < 0x20) out += ' ';
                else out += c;
            }
            out += '"';
        }

        void WriteNode(std::string& out, const DockNode& node, const std::function<std::string(ID)>& keyOf)
        {
            if (node.isLeaf)
            {
                out += "{\"tabs\":[";
                bool first = true;
                for (const ID id : node.panelIds)
                {
                    if (!first) out += ',';
                    first = false;
                    WriteQuoted(out, keyOf(id));
                }
                out += "],\"selected\":" + std::to_string(node.selectedTab) + "}";
                return;
            }
            out += "{\"split\":\"";
            out += ModeName(node.mode);
            out += "\",\"size\":" + std::to_string(node.splitSize.value) + ",\"minFirst\":" + std::to_string(node.minFirstSize.value)
                + ",\"minSecond\":" + std::to_string(node.minSecondSize.value) + ",\"first\":";
            if (node.first) WriteNode(out, *node.first, keyOf); else out += "{\"tabs\":[],\"selected\":0}";
            out += ",\"second\":";
            if (node.second) WriteNode(out, *node.second, keyOf); else out += "{\"tabs\":[],\"selected\":0}";
            out += '}';
        }

        // Minimal strict JSON reader for exactly the shapes SerializeDockTree writes.
        class Reader
        {
        public:
            explicit Reader(std::string_view text) : text_(text) {}

            bool Expect(char c) { Skip(); if (pos_ < text_.size() && text_[pos_] == c) { ++pos_; return true; } return false; }
            bool Peek(char c) { Skip(); return pos_ < text_.size() && text_[pos_] == c; }
            bool AtEnd() { Skip(); return pos_ == text_.size(); }

            std::optional<std::string> String()
            {
                if (!Expect('"')) return std::nullopt;
                std::string out;
                while (pos_ < text_.size())
                {
                    const char c = text_[pos_++];
                    if (c == '"') return out;
                    if (c == '\\')
                    {
                        if (pos_ >= text_.size()) return std::nullopt;
                        out += text_[pos_++];
                    }
                    else out += c;
                    if (out.size() > 4096) return std::nullopt;
                }
                return std::nullopt;
            }

            std::optional<int> Integer()
            {
                Skip();
                const size_t start = pos_;
                if (pos_ < text_.size() && text_[pos_] == '-') ++pos_;
                const size_t digits = pos_;
                while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
                if (pos_ == digits || pos_ - start > 9) return std::nullopt;
                int value = 0;
                std::from_chars(text_.data() + start, text_.data() + pos_, value);
                return value;
            }

        private:
            void Skip() { while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\n' || text_[pos_] == '\r' || text_[pos_] == '\t')) ++pos_; }
            std::string_view text_;
            size_t pos_ = 0;
        };

        std::unique_ptr<DockNode> ReadNode(Reader& in, const std::function<std::optional<ID>(std::string_view)>& idOf,
            std::vector<ID>& used, int depth)
        {
            if (depth > 32 || !in.Expect('{')) return nullptr;
            auto key = in.String();
            if (!key || !in.Expect(':')) return nullptr;

            if (*key == "tabs")
            {
                if (!in.Expect('[')) return nullptr;
                auto leaf = std::make_unique<DockNode>();
                leaf->isLeaf = true;
                bool firstItem = true;
                // Per saved tab: how many surviving tabs precede it, so the saved
                // selection can be mapped onto the filtered list.
                std::vector<int> keptBefore;
                while (!in.Peek(']'))
                {
                    if (!firstItem && !in.Expect(',')) return nullptr;
                    firstItem = false;
                    const auto name = in.String();
                    if (!name) return nullptr;
                    keptBefore.push_back(static_cast<int>(leaf->panelIds.size()));
                    const auto id = idOf(*name);
                    if (id && std::find(used.begin(), used.end(), *id) == used.end())
                    {
                        used.push_back(*id);
                        leaf->panelIds.push_back(*id);
                    }
                    if (leaf->panelIds.size() > 256) return nullptr;
                }
                in.Expect(']');
                if (!in.Expect(',')) return nullptr;
                const auto selectedKey = in.String();
                if (!selectedKey || *selectedKey != "selected" || !in.Expect(':')) return nullptr;
                const auto selected = in.Integer();
                if (!selected || !in.Expect('}')) return nullptr;
                // The selected panel keeps its selection; if it was dropped, the tab after
                // it is selected (or the one before, when it was last), as when closing a tab.
                const int savedIndex = std::clamp(*selected, 0, std::max(0, static_cast<int>(keptBefore.size()) - 1));
                const int mapped     = keptBefore.empty() ? 0 : keptBefore[size_t(savedIndex)];
                leaf->selectedTab = leaf->panelIds.empty() ? 0 : std::min(mapped, static_cast<int>(leaf->panelIds.size()) - 1);
                return leaf;
            }

            if (*key != "split") return nullptr;
            const auto modeName = in.String();
            if (!modeName) return nullptr;
            DockSplitMode mode;
            if (*modeName == "LeftPinned") mode = DockSplitMode::LeftPinned;
            else if (*modeName == "RightPinned") mode = DockSplitMode::RightPinned;
            else if (*modeName == "TopPinned") mode = DockSplitMode::TopPinned;
            else if (*modeName == "BottomPinned") mode = DockSplitMode::BottomPinned;
            else return nullptr;

            int numbers[3] = {};
            const char* const expected[3] = {"size", "minFirst", "minSecond"};
            for (int i = 0; i < 3; ++i)
            {
                if (!in.Expect(',')) return nullptr;
                const auto name = in.String();
                if (!name || *name != expected[i] || !in.Expect(':')) return nullptr;
                const auto value = in.Integer();
                if (!value || *value < 0 || *value > 100000) return nullptr;
                numbers[i] = *value;
            }
            if (!in.Expect(',')) return nullptr;
            const auto firstKey = in.String();
            if (!firstKey || *firstKey != "first" || !in.Expect(':')) return nullptr;
            auto first = ReadNode(in, idOf, used, depth + 1);
            if (!first || !in.Expect(',')) return nullptr;
            const auto secondKey = in.String();
            if (!secondKey || *secondKey != "second" || !in.Expect(':')) return nullptr;
            auto second = ReadNode(in, idOf, used, depth + 1);
            if (!second || !in.Expect('}')) return nullptr;
            return MakeSplit(mode, numbers[0], std::move(first), std::move(second), numbers[1], numbers[2]);
        }

        bool IsEmptyTree(const DockNode& node)
        {
            if (node.isLeaf) return node.panelIds.empty();
            return (!node.first || IsEmptyTree(*node.first)) && (!node.second || IsEmptyTree(*node.second));
        }

        void CollectIds(const DockNode& node, std::vector<ID>& out)
        {
            if (node.isLeaf) { out.insert(out.end(), node.panelIds.begin(), node.panelIds.end()); return; }
            if (node.first) CollectIds(*node.first, out);
            if (node.second) CollectIds(*node.second, out);
        }
    }

    std::string SerializeDockTree(const DockNode& root, const std::function<std::string(ID)>& keyOf)
    {
        std::string out;
        WriteNode(out, root, keyOf);
        return out;
    }

    std::unique_ptr<DockNode> ParseDockTree(std::string_view text,
        const std::function<std::optional<ID>(std::string_view)>& idOf)
    {
        Reader in(text);
        std::vector<ID> used;
        auto root = ReadNode(in, idOf, used, 0);
        if (!root || !in.AtEnd() || IsEmptyTree(*root)) return nullptr;
        CollapseEmptyLeaves(root);
        return root;
    }

    std::vector<ID> UnplacedPanels(const DockNode& root, const std::vector<ID>& allPanels)
    {
        std::vector<ID> placed;
        CollectIds(root, placed);
        std::vector<ID> result;
        for (const ID id : allPanels)
            if (std::find(placed.begin(), placed.end(), id) == placed.end()) result.push_back(id);
        return result;
    }

} // namespace tachyon
