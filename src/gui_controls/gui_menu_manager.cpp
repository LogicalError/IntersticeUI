// ---------------------------------------------------------------------------
// MenuManager: menu open/close state and keyboard navigation, plus the menu item
// handlers. The menu widgets (BeginMenu / MenuItem / EndMenu) are in controls_menus.cpp.
// ---------------------------------------------------------------------------

#include <gui_controls/gui_menu_manager.h>
#include <gui_controls/gui_default_handlers.h>

#include <gui_core/elements/gui_element_storage.h>
#include <gui_core/input/gui_input_manager.h>

#include <gui_core/windows/gui_os_window_manager.h>
#include <gui_core/windows/gui_window_backend.h>

#include <gui_core/state/gui_state_manager.h>
#include <gui_core/state/gui_scroll_management.h>

#include <gui_core/gui_id_manager.h>

#include <algorithm>
#include <assert.h>

namespace interstice
{

    MenuManager::Level& MenuManager::GetOrCreateNextLevel(IDManager& idManager)
    {
        int nextDepth = currentDepth + 1;
        assert(nextDepth < MenuManager::kMaxDepth);
        if (nextDepth >= MenuManager::kMaxDepth) nextDepth = MenuManager::kMaxDepth - 1; // release: reuse the deepest level

        auto& level = levels[nextDepth];
        if (level.windowID == ID::Invalid())
            level.windowID = idManager.CreateStaticID();
        level.depth = nextDepth;
        return level;
    }


    ID MenuManager::PopupWindowFor(ID triggerID, IDManager& idManager)
    {
        auto [it, added] = popupWindowIDs.try_emplace(triggerID, ID::Invalid());
        if (added)
            it->second = idManager.CreateStaticID();
        return it->second;
    }

    void MenuManager::CloseOnInputOutside(const InputManager& inputManager, const ElementStorage& elementStorage,
                                          GUIStateManager& guiStateManager)
    {
        // Not a menu that this frame's input just opened (a context menu's right-click).
        if (!IsOpen() || skipActivityCheck)
            return;
        // Against the menus' own rects: Windows sends the wheel to the focused popup.
        auto contains = [&](ID id, int2 pos)
        {
            const ElementRef ref = elementStorage.FindElementRefByID(id);
            return ref.IsValid() && elementStorage.GetElementRectUnsafe(ref).rect.Contains(pos);
        };
        auto inMenu = [&](int2 pos)
        {
            for (int i = 0; i < kMaxDepth; i++)
                if (levels[i].isOpen && contains(levels[i].windowID, pos))
                    return true;
            return contains(levels[0].triggerID, pos);
        };
        for (const auto& e : inputManager.GetRecordedEvents())
        {
            if ((e.type != EventType::MouseScroll && e.type != EventType::MouseDown) || e.consumed)
                continue;
            if (!inMenu(e.state.screenPos))
            {
                CloseAll(guiStateManager);
                return;
            }
        }
    }

    void MenuManager::BeginFrame(GUIStateManager& guiStateManager, OSWindowManager& windowManager)
    {
        justClosedTriggerID = ID::Invalid();
        navigatedThisFrame = false;

        // Real mouse movement unhides the cursor hidden by keyboard navigation. Polled,
        // since keyboard events carry no valid screenPos.
        if (mouseHiddenForMenu)
        {
            int2 currentMousePos = windowManager.GetScreenMousePos();
            if (lastMousePos.x == 0 && lastMousePos.y == 0)
            {
                // First frame with hidden cursor: capture the baseline.
                lastMousePos = currentMousePos;
            } else 
            if (currentMousePos.x != lastMousePos.x || currentMousePos.y != lastMousePos.y)
            {
                mouseHiddenForMenu = false;
                guiStateManager.Interaction().SetFocusVisible(false); // the mouse leads again
            }
        }
        else
        {
            lastMousePos = {0, 0};
        }

        // Close everything once no level has an active item and no trigger has focus
        // (click outside, tab away, ...).
        if (IsOpen() && !skipActivityCheck)
        {
            bool menuHasActivity = false;

            for (int i = 0; i < MenuManager::kMaxDepth && !menuHasActivity; i++)
            {
                if (levels[i].isOpen && levels[i].activeItemID != ID::Invalid())
                    menuHasActivity = true;
            }

            if (!menuHasActivity)
            {
                const ID focusedID = guiStateManager.Interaction().GetFocusedID();
                for (int i = 0; i < MenuManager::kMaxDepth && !menuHasActivity; i++)
                {
                    if (levels[i].triggerID == focusedID)
                        menuHasActivity = true;
                }
            }

            if (!menuHasActivity)
            {
                CloseAll(guiStateManager);
            }
        }
        skipActivityCheck = false;
    }

