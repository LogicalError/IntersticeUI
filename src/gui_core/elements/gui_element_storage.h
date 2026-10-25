// Element Storage: per-frame hierarchical storage for GUI elements.
// See the ElementStorage class documentation below.

#pragma once

#include <gui_core/style/gui_style.h>
#include <gui_core/style/gui_style_ref.h>

#include <gui_core/layout/gui_constraints.h>

#include <gui_core/elements/gui_element_flags.h>
#include <gui_core/elements/gui_element.h>
#include <gui_core/elements/gui_element_iterators.h>

#include <gui_core/input/gui_input_handler_func.h>

#include <gui_core/gui_id.h>
#include <gui_core/gui_base_types.h>
#include <gui_core/gui_enum_operators.h>

#include <unordered_map>

namespace interstice
{
    class GUIRenderer;
    class ElementStorage;
    class StyleManager;
    class ElementDataStore;

    // Invalid == 0 so zero-initialized handles are invalid; real handles are 1..N.
    enum class ScrollOffsetHandle : uint32_t { Invalid = 0 };


	// ------------------------------------------------------------------------
	// Result type for element storage operations.
	// ------------------------------------------------------------------------
    enum class ElementStorageResult
    {
        Success,
        NotConstructing,        // Called outside BeginConstruction/EndConstruction
        MissingAdd,             // Called without first calling Add
        MissingBeginChildren,   // EndChildren called without BeginChildren being called first
        MissingEndChildren,     // BeginChildren called without EndChildren being called before construction ends
        DuplicateCall,          // Method shouldn't be called more than once in a row
        InvalidStyle,           // An invalid style was used
        UnknownProblem          // Should not happen
    };

    struct AddResult 
    {
        ElementRef ref = ElementRef::Invalid();
        ElementStorageResult status = ElementStorageResult::Success;

        operator bool() const { return status == ElementStorageResult::Success; }

        inline bool operator==(ElementStorageResult rhs) const { return status == rhs; }
        inline bool operator!=(ElementStorageResult rhs) const { return !(*this == rhs); }

        inline bool operator==(const ElementRef& rhs) const { return ref == rhs; }
        inline bool operator!=(const ElementRef& rhs) const { return !(*this == rhs); }
    };

    // ------------------------------------------------------------------------
    // Element Storage - Main storage class
    // ------------------------------------------------------------------------

    // Not thread safe.
    // Elements are added per frame, in order, between BeginConstruction/EndConstruction.
    // Afterwards elements can be queried and modified, but the structure is immutable
    // until the next BeginConstruction resets it. Elements are never removed.
    //
    // All children of parents at depth D are stored contiguously at depth D+1,
    // in the same order as their parents.
    //
    // Depth-scan iterators (top-down / bottom-up) are const-only and valid only
    // after EndConstruction.
    //
    // Example:
    //  ElementStorage storage;
    // 
    //  -- construction mode
    // 
    //  storage.BeginConstruction();
    // 
    //    storage.Add(root); -- root node
    //    storage.BeginChildren();
    //      storage.Add(leaf);
    // 
    //      storage.Add(elementWithChildren);
    //      storage.BeginChildren();
    //        storage.Add(leaf);
    //        storage.Add(leaf);
    //        storage.Add(leaf);
    //      storage.EndChildren();
    //      
    //      storage.Add(leaf);
    //      storage.BeginChildren();
    //        storage.Add(leaf);
    //      storage.EndChildren();
    // 
    //      storage.Add(leaf);
    // 
    //    storage.EndChildren();
    // 
    //    storage.Add(secondRoot);
    // 
    //  storage.EndConstruction();
    class ElementStorage
    {
        static constexpr int32_t kMinimumReservedDepth = 32;
        static constexpr int32_t kMinimumReservedBreadth = 256;
    public:
        // ----------------- Construction ------------------

        ElementStorage(StyleManager& styleManager, ElementDataStore* dataStore = nullptr)
            : m_styleManager(styleManager), m_elementDataStore(dataStore)
        {
            while (kMinimumReservedDepth > m_depths.size())
            {
                m_depths.emplace_back().Reserve(kMinimumReservedBreadth);
            }
        }

        const StyleManager& GetStyleManager() const noexcept { return m_styleManager; }

        ElementStorage(ElementStorage&&) noexcept = default;
        ElementStorage& operator=(ElementStorage&&) noexcept = default;

