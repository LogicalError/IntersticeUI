#pragma once

#include <gui_core/input/gui_input_handler_func.h>

#include <gui_core/gui_id.h>
#include <gui_core/gui_base_types.h>

#include <functional>
#include <unordered_map>
#include <vector>

namespace interstice
{
    class ElementStorage;
    class GUIStateManager;
    class InputManager;
    class IDManager;
    class OSWindowManager;
    class ScrollManagement;

    class MenuManager
    {
    public:
        static constexpr int kMaxDepth = 8;

        struct Level
        {
			int     depth          = -1;   // root is 0, submenus increment depth by 1   
            ID      windowID       = ID::Invalid();
            ID      triggerID      = ID::Invalid();
            ID      activeItemID   = ID::Invalid(); // highlighted item (mouse or keyboard)
            ID      scrollUpID     = ID::Invalid(); // pre-allocated each frame for stable dynamic IDs
            ID      scrollDownID   = ID::Invalid();
            bool    canScrollUp    = false; // from last frame's layout
            bool    canScrollDown  = false;
            bool    isOpen         = false;
            bool    reopened       = false; // closed since last shown: start scrolled to the top
            bool    reserveIconColumn = false; // this frame: reserve the icon slot (from last frame's content)
            bool    itemsHaveIcons    = false; // this frame: accumulated as items with icons are added
        };

        Level levels[kMaxDepth];
        int   currentDepth       = -1;
		int   maxOpenDepth       = -1;
        bool  requestCloseAll    = false;
		ID    justClosedTriggerID = ID::Invalid(); // suppress immediate reopen of the menu whose close was triggered this frame
        ID    priorFocusID       = ID::Invalid(); // focus to restore when all menus close

        // When Left key closes a submenu, suppress auto-reopen until activeItemID changes.
        int   suppressedSubmenuTriggerDepth = -1;

        // Skip auto-close for one frame after opening, before construction updates triggerIDs,
        // and the close on input outside for the input that opened it.
        bool  skipActivityCheck  = false;

        bool  mouseHiddenForMenu = false;         // set by keyboard nav, cleared on mouse move
        int2  lastMousePos       = {0, 0};        // to detect real mouse movement
        bool  focusSubmenuOnOpen = false;          // when true, auto-select first item of newly opened submenu
        bool  navigatedThisFrame = false;          // throttle: at most one Up/Down navigation per frame

        // Per trigger ID: whether the menu had icons last time, so a reopen reserves
        // the icon column on its first frame.
        std::unordered_map<ID, bool> menuItemsHaveIcons;

        // Each menu's own popup window (per trigger ID), so switching menus never
        // shows another menu's last frame while the new one renders.
        std::unordered_map<ID, ID> popupWindowIDs;
        ID PopupWindowFor(ID triggerID, IDManager& idManager);

        // Menu-bar triggers in order, registered during construction for Left/Right navigation.
        bool              buildingMenuBar = false;
        std::vector<ID>   menuBarTriggers;

        // True iff `id` was registered as a menu-bar trigger this frame.
        bool IsMenuBarTrigger(ID id) const;



        Level& GetOrCreateNextLevel(IDManager& idManager);


        void BeginFrame(GUIStateManager& guiStateManager, OSWindowManager& windowManager);
        void EndFrame(ElementStorage& elementStorage, GUIStateManager& guiStateManager);

        void CloseFromDepth(int fromDepth);

        bool IsOpen() const { return levels[0].isOpen; }

        // True iff every BeginMenu was matched by EndMenu.
        bool IsBalanced() const { return currentDepth == -1; }

        void RequestCloseAll();

        // Closes all menus on a press or scroll this frame outside them and the open
        // menu's trigger (which toggles it itself). Call before the input is dispatched.
        void CloseOnInputOutside(const InputManager& inputManager, const ElementStorage& elementStorage,
                                 GUIStateManager& guiStateManager);

        // Call before BeginWindow(level.windowID): a reopened level starts at the top
        // (its window ID, and so its scroll offset, persists across close/reopen).
        void PrepareLevelWindow(Level& level, ScrollManagement& scrollManagement);
        void OpenSubmenu(int parentDepth, ID triggerID);

        // Closes all menus and restores prior focus (the only place that does).
        void CloseAll(GUIStateManager& guiStateManager);

        // Also hides the OS windows at once, for when the frame loop isn't running
        // (e.g. the OS modal move/resize loop).
        void CloseAll(GUIStateManager& guiStateManager, OSWindowManager& windowManager);

        // focusIntoMenu: focus the first item on open (context menus, dropdowns),
        // or keep focus on the trigger (menu bar).
        void ToggleOrOpenMenu(GUIStateManager& guiStateManager, ID id, bool focusIntoMenu = true);
        void OpenMenu(GUIStateManager& guiStateManager, ID id, bool focusIntoMenu = true);

        bool ClickMenu(GUIStateManager& guiStateManager, bool isSubmenu, int depth, ID currentItemID, std::function<void()> onClick);

        bool NavigateMenuSibling(GUIStateManager& guiStateManager, ElementStorage& elementStorage, bool isSubmenu, int depth, ID currentItemID, KeyCode key);

        // Left/Right between menu-bar triggers; an open dropdown follows. True if handled.
        bool NavigateMenuBar(GUIStateManager& guiStateManager, ID currentTriggerID, KeyCode key);

        // Focuses the first item of the trigger's dropdown, opening it if needed.
        void EnterMenuFromTrigger(GUIStateManager& guiStateManager, ID triggerID);
    };

    // Menu item event handler - fires the onClick callback on click/activation.
    EventHandler MakeMenuItemHandler(IDManager& idManager,
                                     GUIStateManager& guiStateManager,
                                     ElementStorage& elementStorage,
                                     MenuManager& menuManager,
                                     int depth,
                                     std::function<void()> onClick);

    // Like MakeMenuItemHandler but Right arrow / activation opens a submenu instead of closing
    EventHandler MakeSubmenuTriggerHandler(IDManager& idManager,
                                           GUIStateManager& guiStateManager,
                                           ElementStorage& elementStorage,
                                           MenuManager& menuManager,
                                           int depth);

} // namespace interstice