    void MenuManager::EndFrame(ElementStorage& elementStorage, GUIStateManager& guiStateManager)
    {
        // Scroll indicators, from this frame's layout.
        if (IsOpen())
        {
            auto& scrollManagement = guiStateManager.GetScrollManagement();
            for (int i = 0; i < MenuManager::kMaxDepth; i++)
            {
                auto& level = levels[i];
                if (!level.isOpen) continue;
                auto ref = elementStorage.FindElementRefByID(level.windowID);
                if (!ref.IsValid()) continue;
                const auto& rect = elementStorage.GetElementRectUnsafe(ref);
                auto scrollHandle = scrollManagement.GetScrollOffsetHandle(level.windowID);
                int2 offset = (scrollHandle != interstice::ScrollOffsetHandle::Invalid)
                    ? scrollManagement.GetScrollOffset(scrollHandle) : int2{ 0,0 };
                level.canScrollUp = (offset.y < 0);
                level.canScrollDown = (rect.contentBounds.h > rect.innerRect.h) &&
                    (-offset.y + rect.innerRect.h < rect.contentBounds.h);
            }
        }

        if (mouseHiddenForMenu)
            guiStateManager.Cursor().Request(MouseCursor::None);
    }

    void MenuManager::CloseFromDepth(int fromDepth)
    {
        for (int i = kMaxDepth - 1; i >= fromDepth; i--)
        {
            // The window ID is kept: OSWindowManager only hides a closed popup and
            // revives it for the same ID, so a new ID would leak an OS window + swapchain.
            if (levels[i].isOpen) levels[i].reopened = true;
            levels[i].isOpen = false;
            levels[i].activeItemID = ID::Invalid();
            levels[i].triggerID = ID::Invalid();
            levels[i].filter.clear();
        }
    }

    void MenuManager::PrepareLevelWindow(Level& level, ScrollManagement& scrollManagement)
    {
        if (!level.reopened) return;
        level.reopened = false;
        const auto handle = scrollManagement.GetScrollOffsetHandle(level.windowID);
        if (scrollManagement.IsScrollOffsetHandleValid(handle))
            scrollManagement.SetScrollOffset(handle, { 0, 0 });
    }

    void MenuManager::OpenSubmenu(int parentDepth, ID triggerID)
    {
        assert(parentDepth + 1 < MenuManager::kMaxDepth);
        if (parentDepth + 1 >= MenuManager::kMaxDepth) return; // release: no deeper submenu
        auto& childLevel = levels[parentDepth + 1];
        childLevel.triggerID = triggerID;
        childLevel.isOpen = true;
        childLevel.activeItemID = ID::Invalid(); // will be set to first item during construction
        focusSubmenuOnOpen = true;
    }

    void MenuManager::RequestCloseAll()
    {
        requestCloseAll = true;
        if (currentDepth > maxOpenDepth)
            suppressedSubmenuTriggerDepth = -1;
    }

    void MenuManager::CloseAll(GUIStateManager& guiStateManager)
    {
        if (!IsOpen()) return;

        CloseFromDepth(0);

        currentDepth = -1;
        maxOpenDepth = -1;
        requestCloseAll = false;
        mouseHiddenForMenu = false;
        suppressedSubmenuTriggerDepth = -1;
        guiStateManager.Interaction().SetFocusID(priorFocusID);
        priorFocusID = ID::Invalid();
    }

    void MenuManager::CloseAll(GUIStateManager& guiStateManager, OSWindowManager& windowManager)
    {
        if (!IsOpen()) return;

        // Collect the open levels' window IDs before CloseAll marks them closed.
        ID menuWindowIDs[kMaxDepth];
        int count = 0;
        for (int i = 0; i < kMaxDepth; i++)
        {
            if (levels[i].isOpen && levels[i].windowID != ID::Invalid())
                menuWindowIDs[count++] = levels[i].windowID;
        }

        CloseAll(guiStateManager);

        // Hide the OS windows that belonged to the menus we just closed.
        for (int i = 0; i < count; i++)
            windowManager.HidePopupWindow(menuWindowIDs[i]);
    }

    void MenuManager::ToggleOrOpenMenu(GUIStateManager& guiStateManager, ID menuID, bool focusIntoMenu)
    {
        if (justClosedTriggerID == menuID)
        {
            justClosedTriggerID = ID::Invalid();
            return;
        }
        auto& level = levels[0];
        bool isOwnMenuOpen = (level.isOpen && level.triggerID == menuID);
        if (isOwnMenuOpen)
        {
            CloseAll(guiStateManager);
        } else
        {
            OpenMenu(guiStateManager, menuID, focusIntoMenu);
        }
    }