        ElementStorage(const ElementStorage&) = delete;
        ElementStorage& operator=(const ElementStorage&) = delete;

        // ----------------- Construction ------------------
        
        // Fails with DuplicateCall if already constructing.
        [[nodiscard]] ElementStorageResult BeginConstruction()
        {
            if (m_constructionModeActive) return ElementStorageResult::DuplicateCall;
            m_generation++; // Wraps around            
            if (m_generation == 0) m_generation = 1; // never 0
            ClearStorage();
            m_constructionModeActive = true;
            return ElementStorageResult::Success;
        }

        // Fails if not constructing. An unmatched BeginChildren is closed here, so the
        // frame still completes, and reported as MissingEndChildren.
        [[nodiscard]] ElementStorageResult EndConstruction()
        {
            if (!m_constructionModeActive) return ElementStorageResult::NotConstructing;
            const bool unclosed = !m_parentStack.empty();
            while (!m_parentStack.empty())
                (void)EndChildren();
            m_constructionModeActive = false;
            return unclosed ? ElementStorageResult::MissingEndChildren : ElementStorageResult::Success;
        }

        [[nodiscard]] AddResult Add(Element&& element, LayoutOverride&& constraints = {});

        // Fires before ref's children render. Coords are inner-rect-local LAYOUT pixels in
        // ref's content space (they pan/zoom with ref's contentTransform).
        [[nodiscard]] ElementStorageResult AddCustomDraw(const ElementRef& ref, CustomDrawCallback callback);

        // Fires after ref's whole subtree, in the same space as AddCustomDraw.
        // Call after ref's EndChildren().
        [[nodiscard]] ElementStorageResult AddCustomDrawAfter(const ElementRef& ref, CustomDrawCallback callback);

        // Fires after layout, before rendering, with read-only final geometry.
        [[nodiscard]] ElementStorageResult AddPostLayout(const ElementRef& ref, PostLayoutCallback callback);

        // Begins adding children to the last added element; elements without one are leaves.
        // Fails outside construction or before any Add.
        [[nodiscard]] ElementStorageResult BeginChildren();

        // Fails outside construction or without a matching BeginChildren.
        [[nodiscard]] ElementStorageResult EndChildren();

        // Moves a child (with its subtree) to another sibling position, during construction.
        // fromLocal/toLocal are relative to rangeBegin, the absolute index of the first child
        // at childDepth. Refs inside the storage follow; outMove lets other holders remap theirs.
        [[nodiscard]] ElementStorageResult MoveChild(uint32_t childDepth, uint32_t rangeBegin,
                                                     uint32_t fromLocal, uint32_t toLocal,
                                                     ElementMove* outMove = nullptr);

        // ------------------ Statistics -------------------

        inline bool Empty() const { return m_elementCount == 0; }
        inline uint32_t ElementCount() const { return m_elementCount; }
        inline ID GetLastAddedID() const { return m_lastAddedID; }
        inline ElementRef GetLastAddedRef() const { return m_lastAddedRef; }
        inline uint32_t GetCurrentNestingDepth() const { return static_cast<uint32_t>(m_parentStack.size()); }
        inline uint32_t NumberOfDepthLevels() const { return m_usedDepthCount; }

        inline uint32_t DepthSizeOf(uint32_t depth) const
        {
            return depth >= m_usedDepthCount ? 0 : m_depths[depth].Size();
        }

        // ---------------- Queries ----------------

        const ID GetElementIDAt(ElementRef root, int2 position) const;

        // Converts a screen point into the space ref's own rect is stored in, applying each
        // ancestor's inverse contentTransform like hit-testing does. Use it to compare
        // event.state.screenPos with an element's rect under transformed ancestors.
        int2 ScreenToElementSpace(int2 screenPos, ElementRef ref) const;

        // O(1); Invalid when not found (always for ID::Invalid).
        // If an ID is used twice, the shallowest, earliest element wins.
        ElementRef FindElementRefByID(ID id) const;


        inline bool IsValidRef(const ElementRef& ref) const
        {
            return ref.depth >= m_usedDepthCount ? false :
                   ref.index < m_depths[ref.depth].Size();
        }
        
        inline ElementRef GetFirstElementRef() const { return m_usedDepthCount == 0 ? ElementRef::Invalid() : ElementRef{ 0, 0 }; }
        inline bool HasChildren(const ElementRef& ref) const { return GetChildCount(ref) > 0; }

