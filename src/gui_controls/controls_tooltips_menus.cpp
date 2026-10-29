#include <gui_controls/gui_application.h>

#include <algorithm>
#include <string>

namespace tachyon
{


    // ---------------------------------------------------------------------------
    // Tooltips
    // ---------------------------------------------------------------------------

    void GUIApplication::SetTooltip(ID targetID, std::string_view text)
    {
        if (!tooltipManager.ShouldShow(targetID))
            return;

        auto pos = LayoutOverride{}.Anchored(targetID,
            AnchorPlacement::OutsideBottom | AnchorPlacement::InsideLeft,
            {0, int16_t(styleManager.Px(4))}, AutoFlipMode::FlipAvoidTarget);

        ElementFlags flags = ElementFlags::WindowNoCaption | ElementFlags::WindowIsPopup | ElementFlags::Shadows | ElementFlags::WindowNotFocusable;

        if (BeginWindow(tooltipWindowID, theme.tooltipStyleRef, std::move(pos), flags))
        {
            // Set NoInput on the window element so it's passthrough for hit-testing
            {
                auto winRef = elementStorage.FindElementRefByID(tooltipWindowID);
                if (winRef.IsValid())
                    elementStorage.GetElementUnsafe(winRef).flags |= ElementFlags::NoInput;
            }

            auto textID = idManager.AllocateDynamicID();
            RegisterElement(tachyon::Element{ .id = textID, .styleRef = theme.tooltipTextStyleRef }, text, LayoutOverride{});
        }
        EndWindow();
    }

    void GUIApplication::SetTooltip(ID targetID, IconID icon, std::string_view text)
    {
        if (!tooltipManager.ShouldShow(targetID))
            return;

        auto pos = LayoutOverride{}.Anchored(targetID,
            AnchorPlacement::OutsideBottom | AnchorPlacement::InsideLeft,
            {0, int16_t(styleManager.Px(4))}, AutoFlipMode::FlipAvoidTarget);

        ElementFlags flags = ElementFlags::WindowNoCaption | ElementFlags::WindowIsPopup | ElementFlags::Shadows | ElementFlags::WindowNotFocusable;

        if (BeginWindow(tooltipWindowID, theme.tooltipStyleRef, std::move(pos), flags))
        {
            auto winRef = elementStorage.FindElementRefByID(tooltipWindowID);
            if (winRef.IsValid())
                elementStorage.GetElementUnsafe(winRef).flags |= ElementFlags::NoInput;

            auto contentID = idManager.AllocateDynamicID();
            RegisterElement(tachyon::Element{ .id = contentID, .iconID = icon, .styleRef = theme.tooltipTextStyleRef }, text, LayoutOverride{});
        }
        EndWindow();
    }

    bool GUIApplication::BeginTooltip(ID targetID)
    {
        const bool open = OpenTooltip(targetID);
        m_tooltipOpen.push_back(open);
        return open;
    }

    bool GUIApplication::OpenTooltip(ID targetID)
    {
        if (!tooltipManager.ShouldShow(targetID))
            return false;

        auto pos = LayoutOverride{}.Anchored(targetID,
            AnchorPlacement::OutsideBottom | AnchorPlacement::InsideLeft,
            {0, int16_t(styleManager.Px(4))}, AutoFlipMode::FlipAvoidTarget);

        ElementFlags flags = ElementFlags::WindowNoCaption | ElementFlags::WindowIsPopup | ElementFlags::Shadows | ElementFlags::WindowNotFocusable;

        if (BeginWindow(tooltipWindowID, theme.tooltipStyleRef, std::move(pos), flags))
        {
            auto winRef = elementStorage.FindElementRefByID(tooltipWindowID);
            if (winRef.IsValid())
                elementStorage.GetElementUnsafe(winRef).flags |= ElementFlags::NoInput;
            return true;
        }
        EndWindow();   // the window's skipped scope
        return false;
    }

    // Called for every BeginTooltip, shown or not.
    void GUIApplication::EndTooltip()
    {
        if (m_tooltipOpen.empty())
        {
            ReportMisuse("GUIApplication: EndTooltip without BeginTooltip (ignored)");
            return;
        }
        const bool open = m_tooltipOpen.back();
        m_tooltipOpen.pop_back();
        if (open)
            EndWindow();
    }


    // ---------------------------------------------------------------------------
    // Context Menus
    // ---------------------------------------------------------------------------

    bool GUIApplication::BeginContextMenu(ID targetID)
    {
        const bool open = OpenContextMenuWindow(targetID);
        m_menuOpen.push_back(open);   // EndContextMenu is EndMenu
        return open;
    }

