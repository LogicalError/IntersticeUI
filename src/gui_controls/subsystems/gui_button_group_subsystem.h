#pragma once

#include <cstdint>
#include <vector>

#include <gui_core/elements/gui_element.h>
#include <gui_core/style/gui_style_ref.h>

namespace interstice
{
    class ElementStorage;
    class StyleManager;
    class FramePipeline;
    struct FrameContext;

    // Button/toggle groups share borders between adjacent items: the first item gets
    // .first, each next one demotes the previous "last" to "mid" and becomes "last",
    // and a single-item group reverts to "solo" in EndGroup. Owns the group stacks;
    // GUIApplication does the element registration.
    class ButtonGroupSubsystem
    {
    public:
        struct ItemState
        {
            interstice::StyleRef firstStyleRef{};
            interstice::StyleRef midStyleRef{};
            interstice::StyleRef lastStyleRef{};
            interstice::StyleRef soloStyleRef{};
            int32_t       itemCount = 0;
            interstice::ElementRef firstRef = interstice::ElementRef::Invalid();
            interstice::ElementRef lastRef  = interstice::ElementRef::Invalid();
        };

        ButtonGroupSubsystem(FramePipeline& frame, ElementStorage& elementStorage, StyleManager& styleManager);

        void BeginButtonGroup(ItemState state);
        void EndButtonGroup();
        void BeginToggleGroup(ItemState state);
        void EndToggleGroup();

        // nullptr if no group is open at the current depth.
        ItemState* GetActiveButton();
        ItemState* GetActiveToggle();

        // Style for the next item. The caller then stores the registered ref in
        // state.firstRef / state.lastRef.
        interstice::StyleRef NextItemStyleRef(ItemState& state);

        // True iff every Begin*Group was matched by End*Group.
        bool IsBalanced() const { return m_buttonStack.empty() && m_toggleStack.empty(); }

    private:
        void OnEndFrame();

        struct StackEntry { ItemState state; uint32_t nestingDepth; };

        std::vector<StackEntry> m_buttonStack;
        std::vector<StackEntry> m_toggleStack;
        ElementStorage&         m_elementStorage;
        StyleManager&           m_styleManager;
    };
}
