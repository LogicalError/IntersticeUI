#pragma once

#include <gui_core/gui_base_types.h>
#include <gui_core/gui_enum_operators.h>
#include <gui_core/gui_id.h>

#include <unordered_map>
#include <vector>

namespace GUI
{
    class IDManager;
    class ElementStorage;
    struct ElementRef;
    enum class ScrollOffsetHandle : uint32_t;

    // Axis mask for scroll-into-view, so e.g. a horizontal sizer doesn't scroll vertically.
    enum class ScrollAxis : uint8_t
    {
        None = 0,
        X    = 1 << 0,
        Y    = 1 << 1,
        Both = X | Y,
    };
    ENUM_OPERATORS(ScrollAxis)

    class ScrollManagement
    {
        struct ScrollOffsetEntry
        {
            ID id = ID::Invalid();
            int2 offset = int2{0, 0};
            uint32_t lastSeenFrame = 0;
            bool inUse = false;
        };

        uint32_t m_frameIndex = 0;
        std::unordered_map<ID, ScrollOffsetHandle> m_scrollOffsetHandlesByID;
        std::vector<ScrollOffsetEntry> m_scrollOffsets;
        std::vector<ScrollOffsetHandle> m_scrollOffsetFreeList;

    public:
        void BeginFrame();
        void EndFrame(IDManager& idManager);

        ScrollOffsetHandle GetOrCreateScrollOffsetHandle(ID id);
        ScrollOffsetHandle GetScrollOffsetHandle(ID id);
        bool IsScrollOffsetHandleValid(ScrollOffsetHandle handle) const;

        int2 GetScrollOffset(ScrollOffsetHandle handle);
        void SetScrollOffset(ScrollOffsetHandle handle, int2 offset);

        // rectMin/rectMax are in the scroll container's content space.
        bool MakeRectVisibleInScroll(ScrollOffsetHandle scrollHandle, int2 rectMin, int2 rectMax, int2 containerSize, ScrollAxis axis = ScrollAxis::Both);
        // Target rect in screen space.
        bool MakeRectVisibleHierarchically(ElementStorage& elementStorage, int2 targetRectMin, int2 targetRectMax, const ElementRef& elementRef, ScrollAxis axis = ScrollAxis::Both);
        bool MakeRectVisibleHierarchically(ElementStorage& elementStorage, IntRect targetRect, const ElementRef& elementRef, ScrollAxis axis = ScrollAxis::Both)
        {
            return MakeRectVisibleHierarchically(elementStorage, targetRect.position, targetRect.position + targetRect.size, elementRef, axis);
        }
        bool MakeElementVisibleHierarchically(ElementStorage& elementStorage, const ElementRef& elementRef, ScrollAxis axis = ScrollAxis::Both);
		// cursorPos is in the editor's content space.
        bool MakeTextCursorVisibleInTextBox(ElementStorage& elementStorage, const ElementRef& textEditorRef, int2 cursorPos, int cursorHeight = 1);
    };
}