    bool GUIApplication::OpenContextMenuWindow(ID targetID)
    {
        // A right-click on the target (last frame's hovered element) or on a part of it that
        // it takes the states of, or Shift+F10 / the Menu key with the focus there, opens
        // it. The first target that claims the request this frame wins: a control's menu,
        // declared right after it, over the one of a row or node it is in.
        if (targetID != ID::Invalid() && inputManager && !m_contextMenuClaimed)
        {
            bool byMouse = false, byKeyboard = false;
            int2 pressedAt{};
            for (const auto& e : inputManager->GetRecordedEvents())
            {
                if (e.type == tachyon::EventType::MouseDown && e.state.RightPressed())
                {
                    byMouse   = true;
                    pressedAt = e.state.screenPos;
                }
                if (e.type == tachyon::EventType::KeyDown &&
                    (e.state.lastKey == tachyon::KeyCode::Application || e.state.lastKey == tachyon::KeyCode::Menu ||
                     (e.state.lastKey == tachyon::KeyCode::F10 && (e.state.modifiers & KeyModifier::Shift) != KeyModifier::None)))
                    byKeyboard = true;
            }
            auto isIn = [targetID](ID leaf, const std::vector<ID>& around)
            {
                return leaf != ID::Invalid() &&
                       (leaf == targetID || std::find(around.begin(), around.end(), targetID) != around.end());
            };
            const auto& interaction = guiStateManager.Interaction();
            const bool mouseHit = byMouse && isIn(interaction.GetHoveredID(), interaction.GetPropagatedHoverIDs());
            const bool keyHit   = !mouseHit && byKeyboard && isIn(interaction.GetFocusedID(), interaction.GetPropagatedFocusedIDs());
            if (mouseHit || keyHit)
            {
                m_contextMenuClaimed = true;
                contextMenuTargetID = targetID;
                // At the pointer when right-clicked; below the target from the keyboard.
                if (mouseHit)
                    contextMenuScreenPos = pressedAt;
                else
                    contextMenuScreenPos.reset();
                if (menuManager.IsOpen())
                    menuManager.CloseAll(guiStateManager);
                menuManager.OpenMenu(guiStateManager, contextMenuWindowID);
                if (keyHit)
                    menuManager.mouseHiddenForMenu = true;   // the keyboard leads in the menu
            }
        }

        // If context menu is open for this target, declare the popup window
        if (contextMenuTargetID != targetID)
            return false;
        if (!menuManager.IsOpen() || menuManager.levels[0].triggerID != contextMenuWindowID)
        {
            contextMenuTargetID = ID::Invalid();
            return false;
        }

        auto& level = menuManager.GetOrCreateNextLevel(idManager);

        // Where it was right-clicked or OpenContextMenu said, else below the target element.
        LayoutOverride pos = contextMenuScreenPos
            ? LayoutOverride{}.Position(contextMenuScreenPos->x, contextMenuScreenPos->y)
            : LayoutOverride{}.Anchored(targetID,
                  AnchorPlacement::OutsideBottom | AnchorPlacement::InsideLeft,
                  {0, 0}, AutoFlipMode::FlipAvoidTarget);

        menuManager.PrepareLevelWindow(level, guiStateManager.GetScrollManagement());
        if (BeginWindow(level.windowID, theme.menuStyleRef, std::move(pos),
                        ElementFlags::IsMenuWindow))
        {
            menuManager.currentDepth = level.depth;
            menuManager.maxOpenDepth = std::max(menuManager.maxOpenDepth, menuManager.currentDepth);
            level.reserveIconColumn = menuManager.menuItemsHaveIcons[level.triggerID];
            level.itemsHaveIcons    = false;
            level.scrollUpID = idManager.AllocateDynamicID();
            level.scrollDownID = idManager.AllocateDynamicID();
            return true;
        }
        else
        {
            EndWindow();   // the popup window's skipped scope
            menuManager.CloseAll(guiStateManager);
            contextMenuTargetID = ID::Invalid();
            return false;
        }
    }

    void GUIApplication::OpenContextMenu(ID targetID, int2 screenPos)
    {
        contextMenuTargetID  = targetID;
        contextMenuScreenPos = screenPos;
        if (menuManager.IsOpen())
            menuManager.CloseAll(guiStateManager);
        menuManager.OpenMenu(guiStateManager, contextMenuWindowID);
    }

    void GUIApplication::EndContextMenu()
    {
        EndMenu();
    }
}