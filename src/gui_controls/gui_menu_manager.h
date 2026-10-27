#pragma once

#include <string>

#include <gui_core/input/gui_input_handler_func.h>

#include <gui_core/gui_id.h>
#include <gui_core/gui_base_types.h>

#include <functional>
#include <unordered_map>
#include <vector>

namespace tachyon
{
    /// @addtogroup controls_menus
    /// @{

    class ElementStorage;
    class GUIStateManager;
    class InputManager;
    class IDManager;
    class OSWindowManager;
    class ScrollManagement;

    /// State of the open menus (menu bar dropdowns, context menus, submenus) and their
    /// mouse and keyboard behaviour; one per application.
    class MenuManager
    {
    public:
        static constexpr int kMaxDepth = 8;   ///< deepest submenu nesting, root included

        /// One open menu: the root dropdown or a submenu.
        struct Level
        {
			int     depth          = -1;   ///< root is 0, each submenu level adds 1   
            ID      windowID       = ID::Invalid();
            ID      triggerID      = ID::Invalid();
            ID      activeItemID   = ID::Invalid(); ///< highlighted item (mouse or keyboard)
            ID      scrollUpID     = ID::Invalid(); ///< pre-allocated each frame for stable dynamic IDs
            ID      scrollDownID   = ID::Invalid();
            bool    canScrollUp    = false; ///< from last frame's layout
            bool    canScrollDown  = false; ///< from last frame's layout
            bool    isOpen         = false;
            bool    reopened       = false; ///< closed since last shown: start scrolled to the top
            /// This frame: reserve the icon slot (from last frame's content).
            bool    reserveIconColumn = false;
            bool    itemsHaveIcons    = false; ///< this frame: accumulated as items with icons are added
            std::string filter;                ///< MenuFilter's text; cleared when the menu closes
            /// This frame: MenuFilter holds text, so items not matching it hide.
            bool    filtering         = false;
        };

        Level levels[kMaxDepth];
        int   currentDepth       = -1;
		int   maxOpenDepth       = -1;
        bool  requestCloseAll    = false;
		/// Trigger whose menu closed this frame; its own click doesn't reopen it.
		ID    justClosedTriggerID = ID::Invalid();
        ID    priorFocusID       = ID::Invalid(); ///< focus to restore when all menus close

        /// Depth of a submenu the Left key closed; it doesn't auto-reopen until the parent's
        /// activeItemID changes. -1 = none.
        int   suppressedSubmenuTriggerDepth = -1;

        /// Set when a menu opens: skips the next BeginFrame auto-close (construction hasn't set
        /// the triggerIDs yet) and CloseOnInputOutside for the input that opened it.
        bool  skipActivityCheck  = false;

        bool  mouseHiddenForMenu = false;         ///< set by keyboard nav, cleared on mouse move
        int2  lastMousePos       = {0, 0};        ///< to detect real mouse movement
        bool  focusSubmenuOnOpen = false;          ///< select and focus the first item of the next opened level
        bool  navigatedThisFrame = false;          ///< throttle: at most one Up/Down navigation per frame

        /// Per trigger ID: whether the menu had icons last time, so a reopen reserves
        /// the icon column on its first frame.
        std::unordered_map<ID, bool> menuItemsHaveIcons;

        /// Each menu's own popup window (per trigger ID), so switching menus never
        /// shows another menu's last frame while the new one renders.
        std::unordered_map<ID, ID> popupWindowIDs;
        /// The popup window ID of `triggerID`'s menu, created on first use.
        ID PopupWindowFor(ID triggerID, IDManager& idManager);

        bool              buildingMenuBar = false;   ///< true while a menu bar is being built
        /// Menu-bar triggers in order, registered during construction for Left/Right navigation.
        std::vector<ID>   menuBarTriggers;

        /// True iff `id` was registered as a menu-bar trigger this frame.
        bool IsMenuBarTrigger(ID id) const;



        /// The level below currentDepth, with a window ID assigned on first use.
        Level& GetOrCreateNextLevel(IDManager& idManager);


