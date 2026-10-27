#include <gui_controls/gui_application.h>

namespace tachyon
{

    // ---------------------------------------------------------------------------
    // TabBar
    // ---------------------------------------------------------------------------

    bool GUIApplication::BeginTabBar(ID id, int* selectedIndex,
                                  std::function<void(int, int)> onReorder,
                                  std::function<void(int)> onClose,
                                  std::function<void(int, tachyon::int2)> onDragOut,
                                  std::function<void(int, tachyon::int2)> dragVisualizer,
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
        if (m_tabBarState.barID != ID::Invalid())
            guiStateManager.GetElementDataStore().GetOrCreate<TabBarKeyboard>(m_tabBarState.barID).tabCount = m_tabBarState.nextTabIndex;
        EndReorderableGroup();
        m_tabBarState = {};
    }

    bool GUIApplication::Tab(ID id, const GUIContent& content, StyleRef style, LayoutOverride&& constraints)
    {
        const IconID           icon = content.icon;
        const std::string_view text = content.text;
        ShowContentTooltip(id, content);
        if (m_tabBarState.barID == ID::Invalid())
        {
            ReportMisuse("GUIApplication: Tab outside a tab bar (ignored)");
            return false;
        }
        int         tabIndex      = m_tabBarState.nextTabIndex++;
        int*        selectedIndex = m_tabBarState.selectedIndex;
        const auto& onClose       = m_tabBarState.onClose;
        bool        isSelected    = selectedIndex && *selectedIndex == tabIndex;

        if (isSelected) guiStateManager.Construction().PushSelected();

        BeginReorderableItem(id, std::move(constraints));

        // Tab body: container only; icon+text and close button live as children. Its ID is
        // the tab's own, so keyboard focus stays on the tab when tabs move.
        auto capturedIndex = tabIndex;
        const ID barID = m_tabBarState.barID;
        [[maybe_unused]] const bool pushed = idManager.PushParent(id);
        tachyon::ID bodyID  = idManager.AllocateDynamicID("tab_body");
        tachyon::ID contentID = idManager.AllocateDynamicID("tab_content");
        if (pushed) (void)idManager.PopParent();

        // The keyboard reaches the bar at its selected tab (the first without a selection);
        // arrows then move along the tabs.
        auto& keyboard = guiStateManager.GetElementDataStore().GetOrCreate<TabBarKeyboard>(barID);
        if (isSelected || (!selectedIndex && tabIndex == 0))
            tabChainManager.RegisterTabTarget(bodyID);
        if (keyboard.focusIndex == tabIndex)
        {
            guiStateManager.Interaction().SetFocusID(bodyID);
            keyboard.focusIndex = -1;
        }

        auto state = guiStateManager.GetCurrentState(bodyID);

        auto bodyRes = elementStorage.Add(
            tachyon::Element
            {
                .id       = bodyID,
                .eventHandler = [this, id, barID, capturedIndex, selectedIndex](ElementEvent&& event) -> bool
                {
                    if (event.type == tachyon::EventType::MouseDown && event.state.LeftPressed())
                    {
                        if (selectedIndex)
                            *selectedIndex = capturedIndex;
                        guiStateManager.Interaction().SetClicked(id);
					    // Not consumed, so MouseUp still arrives for drag-out detection.
                    }
                    if (event.type == tachyon::EventType::KeyDown &&
                        (guiStateManager.GetCurrentState(event.id) & ElementState::Focused) != ElementState::None)
                    {
                        auto& keys = guiStateManager.GetElementDataStore().GetOrCreate<TabBarKeyboard>(barID);
                        int next = capturedIndex;
                        switch (event.state.lastKey)
                        {
                            case KeyCode::Left:  next = capturedIndex - 1; break;
                            case KeyCode::Right: next = capturedIndex + 1; break;
                            case KeyCode::Home:  next = 0;                 break;
                            case KeyCode::End:   next = keys.tabCount - 1; break;
                            default: return false;
                        }
                        // Moving along the tabs shows the tab moved to, as in a tab list.
                        next = std::clamp(next, 0, std::max(0, keys.tabCount - 1));
                        if (next != capturedIndex)
                        {
                            if (selectedIndex)
                                *selectedIndex = next;
                            keys.focusIndex = next;
                        }
                        return true;
                    }
                    return false;
                },
                .styleRef = style,
                .flags    = ElementFlags::PropagatesDescendantState,
                .state    = state
            }, LayoutOverride{}.FlexGrow(1));
        VERIFY(bodyRes.status);
        VERIFY(windowRegistry.Add(bodyRes.ref));

        VERIFY(elementStorage.BeginChildren());

        // Icon+text child: NoInput so input reaches the body; shares the body's state so it restyles with it.
        RegisterElement(
            tachyon::Element
            {
                .id       = contentID,
                .textID   = textSystem->RegisterText(contentID, icon, text, theme.tabContentStyleRef, state),
                .styleRef = theme.tabContentStyleRef,
                .flags    = ElementFlags::NoInput,
                .state    = state
            }, LayoutOverride::Auto());

        VERIFY(elementStorage.EndChildren());

        EndReorderableItem();

        // Right-click a tab -> "Close Panel".
        if (onClose != nullptr)
        {
            if (BeginContextMenu(bodyID))
                MenuItem("Close Panel", [onClose, capturedIndex]() { onClose(capturedIndex); });
            EndContextMenu();
        }

        if (isSelected) guiStateManager.Construction().PopSelected();

        return guiStateManager.Interaction().HasBeenClicked(id);
    }

}