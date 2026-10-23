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
        auto size = LayoutOverride::Auto();

        ElementFlags flags = ElementFlags::WindowNoCaption | ElementFlags::WindowIsPopup | ElementFlags::Shadows | ElementFlags::WindowNotFocusable;

        if (BeginWindow(tooltipWindowID, std::move(pos), theme.tooltipStyleRef, flags))
        {
            // Set NoInput on the window element so it's passthrough for hit-testing
            {
                auto winRef = elementStorage.FindElementRefByID(tooltipWindowID);
                if (winRef.IsValid())
                    elementStorage.GetElementUnsafe(winRef).flags |= ElementFlags::NoInput;
            }

            auto textID = idManager.AllocateDynamicID();
            RegisterElement(interstice::Element{ .id = textID, .styleRef = theme.tooltipTextStyleRef }, text, LayoutOverride{});

            EndWindow();
        }
    }

    void GUIApplication::SetTooltip(ID targetID, IconID icon, std::string_view text)
    {
        if (!tooltipManager.ShouldShow(targetID))
            return;

        auto pos = LayoutOverride{}.Anchored(targetID,
            AnchorPlacement::OutsideBottom | AnchorPlacement::InsideLeft,
            {0, int16_t(styleManager.Px(4))}, AutoFlipMode::FlipAvoidTarget);
        auto size = LayoutOverride::Auto();

        ElementFlags flags = ElementFlags::WindowNoCaption | ElementFlags::WindowIsPopup | ElementFlags::Shadows | ElementFlags::WindowNotFocusable;

        if (BeginWindow(tooltipWindowID, std::move(pos), theme.tooltipStyleRef, flags))
        {
            auto winRef = elementStorage.FindElementRefByID(tooltipWindowID);
            if (winRef.IsValid())
                elementStorage.GetElementUnsafe(winRef).flags |= ElementFlags::NoInput;

            auto contentID = idManager.AllocateDynamicID();
            RegisterElement(interstice::Element{ .id = contentID, .iconID = icon, .styleRef = theme.tooltipTextStyleRef }, text, LayoutOverride{});
            EndWindow();
        }
    }

    bool GUIApplication::BeginTooltip(ID targetID)
    {
        if (!tooltipManager.ShouldShow(targetID))
            return false;

        auto pos = LayoutOverride{}.Anchored(targetID,
            AnchorPlacement::OutsideBottom | AnchorPlacement::InsideLeft,
            {0, int16_t(styleManager.Px(4))}, AutoFlipMode::FlipAvoidTarget);
        auto size = LayoutOverride::Auto();

        ElementFlags flags = ElementFlags::WindowNoCaption | ElementFlags::WindowIsPopup | ElementFlags::Shadows | ElementFlags::WindowNotFocusable;

        if (BeginWindow(tooltipWindowID, std::move(pos), theme.tooltipStyleRef, flags))
        {
            auto winRef = elementStorage.FindElementRefByID(tooltipWindowID);
            if (winRef.IsValid())
                elementStorage.GetElementUnsafe(winRef).flags |= ElementFlags::NoInput;
            return true;
        }
        return false;
    }

    void GUIApplication::EndTooltip()
    {
        EndWindow();
    }


    // ---------------------------------------------------------------------------
    // Context Menus
    // ---------------------------------------------------------------------------

    bool GUIApplication::BeginContextMenu(ID targetID)
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

        // Anchored below the target element
        LayoutOverride pos = LayoutOverride{}.Anchored(targetID,
            AnchorPlacement::OutsideBottom | AnchorPlacement::InsideLeft,
            {0, 0}, AutoFlipMode::FlipAvoidTarget)
            ;

        menuManager.PrepareLevelWindow(level, guiStateManager.GetScrollManagement());
        if (BeginWindow(level.windowID, std::move(pos), theme.menuStyleRef,
                        ElementFlags::IsMenuWindow))
        {
            windowManager->BringToFront(level.windowID);

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
            menuManager.CloseAll(guiStateManager);
            contextMenuTargetID = ID::Invalid();
            return false;
        }
    }

    void GUIApplication::EndContextMenu()
    {
        EndMenu();
    }
}