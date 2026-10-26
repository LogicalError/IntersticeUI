#pragma once

#include <gui_core/elements/gui_element.h>

#include <iterator>
#include <span>

namespace interstice
{
    class ElementStorage;

    // ------------------------------------------------------------------------
    // Depth-Scan Cursor
    // ------------------------------------------------------------------------

    struct DepthScanCursor
    {
        uint32_t parentDepth = 0;
        uint32_t parentIndex = 0;
        uint32_t parentCountAtDepth = 0;

        /// This parent's children at parentDepth + 1.
        IndexRange childRange = {0, 0};

        inline bool IsDone() const { return *this == EndCursor(); }
        inline bool HasChildren() const { return !childRange.Empty(); }
        inline ElementRef CurrentRef() const { return ElementRef{ parentDepth, parentIndex }; }

        const Element& CurrentElement(const ElementStorage &storage) const;
        Element& CurrentElement(ElementStorage &storage) const;
        const ElementRect& CurrentElementRect(const ElementStorage &storage) const;
        ElementRect& CurrentElementRect(ElementStorage &storage) const;
        std::span<const Element> GetChildren(const ElementStorage& storage) const;
        std::span<Element> GetChildren(ElementStorage &storage) const;
        std::span<const ElementRect> GetChildRects(const ElementStorage& storage) const;
        std::span<ElementRect> GetChildRects(ElementStorage &storage) const;
    
        inline bool operator==(const DepthScanCursor& rhs) const
        {
            return parentDepth == rhs.parentDepth &&
                   parentIndex == rhs.parentIndex &&
                   parentCountAtDepth == rhs.parentCountAtDepth;
        }
        inline bool operator!=(const DepthScanCursor& rhs) const { return !(*this == rhs); }

        static constexpr DepthScanCursor EndCursor() { return {}; }
        inline void SetDone() { *this = EndCursor(); }
    };

    #define INTERSTICE_ITERATOR_TRAITS_USING_DECLS(type) \
            using value_type = type; \
            using difference_type = std::ptrdiff_t; \
            using reference = const value_type&; \
            using pointer = const value_type*; 

    // ------------------------------------------------------------------------
    // Top-down depth-scan iterator: depth 0 to deepest, parents in index order.
    // Storage must not change during iteration.
    // ------------------------------------------------------------------------
    struct TopDownDepthScanIterator
    {
        const ElementStorage& storage;
        uint64_t generation = 0;
        DepthScanCursor cursor;

        INTERSTICE_ITERATOR_TRAITS_USING_DECLS(DepthScanCursor);
        using iterator_category = std::input_iterator_tag;

        inline bool Validate() const;

        inline const DepthScanCursor& operator*() const { return cursor; }
        inline const DepthScanCursor* operator->() const { return &cursor; }

        inline bool operator==(const TopDownDepthScanIterator& rhs) const { return &storage == &rhs.storage && generation == rhs.generation && cursor == rhs.cursor; }
        inline bool operator!=(const TopDownDepthScanIterator& rhs) const { return !(*this == rhs); }
        
        TopDownDepthScanIterator& operator++();
        TopDownDepthScanIterator operator++(int);
    };

    // ------------------------------------------------------------------------
    // Bottom-up depth-scan iterator: deepest depth to 0, parents in index order.
    // Storage must not change during iteration.
    // ------------------------------------------------------------------------
    struct BottomUpDepthScanIterator
    {
        const ElementStorage& storage;
        uint64_t generation = 0;
        DepthScanCursor cursor;

        INTERSTICE_ITERATOR_TRAITS_USING_DECLS(DepthScanCursor);
        using iterator_category = std::input_iterator_tag;

        inline bool Validate() const;

        inline const DepthScanCursor& operator*() const { return cursor; }
        inline const DepthScanCursor* operator->() const { return &cursor; }

        inline bool operator==(const BottomUpDepthScanIterator& rhs) const { return &storage == &rhs.storage && generation == rhs.generation && cursor == rhs.cursor; }
        inline bool operator!=(const BottomUpDepthScanIterator& rhs) const { return !(*this == rhs); }

        BottomUpDepthScanIterator& operator++();
        BottomUpDepthScanIterator operator++(int);
    };

    #undef INTERSTICE_ITERATOR_TRAITS_USING_DECLS

    // ------------------------------------------------------------------------
    // Range Wrappers - Enable idiomatic for-each loops
    // ------------------------------------------------------------------------

    template<typename Iterator>
    class Range
    {
    public:
        Range(Iterator begin, Iterator end) : m_begin(begin), m_end(end) {}
    
        Iterator begin() const { return m_begin; }
        Iterator end() const { return m_end; }
    
        bool empty() const { return m_begin == m_end; }
    
    private:
        Iterator m_begin;
        Iterator m_end;
    };

    using TopDownDepthScanRangeIterator = Range<TopDownDepthScanIterator>;
    using BottomUpDepthScanRangeIterator = Range<BottomUpDepthScanIterator>;

} // namespace interstice