    void MenuManager::OpenMenu(GUIStateManager& guiStateManager, ID menuID, bool focusIntoMenu)
    {
        if (IsOpen())
        {
            CloseFromDepth(0);
            currentDepth = -1;
            maxOpenDepth = -1;
            // priorFocusID is kept from when the first menu opened.
        }

        if (!IsOpen())
            priorFocusID = guiStateManager.Interaction().GetFocusedID();

        auto& level = levels[0];
        level.isOpen = true;
        level.activeItemID = ID::Invalid(); // will be set to first item during construction
        level.triggerID = menuID;

        suppressedSubmenuTriggerDepth = -1;
        skipActivityCheck = true;
        // Menu bar keeps focus on the trigger (focusIntoMenu=false); context menus
        // and dropdowns move focus into the first item.
        focusSubmenuOnOpen = focusIntoMenu;
    }

    bool MenuManager::IsMenuBarTrigger(ID id) const
    {
        for (ID t : menuBarTriggers)
            if (t == id) return true;
        return false;
    }

    bool MenuManager::NavigateMenuBar(GUIStateManager& guiStateManager, ID currentTriggerID, KeyCode key)
    {
        if (key != KeyCode::Left && key != KeyCode::Right)
            return false;

        const int n = static_cast<int>(menuBarTriggers.size());
        if (n <= 1)
            return false;

        int idx = -1;
        for (int i = 0; i < n; i++)
            if (menuBarTriggers[i] == currentTriggerID) { idx = i; break; }
        if (idx < 0)
            return false;

        const int step = (key == KeyCode::Right) ? 1 : -1;
        const ID target = menuBarTriggers[(idx + step + n) % n];

        // If a dropdown is currently showing, the sibling's opens too (focus stays
        // on the trigger); otherwise only the selection moves along the bar.
        const bool dropdownOpen = levels[0].isOpen;
        guiStateManager.Interaction().SetFocusID(target);
        mouseHiddenForMenu = true;
        if (dropdownOpen)
            OpenMenu(guiStateManager, target, /*focusIntoMenu*/ false);
        return true;
    }

    void MenuManager::EnterMenuFromTrigger(GUIStateManager& guiStateManager, ID triggerID)
    {
        auto& level = levels[0];
        if (!level.isOpen || level.triggerID != triggerID)
        {
            // Dropdown not showing yet - open it and drop focus into the first item.
            OpenMenu(guiStateManager, triggerID, /*focusIntoMenu*/ true);
        }
        else
        {
            // Dropdown already open with focus on the trigger: move into it.
            // Clearing activeItemID lets construction re-select + focus the first item.
            focusSubmenuOnOpen = true;
            level.activeItemID = ID::Invalid();
        }
        mouseHiddenForMenu = true;
    }

    bool MenuManager::ClickMenu(GUIStateManager& guiStateManager, bool isSubmenu, int depth, ID currentItemID, std::function<void()> onClick)
    {
        if (!isSubmenu)
        {
            guiStateManager.Interaction().SetClicked(currentItemID);
            const ID focusBefore = guiStateManager.Interaction().GetFocusedID();
            if (onClick) onClick();
            // A command that moved the focus (Undo back to the field it changed) keeps it
            // when the menu closes.
            if (guiStateManager.Interaction().GetFocusedID() != focusBefore)
                priorFocusID = guiStateManager.Interaction().GetFocusedID();
            RequestCloseAll();
        } else
        {
            guiStateManager.Interaction().SetFocusID(currentItemID);
            guiStateManager.TextEditorFocus().Clear();
            OpenSubmenu(depth, currentItemID);
        }
        return true;
    }

