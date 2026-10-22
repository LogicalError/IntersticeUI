#include <gui_controls/gui_application.h>

namespace GUI
{

    // ---------------------------------------------------------------------------
    // TabBar
    // ---------------------------------------------------------------------------

    bool GUIApplication::BeginTabBar(ID id, int* selectedIndex,
                                  std::function<void(int, int)> onReorder,
                                  std::function<void(int)> onClose,
                                  std::function<void(int, GUI::int2)> onDragOut,
                                  std::function<void(int, GUI::int2)> dragVisualizer,
                                  StyleRef style, LayoutOverride&& constraints)
    {
        m_tabBarState = {};
        m_tabBarState.barID         = id;
        m_tabBarState.selectedIndex = selectedIndex;
        m_tabBarState.onClose       = std::move(onClose);
        m_tabBarState.onDragOut     = onDragOut; // keep a copy for bookkeeping

        if (style == StyleRef{})
            style = theme.tabBarStyleRef;

        // Wrapped, so tabs flow onto multiple rows when the bar is too narrow.
        // onDragOut/dragVisualizer go through the group so handlers capture them;
        // m_tabBarState is cleared in EndTabBar, before input dispatch runs.
        return BeginReorderableGroup(id, ReorderDirection::Horizontal, true,
                                     std::move(onReorder), style, std::move(constraints),
                                     std::move(onDragOut), std::move(dragVisualizer));
    }

    void GUIApplication::EndTabBar()
    {
        EndReorderableGroup();
        m_tabBarState = {};
    }

    bool GUIApplication::Tab(ID id, std::string_view text, IconID icon, LayoutOverride&& constraints)
    {
        int         tabIndex      = m_tabBarState.nextTabIndex++;
        int*        selectedIndex = m_tabBarState.selectedIndex;
        const auto& onClose       = m_tabBarState.onClose;
        bool        isSelected    = selectedIndex && *selectedIndex == tabIndex;

        if (isSelected) guiStateManager.Construction().PushSelected();

        BeginReorderableItem(id, std::move(constraints));

        // Tab body: container only; icon+text and close button live as children.
        auto capturedIndex = tabIndex;
        GUI::ID bodyID  = idManager.AllocateDynamicID();

        auto state = guiStateManager.GetCurrentState(bodyID);

        auto bodyRes = elementStorage.Add(
            GUI::Element
            {
                .id       = bodyID,
                .eventHandler = [this, id, capturedIndex, selectedIndex](ElementEvent&& event) -> bool
                {
                    if (event.type == GUI::EventType::MouseDown && event.state.LeftPressed())
                    {
                        if (selectedIndex)
                            *selectedIndex = capturedIndex;
                        guiStateManager.Interaction().SetClicked(id);
					    // Not consumed, so MouseUp still arrives for drag-out detection.
                    }
                    return false;
                },
                .styleRef = theme.tabStyleRef,
                .flags    = ElementFlags::PropagatesDescendantState,
                .state    = state
            }, LayoutOverride{}.FlexGrow(1));
        VERIFY(bodyRes.status);
        VERIFY(windowRegistry.Add(bodyRes.ref));

        VERIFY(elementStorage.BeginChildren());

        // Icon+text child: NoInput so input reaches the body; shares the body's state so it restyles with it.
        GUI::ID contentID = idManager.AllocateDynamicID();
        RegisterElement(
            GUI::Element
            {
                .id       = contentID,
                .textID   = textSystem->RegisterText(contentID, icon, text, theme.tabContentStyleRef, state),
                .styleRef = theme.tabContentStyleRef,
                .flags    = ElementFlags::NoInput,
                .state    = state
            }, LayoutOverride::Auto());

        VERIFY(elementStorage.EndChildren());

        EndReorderableItem();

        // Right-click a tab → "Close Panel".
        if (onClose != nullptr && BeginContextMenu(bodyID))
        {
            MenuItem("Close Panel", [onClose, capturedIndex]() { onClose(capturedIndex); });
            EndContextMenu();
        }

        if (isSelected) guiStateManager.Construction().PopSelected();

        return guiStateManager.Interaction().HasBeenClicked(id);
    }

}