#pragma once

#include <cstdint>
#include <vector>

#include <gui_core/elements/gui_element.h>
#include <gui_core/style/gui_style_ref.h>

namespace tachyon
{
    /// @addtogroup controls
    /// @{

    class ElementStorage;
    class StyleManager;
    class FramePipeline;
    struct FrameContext;

    /// Button/toggle groups: every item takes the group's item style (its :first-child and
    /// :last-child rules shape the ends), and a single-item group reverts to "solo", a plain
    /// button, in EndGroup. Owns the group stacks; GUIApplication does the element
    /// registration.
    class ButtonGroupSubsystem
    {
    public:
        /// One open group: its styles and the items registered so far.
        struct ItemState
        {
            tachyon::StyleRef itemStyleRef{};
            tachyon::StyleRef soloStyleRef{};
            int32_t       itemCount = 0;
            tachyon::ElementRef firstRef = tachyon::ElementRef::Invalid();
        };

        ButtonGroupSubsystem(FramePipeline& frame, ElementStorage& elementStorage, StyleManager& styleManager);

        /// Opens a button group at the current nesting depth.
        void BeginButtonGroup(ItemState state);
        /// Closes the innermost button group, restyling a single item as solo.
        void EndButtonGroup();
        /// Opens a toggle group at the current nesting depth.
        void BeginToggleGroup(ItemState state);
        /// Closes the innermost toggle group, restyling a single item as solo.
        void EndToggleGroup();

        /// The innermost button group when it was opened at the current nesting depth,
        /// else nullptr.
        ItemState* GetActiveButton();
        /// The innermost toggle group when it was opened at the current nesting depth,
        /// else nullptr.
        ItemState* GetActiveToggle();

        /// Style for the next item. The caller then stores the first item's registered ref
        /// in state.firstRef.
        tachyon::StyleRef NextItemStyleRef(ItemState& state);

        /// True iff every Begin*Group was matched by End*Group.
        bool IsBalanced() const { return m_buttonStack.empty() && m_toggleStack.empty(); }

    private:
        void OnEndFrame();

        struct StackEntry { ItemState state; uint32_t nestingDepth; };

        std::vector<StackEntry> m_buttonStack;
        std::vector<StackEntry> m_toggleStack;
        ElementStorage&         m_elementStorage;
        StyleManager&           m_styleManager;
    };
    /// @}
}