    bool MenuManager::NavigateMenuSibling(GUIStateManager& guiStateManager, ElementStorage& elementStorage, bool isSubmenu, int depth, ID currentItemID, KeyCode key)
    {
        auto& level = levels[depth];
        ElementRef menuRef = elementStorage.FindElementRefByID(level.windowID);
        if (!menuRef.IsValid()) 
            return false;

        auto children = elementStorage.GetChildrenSpan(menuRef);
        if (children.empty()) 
            return false;

        switch(key)
        {
            case KeyCode::Escape: 
            {
                CloseAll(guiStateManager); 
                break; 
            }
            case KeyCode::Right:
            {
                if (isSubmenu)
                {
                    OpenSubmenu(depth, currentItemID);
                    return true;
                }
                break;
            }
            case KeyCode::Left:
            {
                if (depth > 0)
                {
                    // Focus the parent trigger; the submenu stays open until the parent's selection moves.
                    auto& parentLevel = levels[depth - 1];
                    parentLevel.activeItemID = level.triggerID;
                    guiStateManager.Interaction().SetFocusID(level.triggerID);
                    mouseHiddenForMenu = true;
                } else
                {
                    CloseAll(guiStateManager);
                }
                return true;
            }
            case KeyCode::Down:
            case KeyCode::Up:
            {
                // One step per frame: key repeat can queue several events in one frame.
                if (navigatedThisFrame)
                    return true; // consume but don't navigate again

                auto isNavigable = [&](const Element& e) {
                        if (e.id == level.scrollUpID || e.id == level.scrollDownID) return false;
                        if ((e.flags & ElementFlags::IsWindow) != ElementFlags::None) return false;
                        if ((e.flags & ElementFlags::NoInput) != ElementFlags::None) return false; // separators
                        return true;
                    };

                int currentIdx = -1;
                for (int i = 0; i < static_cast<int>(children.size()); i++)
                {
                    if (children[i].id == currentItemID) { currentIdx = i; break; }
                }
                if (currentIdx < 0)
                    return false;

                // Menu-bar dropdown: Up at the first item refocuses the trigger, leaving the dropdown open.
                if (key == KeyCode::Up && depth == 0 && IsMenuBarTrigger(level.triggerID))
                {
                    int firstNavigable = -1;
                    for (int i = 0; i < static_cast<int>(children.size()); i++)
                        if (isNavigable(children[i])) { firstNavigable = i; break; }
                    if (firstNavigable >= 0 && children[firstNavigable].id == currentItemID)
                    {
                        guiStateManager.Interaction().SetFocusID(level.triggerID);
                        focusSubmenuOnOpen = false;
                        mouseHiddenForMenu = true;
                        navigatedThisFrame = true;
                        CloseFromDepth(depth + 1);
                        return true;
                    }
                }

                // Next navigable item, wrapping around.
                const int n = static_cast<int>(children.size());
                const int step = (key == KeyCode::Down) ? 1 : -1;
                ID target = ID::Invalid();
                for (int i = 1; i <= n; i++)
                {
                    int idx = (currentIdx + i * step + n) % n;
                    if (isNavigable(children[idx])) 
                    { 
                        target = children[idx].id; 
                        break; 
                    }
                }
                if (target == ID::Invalid())
                {
                    return false;
                }
                level.activeItemID = target;
                mouseHiddenForMenu = true;
                navigatedThisFrame = true;
                suppressedSubmenuTriggerDepth = -1;
                // Close child submenus; construction reopens one if the target is a submenu trigger.
                CloseFromDepth(depth + 1);
                guiStateManager.Interaction().SetFocusID(target);
                return true;
            }
            default: break;
        }
        return false;
    }


    // ---------------------------------------------------------------------------
    // Menu handler factories
    // ---------------------------------------------------------------------------

    namespace
    {
        // Shared implementation for both menu items and submenu triggers.
        EventHandler MakeMenuHandler(IDManager& /*idManager*/,
                                     GUIStateManager& guiStateManager,
                                     ElementStorage& elementStorage,
                                     MenuManager& menuManager,
                                     int depth,
                                     bool isSubmenuTrigger,
                                     std::function<void()> onClick)
        {
            return [&guiStateManager, &elementStorage, &menuManager, depth, isSubmenuTrigger, onClick](ElementEvent&& event)->bool
            {
                switch (event.type)
                {
                    case EventType::MouseDown:
                    {
                        if (!event.state.LeftPressed())
                            break;
                        return menuManager.ClickMenu(guiStateManager, isSubmenuTrigger, depth, event.id, onClick);
                    }
                    case EventType::KeyDown:
                    {
                        if ((guiStateManager.GetCurrentState(event.id) & ElementState::Focused) == ElementState::None)
                            break;
                        if (IsActivationKey(event))
                            return menuManager.ClickMenu(guiStateManager, isSubmenuTrigger, depth, event.id, onClick);
                        return menuManager.NavigateMenuSibling(guiStateManager, elementStorage, isSubmenuTrigger, depth, event.id, event.state.lastKey);
                    }
                    default: break;
                }
                return false;
            };
        }
    } // anonymous namespace

    EventHandler MakeMenuItemHandler(IDManager& idManager,
                                     GUIStateManager& guiStateManager,
                                     ElementStorage& elementStorage,
                                     MenuManager& menuManager,
                                     int depth,
                                     std::function<void()> onClick)
    {
        return MakeMenuHandler(idManager, guiStateManager, elementStorage, menuManager, depth, false, std::move(onClick));
    }

    EventHandler MakeSubmenuTriggerHandler(IDManager& idManager,
                                           GUIStateManager& guiStateManager,
                                           ElementStorage& elementStorage,
                                           MenuManager& menuManager,
                                           int depth)
    {
        return MakeMenuHandler(idManager, guiStateManager, elementStorage, menuManager, depth, true, nullptr);
    }

} // namespace interstice