        inline uint32_t GetChildCount(const ElementRef& ref) const
        {
            auto hierarchy = GetHierarchyInfoPtr(ref);
            return (hierarchy == nullptr) ? 0 : hierarchy->childCount;
        }

        inline ElementRef GetParentRef(const ElementRef& ref) const
        {
            if (ref.depth == 0) return ElementRef::Invalid();
            auto hierarchy = GetHierarchyInfoPtr(ref);
            if (hierarchy == nullptr) return ElementRef::Invalid();
            return ElementRef{ ref.depth - 1, hierarchy->parentIndex };
        }
        
        inline ElementRef GetNextSiblingRef(const ElementRef& ref) const
        {
            const HierarchyInfo *nodeHierarchy = GetHierarchyInfoPtr(ref);
            if (nodeHierarchy == nullptr ||
                nodeHierarchy->nextSiblingIndex == HierarchyInfo::kInvalidNextIndex) return ElementRef::Invalid();
            return ElementRef{ .depth = ref.depth, .index = nodeHierarchy->nextSiblingIndex };
        }

        // nullptr on invalid ref
        inline Element *GetElementPtr(const ElementRef &elementRef) { if (!IsValidRef(elementRef)) { return nullptr; } return &GetElementUnsafe(elementRef); }

        inline const Element *GetElementPtr(const ElementRef &elementRef) const { if (!IsValidRef(elementRef)) { return nullptr; } return &GetElementUnsafe(elementRef); }        
        inline Element& GetElementUnsafe(const ElementRef &elementRef) { return m_depths[elementRef.depth].elements[elementRef.index]; }
        inline const Element& GetElementUnsafe(const ElementRef &elementRef) const { return m_depths[elementRef.depth].elements[elementRef.index]; }               

        inline ElementRect* GetElementRectPtr(const ElementRef &elementRef) { if (!IsValidRef(elementRef)) { return nullptr; } return &GetElementRectUnsafe(elementRef); }
        inline const ElementRect* GetElementRectPtr(const ElementRef &elementRef) const { if (!IsValidRef(elementRef)) { return nullptr; } return &GetElementRectUnsafe(elementRef); }        
        inline ElementRect &GetElementRectUnsafe(const ElementRef &elementRef) { return m_depths[elementRef.depth].elementRects[elementRef.index]; }
        inline const ElementRect& GetElementRectUnsafe(const ElementRef& elementRef) const { return m_depths[elementRef.depth].elementRects[elementRef.index]; }

        // Call after Add() and before layout. Applied to children at render/hit-test time.
        inline bool SetContentTransform(const ElementRef& ref, const float3x2& transform)
        {
            if (!IsValidRef(ref)) return false;
            GetElementRectUnsafe(ref).contentTransform = transform;
            return true;
        }

        inline const ElementCustomDraw* GetCustomDraw(const ElementRef& elementRef) const
        {
            if (!IsValidRef(elementRef)) return nullptr;
            const auto& cd = m_depths[elementRef.depth].elementCustomDraws[elementRef.index];
            return (cd.callback || cd.afterHead != UINT32_MAX) ? &cd : nullptr;
        }

        // Fired in registration order between layout and rendering.
        struct PostLayoutEntry
        {
            PostLayoutCallback callback;
            ElementRef         ownerRef;
        };
        inline const std::vector<PostLayoutEntry>& GetPostLayoutEntries() const { return m_postLayoutEntries; }

        inline ElementCustomDraw& GetElementCustomDrawUnsafe(const ElementRef& elementRef)
        {
            return m_depths[elementRef.depth].elementCustomDraws[elementRef.index];
        }

        inline PostDrawEntry& GetPostDrawEntryUnsafe(uint32_t index)
        {
            return m_postDrawEntries[index];
        }

        // Valid until the next BeginConstruction. Empty on invalid ref or no children.
        std::span<Element> GetChildrenSpan(const ElementRef& parentRef)
        {
            auto range = GetChildRange(parentRef); // checks parentRef
            if (range.Empty()) return {};
            return { m_depths[parentRef.depth + 1].elements.data() + range.begin, range.Size() };
        }

        // Unchecked. Valid until the next BeginConstruction.
        std::span<Element> GetChildrenSpanUnsafe(int depth, const IndexRange range)
        {
            return {m_depths[depth].elements.data() + range.begin, range.Size()};
        }

        // Unchecked. Valid until the next BeginConstruction.
        std::span<const Element> GetChildrenSpanUnsafe(int depth, const IndexRange range) const 
        {
            return {m_depths[depth].elements.data() + range.begin,
                    range.Size()};
        }
        

