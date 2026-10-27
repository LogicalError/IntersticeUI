#pragma once

#include <gui_core/elements/gui_element.h>     // ElementRef (member by value, used inline)
#include <gui_core/gui_id.h>                   // ID (member by value, map key)

#include <algorithm>
#include <stdint.h>
#include <vector>
#include <unordered_map>

namespace tachyon
{
    /// Outcome of a WindowRegistry call.
    enum class RegistryResult
    {
        Success,
        InvalidRef,
        NoActiveWindow
    };

    /// Per-window lists of ElementRefs in addition (render) order, driven during
    /// ElementStorage construction:
    ///  - Clear() at BeginConstruction
    ///  - BeginWindowScope(id, windowRef) when a window element is added
    ///  - Add(elementRef) for every other element
    ///  - EndWindowScope() after the window's BeginChildren/EndChildren
    /// Nested windows are the active target until their scope ends.
    /// Kept out of ElementStorage so that stays a general-purpose element store.
    class WindowRegistry
    {
        static constexpr uint32_t kInitialWindowCapacity = 16;
        static constexpr uint32_t kInitialWindowStackCapacity = 8;
    public:
        static constexpr uint32_t kInvalidIndex = ~(uint32_t)0;

        /// One entry in a window's flat pre-order render list. To cull an element's subtree,
        /// jump to its subtreeSkip.
        struct RenderNode
        {
            ElementRef ref;
            uint32_t   subtreeSkip = 0; ///< flat-list index past this subtree
            uint32_t   _pad        = 0; ///< explicit padding -> 16 bytes, 4 nodes per cache line
        };

        /// A window of this frame and its render list.
        struct WindowEntry
        {
            uint32_t    parentWindowEntry = kInvalidIndex; ///< index of the enclosing window, if any
            ID          id                = ID::Invalid();
            ElementRef  windowRef         = ElementRef::Invalid();

            std::vector<RenderNode> members; ///< pre-order, populated during construction
        };

        WindowRegistry()
        {
            m_windows.reserve(kInitialWindowCapacity);
            m_windowStack.reserve(kInitialWindowStackCapacity);
        }

        /// Forgets all windows, keeping capacity; call at BeginConstruction.
        void Clear()
        {
            // Keep each window's member list so its capacity is reused next frame
            for (auto& window : m_windows)
            {
                window.members.clear();
                m_spareMembers.push_back(std::move(window.members));
            }
            m_windows.clear();
            m_windowStack.clear();
            m_windowIDToIndexMap.clear();
        }

        /// The number of registered windows.
        uint32_t WindowCount() const { return static_cast<uint32_t>(m_windows.size()); }

        /// The ID of the window at `windowIndex`; Invalid when out of range.
        const ID GetWindowID(uint32_t windowIndex) const
        {
            if (windowIndex >= m_windows.size())
                return ID::Invalid();
            return m_windows[windowIndex].id;
        }

        /// The element of the window at `windowIndex`; Invalid when out of range.
        ElementRef GetWindowRef(uint32_t windowIndex) const
        {
            if (windowIndex >= m_windows.size()) return ElementRef::Invalid();
            return m_windows[windowIndex].windowRef;
        }

        /// The element of the window with `id`; Invalid when unknown.
        ElementRef GetWindowRef(ID id) const
        {
            auto it = m_windowIDToIndexMap.find(id);
            if (it == m_windowIDToIndexMap.end())
                return ElementRef::Invalid();
            return m_windows[it->second].windowRef;
        }

        /// The index of the window with `id`; kInvalidIndex when unknown.
        uint32_t GetWindowIndex(ID id) const
        {
            auto it = m_windowIDToIndexMap.find(id);
            if (it == m_windowIDToIndexMap.end())
                return kInvalidIndex;
            return it->second;
        }

        /// True when a window with `id` is registered.
        inline bool Contains(ID id) const
        {
            auto it = m_windowIDToIndexMap.find(id);
            return it != m_windowIDToIndexMap.end();
        }

