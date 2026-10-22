#include <gui_controls/gui_label_binding.h>
#include <gui_core/elements/gui_element_storage.h>

namespace GUI
{
    namespace
    {
        inline bool HasElementFlag(const Element& element, ElementFlags flag)
        {
            return (element.flags & flag) != ElementFlags::None;
        }
    }

    ID LabelBinding::FindFirstControlInSubtree(const ElementStorage& storage, const ElementRef& rootRef)
    {
        if (!rootRef.IsValid())
            return ID::Invalid();

        const Element& rootElement = storage.GetElementUnsafe(rootRef);
        if (HasElementFlag(rootElement, ElementFlags::IsControl))
            return rootElement.id;

        const IndexRange children = storage.GetChildRange(rootRef);
        for (uint32_t i = children.begin; i < children.end; i++)
        {
            const ElementRef childRef{ rootRef.depth + 1, i };
            const ID found = FindFirstControlInSubtree(storage, childRef);
            if (found != ID::Invalid())
                return found;
        }

        return ID::Invalid();
    }

    ID LabelBinding::ResolveControlForLabel(const ElementStorage& storage, ID labelID)
    {
        if (labelID == ID::Invalid())
            return ID::Invalid();

        const ElementRef labelRef = storage.FindElementRefByID(labelID);
        if (!labelRef.IsValid())
            return ID::Invalid();

        const Element& labelElement = storage.GetElementUnsafe(labelRef);
        if (!HasElementFlag(labelElement, ElementFlags::IsLabel))
            return ID::Invalid();

        const ElementRef parentRef = storage.GetParentRef(labelRef);
        if (!parentRef.IsValid())
            return ID::Invalid();

        const IndexRange siblingRange = storage.GetChildRange(parentRef);
        const uint32_t nextSiblingIndex = labelRef.index + 1;
        if (nextSiblingIndex >= siblingRange.end)
            return ID::Invalid();

        const ElementRef nextSiblingRef{ labelRef.depth, nextSiblingIndex };
        return FindFirstControlInSubtree(storage, nextSiblingRef);
    }
}
