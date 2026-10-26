#include <gui_controls/gui_application.h>

#include <string>

namespace interstice
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
            RegisterElement(interstice::Element{ .id = textID, .styleRef = theme.tooltipTextStyleRef }, text, LayoutOverride{});
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
            RegisterElement(interstice::Element{ .id = contentID, .iconID = icon, .styleRef = theme.tooltipTextStyleRef }, text, LayoutOverride{});
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
        // Detect right-click from recorded input events.
        // Match against the previous frame's hovered element (GetHoveredID is set
        // in the prior EndFrame and is still valid during OnGUI construction).
        if (targetID != ID::Invalid() && inputManager)
        {
            bool hasRightClick = false;
            for (const auto& e : inputManager->GetRecordedEvents())
            {
                if (e.type == interstice::EventType::MouseDown && e.state.RightPressed())
                {
                    hasRightClick = true;
                    break;
                }
            }
            if (hasRightClick && guiStateManager.Interaction().GetHoveredID() == targetID)
            {
                contextMenuTargetID = targetID;
                contextMenuScreenPos.reset();
                if (menuManager.IsOpen())
                    menuManager.CloseAll(guiStateManager);
                menuManager.OpenMenu(guiStateManager, contextMenuWindowID);
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

        // Below the target element, or where OpenContextMenu said.
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