        /// A window's render list (nullptr for an invalid index), for building its render data.
        /// Valid until Clear(), another window is added, or the registry moves.
        const std::vector<RenderNode>* GetWindowMembers(uint32_t windowIndex) const
        {
            if (windowIndex >= m_windows.size()) return nullptr;
            return &m_windows[windowIndex].members;
        }

        /// How many window scopes are open.
        uint32_t GetWindowStackDepth() const
        {
            return static_cast<uint32_t>(m_windowStack.size());
        }

        /// The window being built (innermost window scope); Invalid outside one.
        ID GetCurrentWindowID() const
        {
            return m_windowStack.empty() ? ID::Invalid() : GetWindowID(m_windowStack.back());
        }

        /// True inside a window scope.
        bool HasActiveWindow() const
        {
            return !m_windowStack.empty();
        }

        /// Registers a window element just added to the ElementStorage and opens its scope:
        /// following Adds go to it until EndWindowScope.
        RegistryResult BeginWindowScope(ID id, const ElementRef& ref)
        {
            if (!ref.IsValid()) return RegistryResult::InvalidRef;

            // Create a new window entry
            uint32_t newIndex = static_cast<uint32_t>(m_windows.size());
            std::vector<RenderNode> members;
            if (!m_spareMembers.empty())
            {
                members = std::move(m_spareMembers.back());
                m_spareMembers.pop_back();
            }
            m_windows.push_back(
                WindowEntry
                {
                    .parentWindowEntry = m_activeWindowIndex,
                    .id = id,
                    .windowRef = ref,
                    .members = std::move(members)
                });
            m_windowIDToIndexMap[id] = newIndex;

            // Become the active window (supports nested windows)
            m_windowStack.push_back(newIndex);
            m_activeWindowIndex = newIndex;
            return RegistryResult::Success;
        }

        /// Adds a non-window element to the innermost window scope's render list.
        RegistryResult Add(const ElementRef &ref)
        {
            if (!ref.IsValid()) return RegistryResult::InvalidRef;
            if (m_windowStack.empty()) return RegistryResult::NoActiveWindow;

            m_windows[m_activeWindowIndex].members.push_back(RenderNode{ ref, 0, 0 });
            return RegistryResult::Success;
        }

        /// Moves an element's subtree, in the innermost window scope, to render last among its
        /// siblings (on top). Call during construction; BuildRenderSkipLinks updates subtreeSkip.
        void MoveSubtreeToEnd(const ElementRef& nodeRef)
        {
            if (m_windowStack.empty()) return;
            auto& members = m_windows[m_activeWindowIndex].members;

            // Find the node in the members list
            uint32_t nodeStart = UINT32_MAX;
            for (uint32_t i = 0; i < static_cast<uint32_t>(members.size()); i++)
            {
                if (members[i].ref == nodeRef) { nodeStart = i; break; }
            }
            if (nodeStart == UINT32_MAX) return;

            uint32_t nodeDepth = nodeRef.depth;

            // Find the end of this node's subtree: next node at same or shallower depth
            uint32_t subtreeEnd = nodeStart + 1;
            while (subtreeEnd < members.size() && members[subtreeEnd].ref.depth > nodeDepth)
                subtreeEnd++;

            // Find the end of the sibling group: scan forward past all remaining siblings
            // and their subtrees (any node at depth >= nodeDepth belongs to the group)
            uint32_t siblingGroupEnd = subtreeEnd;
            while (siblingGroupEnd < members.size() && members[siblingGroupEnd].ref.depth >= nodeDepth)
                siblingGroupEnd++;

            // Rotate [nodeStart, subtreeEnd) to just before siblingGroupEnd
            if (subtreeEnd < siblingGroupEnd)
                std::rotate(members.begin() + nodeStart, members.begin() + subtreeEnd, members.begin() + siblingGroupEnd);
        }