        /// Unhides the cursor on real mouse movement and closes all menus once none has an
        /// active item or a focused trigger.
        void BeginFrame(GUIStateManager& guiStateManager, OSWindowManager& windowManager);
        /// Updates the scroll indicators from this frame's layout.
        void EndFrame(ElementStorage& elementStorage, GUIStateManager& guiStateManager);

        /// Closes level `fromDepth` and every level below it; window IDs are kept for reuse.
        void CloseFromDepth(int fromDepth);

        /// True while the root menu is open.
        bool IsOpen() const { return levels[0].isOpen; }

        /// True iff every BeginMenu was matched by EndMenu.
        bool IsBalanced() const { return currentDepth == -1; }

        /// Closes all menus once construction is back outside them (in the outermost EndMenu).
        void RequestCloseAll();

        /// Closes all menus on a press or scroll this frame outside them and the open
        /// menu's trigger (which toggles it itself). Call before the input is dispatched.
        void CloseOnInputOutside(const InputManager& inputManager, const ElementStorage& elementStorage,
                                 GUIStateManager& guiStateManager);

        /// Call before BeginWindow(level.windowID): a reopened level starts at the top
        /// (its window ID, and so its scroll offset, persists across close/reopen).
        void PrepareLevelWindow(Level& level, ScrollManagement& scrollManagement);
        /// Opens the level below `parentDepth` for submenu trigger `triggerID`, focusing its
        /// first item.
        void OpenSubmenu(int parentDepth, ID triggerID);

        /// Closes all menus and restores prior focus (the only place that does).
        void CloseAll(GUIStateManager& guiStateManager);

        /// As CloseAll(), and also hides the OS windows at once, for when the frame loop
        /// isn't running (such as during the OS modal move/resize loop).
        void CloseAll(GUIStateManager& guiStateManager, OSWindowManager& windowManager);

        /// Closes the root menu of trigger `id` when it is open, else opens it (see OpenMenu).
        /// Does nothing for the trigger whose menu closed this frame.
        void ToggleOrOpenMenu(GUIStateManager& guiStateManager, ID id, bool focusIntoMenu = true);
        /// Opens the root menu of trigger `id`, replacing any open menu. `focusIntoMenu` focuses
        /// the first item (context menus, dropdowns); false keeps focus on the trigger (menu bar).
        void OpenMenu(GUIStateManager& guiStateManager, ID id, bool focusIntoMenu = true);

        /// Activates item `currentItemID` at `depth`: runs `onClick` and closes all menus, or
        /// for a submenu trigger (`isSubmenu`) opens its submenu. Returns true.
        bool ClickMenu(GUIStateManager& guiStateManager, bool isSubmenu, int depth, ID currentItemID, std::function<void()> onClick);

        /// Handles Escape, Left/Right or Up/Down `key` on item `currentItemID` at `depth`. True if handled.
        bool NavigateMenuSibling(GUIStateManager& guiStateManager, ElementStorage& elementStorage, bool isSubmenu, int depth, ID currentItemID, KeyCode key);

        /// Left/Right between menu-bar triggers; an open dropdown follows. True if handled.
        bool NavigateMenuBar(GUIStateManager& guiStateManager, ID currentTriggerID, KeyCode key);

        /// Focuses the first item of the trigger's dropdown, opening it if needed.
        void EnterMenuFromTrigger(GUIStateManager& guiStateManager, ID triggerID);
    };

    /// Menu item event handler - fires the onClick callback on click/activation.
    EventHandler MakeMenuItemHandler(IDManager& idManager,
                                     GUIStateManager& guiStateManager,
                                     ElementStorage& elementStorage,
                                     MenuManager& menuManager,
                                     int depth,
                                     std::function<void()> onClick);

    /// Like MakeMenuItemHandler, but Right arrow or activation opens a submenu instead of closing.
    EventHandler MakeSubmenuTriggerHandler(IDManager& idManager,
                                           GUIStateManager& guiStateManager,
                                           ElementStorage& elementStorage,
                                           MenuManager& menuManager,
                                           int depth);

    /// @}
} // namespace tachyon