        std::span<ElementRect> GetChildRectSpanUnsafe(int depth, const IndexRange range)
        {
            return {m_depths[depth].elementRects.data() + range.begin, range.Size()};
        }

        std::span<const ElementRect> GetChildRectSpanUnsafe(int depth, const IndexRange range) const 
        {
            return {m_depths[depth].elementRects.data() + range.begin, range.Size()};
        }

        // Valid until the next BeginConstruction. Empty on invalid ref or no children.
        std::span<const Element> GetChildrenSpan(const ElementRef& parentRef) const
        {
            auto range = GetChildRange(parentRef); // checks parentRef
            if (range.Empty()) return {};
            return { m_depths[parentRef.depth + 1].elements.data() + range.begin, range.Size() };
        }

        // Empty on invalid ref or no children.
        IndexRange GetChildRange(const ElementRef& parentRef) const
        {
            if (!IsValidRef(parentRef)) return {};
            return GetChildRangeUnsafe(parentRef);
        }

        // ----- Range helpers that return the iterators -----
        // Only valid after EndConstruction.

        friend struct TopDownDepthScanIterator;
        inline TopDownDepthScanIterator TopDownDepthScanBegin() const
        {
            assert(!m_constructionModeActive);
            if (Empty()) return TopDownDepthScanTerminator();
            DepthScanCursor cursor
            {
                .parentCountAtDepth = DepthSizeOf(0),
                .childRange = GetChildRangeUnsafe(cursor.CurrentRef())
            };
            return TopDownDepthScanIterator{*this, m_generation, cursor};
        }
        TopDownDepthScanIterator TopDownDepthScanEnd() const
        {
            assert(!m_constructionModeActive && "TopDownDepthScanEnd called during BeginConstruction/EndConstruction");
            return TopDownDepthScanTerminator();
        }

        friend struct BottomUpDepthScanIterator;
        inline BottomUpDepthScanIterator BottomUpDepthScanBegin() const
        {
            assert(!m_constructionModeActive);
            if (Empty()) return BottomUpDepthScanTerminator();
            DepthScanCursor cursor
            {
                .parentDepth = m_usedDepthCount - 1, .parentIndex = 0,
                .parentCountAtDepth = DepthSizeOf(cursor.parentDepth),
                .childRange = GetChildRangeUnsafe(cursor.CurrentRef())
            };
            return BottomUpDepthScanIterator{*this, m_generation, cursor};
        }
        BottomUpDepthScanIterator BottomUpDepthScanEnd() const
        {
            assert(!m_constructionModeActive && "BottomUpDepthScanIterator called during BeginConstruction/EndConstruction");
            return BottomUpDepthScanTerminator();
        }

        TopDownDepthScanRangeIterator TopDownDepthScanRange() const { return { TopDownDepthScanBegin(), TopDownDepthScanEnd() }; }
        BottomUpDepthScanRangeIterator BottomUpDepthScanRange() const { return { BottomUpDepthScanBegin(), BottomUpDepthScanEnd() }; }


    private:
        inline TopDownDepthScanIterator TopDownDepthScanTerminator() const { return TopDownDepthScanIterator{ .storage{*this}, .generation = m_generation, .cursor = DepthScanCursor::EndCursor() }; }
        inline BottomUpDepthScanIterator BottomUpDepthScanTerminator() const { return BottomUpDepthScanIterator{ .storage{*this}, .generation = m_generation, .cursor = DepthScanCursor::EndCursor() }; }

        void AdvanceTopDownDepthScan(DepthScanCursor& cursor) const;

        // Deepest depth toward 0; parents left-to-right per depth.
        void AdvanceBottomUpDepthScan(DepthScanCursor& cursor) const;


    private:                
        inline const HierarchyInfo* GetHierarchyInfoPtr(const ElementRef& elementRef) const
        {
            if (!IsValidRef(elementRef)) return nullptr;
            return &m_depths[elementRef.depth].hierarchy[elementRef.index];
        }

        inline HierarchyInfo *GetHierarchyInfoPtr(const ElementRef &elementRef)
        {
            if (!IsValidRef(elementRef)) return nullptr;
            return &m_depths[elementRef.depth].hierarchy[elementRef.index];
        }

        inline const HierarchyInfo* GetHierarchyInfoPtrUnsafe(const ElementRef& elementRef) const
        {
            return &m_depths[elementRef.depth].hierarchy[elementRef.index];
        }