        /// Follows the elements an ElementStorage::MoveChild moved: remaps every ref,
        /// then reorders the moved siblings' subtrees so the list stays pre-order.
        void ApplyElementMove(const ElementMove& move)
        {
            for (auto& window : m_windows)
            {
                auto& members = window.members;
                for (RenderNode& node : members)
                    node.ref = move.Remap(node.ref);

                auto isMovedSibling = [&](const RenderNode& node)
                {
                    return node.ref.depth == move.childDepth &&
                           node.ref.index >= move.siblingsBegin && node.ref.index < move.siblingsEnd;
                };
                auto first = std::find_if(members.begin(), members.end(), isMovedSibling);
                if (first == members.end())
                    continue;
                auto last = std::find_if(members.rbegin(), members.rend(), isMovedSibling).base();
                while (last != members.end() && last->ref.depth > move.childDepth)
                    ++last; // include the last sibling's subtree

                // Split the region into sibling subtrees and put them in slot order.
                struct Chunk { uint32_t index; size_t begin, end; };
                std::vector<Chunk> chunks;
                for (auto it = first; it != last; )
                {
                    auto end = it + 1;
                    while (end != last && end->ref.depth > move.childDepth)
                        ++end;
                    chunks.push_back({ it->ref.index, size_t(it - members.begin()), size_t(end - members.begin()) });
                    it = end;
                }
                std::stable_sort(chunks.begin(), chunks.end(), [](const Chunk& a, const Chunk& b) { return a.index < b.index; });

                std::vector<RenderNode> reordered;
                reordered.reserve(size_t(last - first));
                for (const Chunk& chunk : chunks)
                    reordered.insert(reordered.end(), members.begin() + chunk.begin, members.begin() + chunk.end);
                std::copy(reordered.begin(), reordered.end(), first);
            }
        }

        /// Computes subtreeSkip for every RenderNode in every window. Call once after
        /// construction and layout, before rendering. O(n) per window, allocation-free once warm.
        void BuildRenderSkipLinks()
        {
            for (auto& window : m_windows)
            {
                auto& nodes = window.members;
                const uint32_t count = static_cast<uint32_t>(nodes.size());

                m_skipLinkStack.clear();
                for (uint32_t i = 0; i < count; i++)
                {
                    const uint32_t myDepth = nodes[i].ref.depth;
                    // Close any open ancestors that are at the same depth or deeper -
                    // their subtrees end here.
                    while (!m_skipLinkStack.empty() &&
                           nodes[m_skipLinkStack.back()].ref.depth >= myDepth)
                    {
                        nodes[m_skipLinkStack.back()].subtreeSkip = i;
                        m_skipLinkStack.pop_back();
                    }
                    m_skipLinkStack.push_back(i);
                }
                // Remaining open nodes have subtrees that extend to the end of the list.
                for (uint32_t idx : m_skipLinkStack)
                    nodes[idx].subtreeSkip = count;
            }
        }

        /// Closes the innermost window scope; NoActiveWindow when none is open.
        RegistryResult EndWindowScope()
        {
            if (m_windowStack.empty())
            {
                m_activeWindowIndex = kInvalidIndex;
                return RegistryResult::NoActiveWindow;
            }

            m_windowStack.pop_back();

            // Update active window index
            if (!m_windowStack.empty())
            {
                m_activeWindowIndex = m_windowStack.back();
            } else
                m_activeWindowIndex = kInvalidIndex;

            return RegistryResult::Success;
        }

        /// The windows, in registration order.
        ///@{
        auto begin() const { return m_windows.begin(); }
        auto end() const { return m_windows.end(); }
        const std::vector<WindowEntry>& Windows() const { return m_windows; }
        ///@}

    private:
        std::vector<WindowEntry> m_windows;        ///< this frame's windows, in registration order
        std::vector<uint32_t>    m_windowStack;    ///< indices of the open (nested) window scopes
        std::vector<uint32_t>    m_skipLinkStack;  ///< BuildRenderSkipLinks scratch, reused
        std::vector<std::vector<RenderNode>> m_spareMembers; ///< last frame's lists, for reuse

        uint32_t m_activeWindowIndex = kInvalidIndex;
        std::unordered_map<ID, uint32_t> m_windowIDToIndexMap;

    };

} // namespace tachyon