// Label-to-control binding for the current construction phase.
//
//   - Pending label ID: the label just registered, for the next control to link to.
//   - Focused associated label IDs: labels whose bound control is focused, so
//     they get :focus too (one frame late).
//
// Owned by GUIStateManager.

#pragma once

#include <gui_core/gui_id.h>
#include <gui_core/elements/gui_element.h>

#include <vector>

namespace interstice
{
    class ElementStorage;

    class LabelBinding
    {
        ID m_currentLabelID = ID::Invalid();
        std::vector<ID> m_focusedAssociatedLabelIDs;

    public:
        void RegisterPendingLabel(ID labelID) { m_currentLabelID = labelID; }
        void ClearPendingLabel()              { m_currentLabelID = ID::Invalid(); }
        ID   GetPendingLabelID() const        { return m_currentLabelID; }

        void ClearFocusedAssociatedLabelIDs()   { m_focusedAssociatedLabelIDs.clear(); }
        void AddFocusedAssociatedLabelID(ID id) { m_focusedAssociatedLabelIDs.push_back(id); }
        const std::vector<ID>& GetFocusedAssociatedLabelIDs() const { return m_focusedAssociatedLabelIDs; }

        // The control bound to a label, or Invalid.
        static ID ResolveControlForLabel(const ElementStorage& storage, ID labelID);

        // First IsControl element in rootRef's subtree (pre-order), or Invalid.
        static ID FindFirstControlInSubtree(const ElementStorage& storage, const ElementRef& rootRef);
    };
}