        inline HierarchyInfo* GetHierarchyInfoPtrUnsafe(const ElementRef& elementRef)
        {
            return &m_depths[elementRef.depth].hierarchy[elementRef.index];
        }

        // GetChildRange without the ref check.
        IndexRange GetChildRangeUnsafe(const ElementRef& parentRef) const
        {
            auto& parent = m_depths[parentRef.depth].hierarchy[parentRef.index];
            if (parent.childCount == 0) return {};
            return IndexRange{ parent.childOffset, parent.childOffset + parent.childCount };
        }

        // Rotates all parallel arrays of a depth level.
        void RotateDepthLevel(uint32_t depth, uint32_t begin, uint32_t mid, uint32_t end);

        void RebuildSiblingLinks(uint32_t depth, uint32_t rangeBegin, uint32_t rangeEnd, uint32_t parentIndex);

        // Keeps allocations for reuse.
        void ClearStorage();


    private:
        // Parallel arrays for one hierarchy depth.
        struct DepthLevel
        {
            std::vector<Element> elements;
            std::vector<ElementRect> elementRects;
            std::vector<ElementCustomDraw> elementCustomDraws;
            std::vector<HierarchyInfo> hierarchy;

            inline uint32_t Size() const { return static_cast<uint32_t>(elements.size()); }
            inline bool Empty() const { return elements.empty(); }

            void Clear();

            void Reserve(uint32_t capacity);

            inline uint32_t PeekIndex() const
            {
                return Size();
            }

            inline uint32_t Add(int32_t parentIndex, Element&& element, ElementRect&& elementRect)
            {
                uint32_t index = Size();
                elements.push_back(std::move(element));
                elementRects.push_back(std::move(elementRect));
                elementCustomDraws.push_back({});
                hierarchy.push_back(HierarchyInfo::Initialize(parentIndex));
                return index;
            }
        };

        std::vector<DepthLevel> m_depths;

        struct ParentStackEntry
        {
            ElementRef ref = {};
            uint32_t startChildIndex = 0;
        };

        std::vector<ParentStackEntry> m_parentStack;

        // Invalid means BeginChildren is disallowed.
        ElementRef m_lastAddedRef = ElementRef::Invalid();
        ID m_lastAddedID = ID::Invalid();  // persists across BeginChildren/EndChildren
        bool m_childrenBegun = false;

        uint32_t m_elementCount = 0;

        // Depth levels that contain elements.
        uint32_t m_usedDepthCount = 0;

        uint64_t m_generation = 1;
        bool m_constructionModeActive = false;


        std::vector<PostDrawEntry>      m_postDrawEntries;
        std::vector<PostLayoutEntry>    m_postLayoutEntries;

        StyleManager &m_styleManager;
        ElementDataStore* m_elementDataStore = nullptr;

        // Add keeps it current; MoveChild marks it stale and the next lookup rebuilds it.
        mutable std::unordered_map<ID, ElementRef> m_idIndex;
        mutable bool                               m_idIndexStale = false;
        void IndexElement(ID id, ElementRef ref) const;
    };


    // ------------------------------------------------------------------------
    // ElementStorage iterators and range helpers (members)
    // ------------------------------------------------------------------------

    inline bool TopDownDepthScanIterator::Validate() const { return storage.m_generation == generation; }
    inline bool BottomUpDepthScanIterator::Validate() const { return storage.m_generation == generation; }
     
    inline TopDownDepthScanIterator& TopDownDepthScanIterator::operator++() { if (!Validate()) cursor.SetDone(); else storage.AdvanceTopDownDepthScan(cursor); return *this; }
    inline TopDownDepthScanIterator TopDownDepthScanIterator::operator++(int) { TopDownDepthScanIterator tmp = *this; if (!Validate()) cursor.SetDone(); else storage.AdvanceTopDownDepthScan(cursor); return tmp; }

    inline BottomUpDepthScanIterator& BottomUpDepthScanIterator::operator++() { if (!Validate()) cursor.SetDone(); else storage.AdvanceBottomUpDepthScan(cursor); return *this; }
    inline BottomUpDepthScanIterator BottomUpDepthScanIterator::operator++(int) { BottomUpDepthScanIterator tmp = *this; if (!Validate()) cursor.SetDone(); else storage.AdvanceBottomUpDepthScan(cursor); return tmp; }

} // namespace interstice