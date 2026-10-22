#include <gui_core/elements/gui_element_storage.h>

namespace GUI
{
    // ------------------------------------------------------------------------
    // DepthScanCursor (members)
    // ------------------------------------------------------------------------

    const Element& DepthScanCursor::CurrentElement(const ElementStorage& storage) const { return storage.GetElementUnsafe(CurrentRef()); }
    Element& DepthScanCursor::CurrentElement(ElementStorage& storage) const { return storage.GetElementUnsafe(CurrentRef()); }
    const ElementRect& DepthScanCursor::CurrentElementRect(const ElementStorage& storage) const { return storage.GetElementRectUnsafe(CurrentRef()); }
    ElementRect& DepthScanCursor::CurrentElementRect(ElementStorage& storage) const { return storage.GetElementRectUnsafe(CurrentRef()); }

    std::span<const Element> DepthScanCursor::GetChildren(const ElementStorage& storage) const
    {
        if (childRange.Empty()) return {};
        return storage.GetChildrenSpanUnsafe(parentDepth + 1, childRange);
    }

    std::span<Element> DepthScanCursor::GetChildren(ElementStorage& storage) const
    {
        if (childRange.Empty()) return {};
        return storage.GetChildrenSpanUnsafe(parentDepth + 1, childRange);
    }

    std::span<const ElementRect> DepthScanCursor::GetChildRects(const ElementStorage& storage) const
    {
        if (childRange.Empty()) return {};
        return storage.GetChildRectSpanUnsafe(parentDepth + 1, childRange);
    }

    std::span<ElementRect> DepthScanCursor::GetChildRects(ElementStorage& storage) const
    {
        if (childRange.Empty()) return {};
        return storage.GetChildRectSpanUnsafe(parentDepth + 1, childRange);
    }

}//namespace GUI