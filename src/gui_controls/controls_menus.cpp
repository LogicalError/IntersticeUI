#include <gui_controls/gui_application_internal.h>

#include <algorithm>

#include <array>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <string>
#include <chrono>

namespace interstice
{

    // ---------------------------------------------------------------------------
    // Menu widgets (BeginMenu / MenuItem / EndMenu). MenuManager itself lives in gui_core.
    // ---------------------------------------------------------------------------


    static void ShowScrollUpButton(GUIApplication& application, ID id, ID windowID, const StyleRef& styleRef, int32_t windowWidth)
    {
        const int32_t borderWidth = application.styleManager.Px(1);
        const int32_t buttonWidth = std::max(0, windowWidth - borderWidth * 2);
        application.RegisterElement(interstice::Element{
            .id = id,
            .eventHandler = [&application, windowID = windowID](ElementEvent&& event) -> bool {
                if (event.type == EventType::MouseDown && event.state.LeftPressed()) {
                    auto& scrollManagement = application.guiStateManager.GetScrollManagement();
                    auto handle = scrollManagement.GetOrCreateScrollOffsetHandle(windowID);
                    int2 offset = scrollManagement.GetScrollOffset(handle);
                    offset.y = std::min(0, offset.y + application.styleManager.Px(60));
                    scrollManagement.SetScrollOffset(handle, offset);
                    return true;
                }
                return false;
            },
            .styleRef = styleRef
            }, "...",
            LayoutOverride::FixedWidth(Pixels{ buttonWidth }).Anchored(windowID, AnchorPlacement::InsideTopLeft, {(int16_t)borderWidth, (int16_t)borderWidth}));
    }

    static void ShowScrollDownButton(GUIApplication& application, ID id, ID windowID, const StyleRef& styleRef, int32_t windowWidth)
    {
        const int32_t borderWidth = application.styleManager.Px(1);
        const int32_t buttonWidth = std::max(0, windowWidth - borderWidth * 2);
        application.RegisterElement(interstice::Element{
                .id = id,
                .eventHandler = [&application, windowID = windowID](ElementEvent&& event) -> bool {
                    if (event.type == EventType::MouseDown && event.state.LeftPressed()) {
                        auto& scrollManagement = application.guiStateManager.GetScrollManagement();
                        auto handle = scrollManagement.GetOrCreateScrollOffsetHandle(windowID);
                        int2 offset = scrollManagement.GetScrollOffset(handle);
                        offset.y -= application.styleManager.Px(60);
                        scrollManagement.SetScrollOffset(handle, offset);
                        return true;
                    }
                    return false;
                },
                .styleRef = styleRef
            }, "...",
                LayoutOverride::FixedWidth(Pixels{ buttonWidth }).Anchored(windowID, AnchorPlacement::InsideBottomLeft, {(int16_t)borderWidth, (int16_t)-borderWidth}));
    }

    static void MenuTriggerButton(GUIApplication& application, ID menuID, std::string_view text, StyleRef buttonStyle, bool isMenuBar, LayoutOverride&& constraints)
    {
        auto handler = [&menuManager = application.menuManager, &idManager = application.idManager, &guiStateManager = application.guiStateManager, menuID, isMenuBar](ElementEvent&& event) -> bool
        {
            switch (event.type)
            {
                case EventType::MouseDown:
                {
                    if (!event.state.LeftPressed())
                        break;

                    guiStateManager.Interaction().SetFocusID(event.id);
                    guiStateManager.TextEditorFocus().Clear();
                    guiStateManager.Interaction().SetClicked(event.id);
                    // Menu bar keeps keyboard focus on the trigger (the bar item stays
                    // selected); a standalone dropdown drops focus into its list.
                    menuManager.ToggleOrOpenMenu(guiStateManager, menuID, /*focusIntoMenu*/ !isMenuBar);
                    return true;
                }

                case EventType::KeyDown:
                {
                    if ((guiStateManager.GetCurrentState(event.id) & ElementState::Focused) == ElementState::None)
                        break;

                    if (IsActivationKey(event))
                    {
                        guiStateManager.Interaction().SetClicked(event.id);
                        menuManager.ToggleOrOpenMenu(guiStateManager, menuID, /*focusIntoMenu*/ !isMenuBar);
                        menuManager.mouseHiddenForMenu = true;
                        return true;
                    }

                    // Menu bar: Left/Right move between sibling triggers.
                    if (isMenuBar && (event.state.lastKey == KeyCode::Left || event.state.lastKey == KeyCode::Right))
                        return menuManager.NavigateMenuBar(guiStateManager, menuID, event.state.lastKey);

                    // Down opens the dropdown; for a menu bar it also moves focus
                    // into the first item (the dropdown was already shown on open).
                    if (event.state.lastKey == KeyCode::Down)
                    {
                        if (isMenuBar)
                            menuManager.EnterMenuFromTrigger(guiStateManager, menuID);
                        else
                            menuManager.OpenMenu(guiStateManager, menuID);
                        menuManager.mouseHiddenForMenu = true;
                        return true;
                    }

                    // Menu bar: Escape exits the bar entirely.
                    if (isMenuBar && event.state.lastKey == KeyCode::Escape)
                    {
                        menuManager.CloseAll(guiStateManager);
                        return true;
                    }
                    break;
                }
            }
            return false;
        };

        // A dropdown shows an arrow after its text, pointing up (and the button selected)
        // while its menu is open; a menu-bar item has no arrow.
        const auto& root = application.menuManager.levels[0];
        const bool  open = !isMenuBar && root.isOpen && root.triggerID == menuID;
        const IconID arrow = isMenuBar ? IconID::Invalid
                           : open      ? application.theme.dropdownArrowUpIcon : application.theme.dropdownArrowIcon;
        if (open) application.guiStateManager.Construction().PushSelected();
        application.RegisterControl(interstice::Element{ .id = menuID, .eventHandler = std::move(handler), .styleRef = buttonStyle },
                            [&application, arrow, text](interstice::Element& element) {
                                return application.textSystem->RegisterText(element.id, arrow, text, element.styleRef, element.state);
                            },
                            std::move(constraints));
        if (open) application.guiStateManager.Construction().PopSelected();

        // Snapshot the trigger's final screen rect after layout; next frame's BeginMenu
        // uses it to match the popup width and to detect side-flipped placement.
        // Rects aren't available at construction time (cleared each BeginConstruction).
        auto triggerRef = application.elementStorage.FindElementRefByID(menuID);
        if (triggerRef.IsValid())
        {
            auto& triggerRects = GUIApplicationInternals::MenuRects(application).TriggerRects();
		    [[maybe_unused]]
            auto result =
            application.elementStorage.AddPostLayout(triggerRef,
                [&triggerRects, menuID](interstice::PostLayoutContext& ctx)
                {
                    const interstice::IntRect r = ctx.ElementRect();
                    if (r.w > 0 && r.h > 0) triggerRects.insert_or_assign(menuID, r);
                });
        }
    }


    bool GUIApplication::BeginMenuBar(ID id, StyleRef style, LayoutOverride&& constraints)
    {
        // While a bar menu is open, hovering another top-level trigger switches to it.
        // Done before any BeginMenu (using last frame's trigger list) so two dropdowns
        // never show at once. Skipped while keyboard-navigating.
        if (!menuManager.mouseHiddenForMenu && menuManager.IsOpen()
            && menuManager.IsMenuBarTrigger(menuManager.levels[0].triggerID))
        {
            const ID hovered = guiStateManager.Interaction().GetHoveredID();
            if (hovered != menuManager.levels[0].triggerID && menuManager.IsMenuBarTrigger(hovered))
            {
                guiStateManager.Interaction().SetFocusID(hovered);
                menuManager.OpenMenu(guiStateManager, hovered, /*focusIntoMenu*/ false);
            }
        }

        // Mark the construction scope so BeginMenu triggers inside register as
        // keyboard-navigable menu-bar items (see MenuTriggerButton / NavigateMenuBar).
        menuManager.buildingMenuBar = true;
        menuManager.menuBarTriggers.clear();
        return BeginHorizontal(id, style, std::move(constraints));
    }

    void GUIApplication::EndMenuBar()
    {
        EndHorizontal();
        menuManager.buildingMenuBar = false;
    }

    bool GUIApplication::BeginTitleBar(ID id, StyleRef style, LayoutOverride&& constraints)
    {
        m_titleBarStack.push_back(id);
        return BeginMenuBar(id, style, std::move(constraints));
    }

    void GUIApplication::EndTitleBar()
    {
        if (m_titleBarStack.empty())
        {
            ReportMisuse("GUIApplication: EndTitleBar without BeginTitleBar (ignored)");
            return;
        }
        const ID barID = m_titleBarStack.back();
        m_titleBarStack.pop_back();

        const ID          windowID  = windowRegistry.GetCurrentWindowID();
        const ElementRef  windowRef = windowRegistry.GetWindowRef(windowID);
        const bool        ownsChrome = elementStorage.IsValidRef(windowRef)
            && (elementStorage.GetElementUnsafe(windowRef).flags & ElementFlags::WindowCustomTitleBar) != ElementFlags::None;

        TitleBarInfo bar{ .barID = barID, .captions = std::move(m_titleBarCaptions) };
        m_titleBarCaptions.clear();
        if (ownsChrome)
        {
            [[maybe_unused]] const bool pushed = idManager.PushParent(barID);
            Button(idManager.AllocateDynamicID("minimize"), theme.windowMinimizeIcon,
                   [this, windowID] { windowManager->SetWindowMode(windowID, WindowMode::Minimized); }, theme.titleBarButtonStyleRef);
            // Maximized restores; otherwise (fullscreen too) it maximizes.
            const bool maximized = windowManager->GetWindowMode(windowID) == WindowMode::Maximized;
            bar.maximizeID = idManager.AllocateDynamicID("maximize");
            Button(bar.maximizeID, maximized ? theme.windowRestoreIcon : theme.windowMaximizeIcon,
                   [this, windowID, maximized] {
                       windowManager->SetWindowMode(windowID, maximized ? WindowMode::Normal : WindowMode::Maximized);
                   }, theme.titleBarButtonStyleRef);
            const bool confirmClose = (elementStorage.GetElementUnsafe(windowRef).flags & ElementFlags::WindowConfirmClose) != ElementFlags::None;
            Button(idManager.AllocateDynamicID("close"), theme.tabCloseIcon,
                   [this, windowID, confirmClose] {
                       if (confirmClose) m_closeRequests.insert(windowID);   // the app decides (CloseRequested)
                       else              (void)windowManager->RequestUserClose(windowID);
                   }, theme.titleBarCloseButtonStyleRef);
            if (pushed)
                (void)idManager.PopParent();
        }
        EndMenuBar();
        m_titleBars[windowID] = bar;
    }

    WindowHitArea GUIApplication::TitleBarHitTest(ID windowID, int2 screenPos) const
    {
        const auto it = m_titleBars.find(windowID);
        if (it == m_titleBars.end())
            return WindowHitArea::Client;
        const ElementRef barRef = elementStorage.FindElementRefByID(it->second.barID);
        if (!barRef.IsValid() || !elementStorage.GetElementRectUnsafe(barRef).rect.Contains(screenPos))
            return WindowHitArea::Client;

        const ID hit = elementStorage.GetElementIDAt(windowRegistry.GetWindowRef(windowID), screenPos);
        if (hit == it->second.maximizeID)
            return WindowHitArea::MaximizeButton;
        // The bar itself, and anything in it that takes no input (spacers, the title).
        const ElementRef hitRef = elementStorage.FindElementRefByID(hit);
        if (hit == it->second.barID || !hitRef.IsValid())
            return WindowHitArea::Caption;
        if (std::find(it->second.captions.begin(), it->second.captions.end(), hit) != it->second.captions.end())
            return WindowHitArea::Caption;
        const Element& element = elementStorage.GetElementUnsafe(hitRef);
        const bool interactive = element.eventHandler || (element.flags & ElementFlags::IsControl) != ElementFlags::None;
        return interactive ? WindowHitArea::Client : WindowHitArea::Caption;
    }


    interstice::ElementRef GUIApplication::MenuItemRow(ID id, interstice::EventHandler handler, StyleRef rowStyle,
                                                bool reserveIcon, IconID icon, std::string_view text,
                                                std::string_view trailing, StyleRef trailingStyle,
                                                LayoutOverride&& constraints, bool destructive)
    {
        const interstice::ElementState rowState = guiStateManager.GetCurrentState(id);
        const bool selected = (rowState & interstice::ElementState::Selected) != interstice::ElementState::None;

        interstice::Element row{};
        row.id           = id;
        row.eventHandler = std::move(handler);
        row.styleRef     = rowStyle;
        row.state        = rowState;
        // The row is the focusable control; its children are non-interactive.
        interstice::ElementRef rowRef = RegisterControl(std::move(row), std::move(constraints));

        VERIFY(elementStorage.BeginChildren());

        // Left column - present for every item in a menu that uses icons or has a
        // checkable (selected) item, so labels line up. A selected item shows a
        // checkmark here; otherwise the item's own icon, if any.
        if (reserveIcon)
        {
            interstice::Element iconEl{};
            iconEl.id       = idManager.AllocateDynamicID();
            iconEl.flags    = interstice::ElementFlags::NoInput;
            iconEl.styleRef = destructive ? theme.menuItemDestructiveIconStyleRef : theme.menuItemIconStyleRef;
            if (selected)
                RegisterControl(std::move(iconEl),
                    [this](interstice::Element& e) { return textSystem->RegisterText(e.id, theme.menuCheckIcon, std::string_view{}, e.styleRef, e.state); },
                    interstice::LayoutOverride{});
            else
                RegisterControl(std::move(iconEl),
                    [this, icon](interstice::Element& e) { return textSystem->RegisterText(e.id, icon, std::string_view{}, e.styleRef, e.state); },
                    interstice::LayoutOverride{});
        }

        // Label fills the row and pushes any trailing text to the right edge.
        {
            interstice::Element labelEl{};
            labelEl.id       = idManager.AllocateDynamicID();
            labelEl.flags    = interstice::ElementFlags::NoInput;
            labelEl.styleRef = destructive ? theme.menuItemDestructiveLabelStyleRef : theme.menuItemLabelStyleRef;
            RegisterControl(std::move(labelEl),
                [this, text](interstice::Element& e) { return textSystem->RegisterText(e.id, IconID::Invalid, text, e.styleRef, e.state); },
                interstice::LayoutOverride{});
        }

        // Trailing keyboard shortcut or submenu arrow, pinned right.
        if (!trailing.empty())
        {
            interstice::Element trailEl{};
            trailEl.id       = idManager.AllocateDynamicID();
            trailEl.flags    = interstice::ElementFlags::NoInput;
            trailEl.styleRef = trailingStyle;
            RegisterControl(std::move(trailEl),
                [this, trailing](interstice::Element& e) { return textSystem->RegisterText(e.id, IconID::Invalid, trailing, e.styleRef, e.state); },
                interstice::LayoutOverride{});
        }

        VERIFY(elementStorage.EndChildren());
        return rowRef;
    }


    bool GUIApplication::BeginMenu(ID menuID, std::string_view text, StyleRef buttonStyle, StyleRef menuStyle, StyleRef itemStyle, LayoutOverride&& constraints)
    {
        const bool open = OpenMenu(menuID, text, buttonStyle, menuStyle, itemStyle, std::move(constraints));
        m_menuOpen.push_back(open);
        return open;
    }

    bool GUIApplication::OpenMenu(ID menuID, std::string_view text, StyleRef buttonStyle, StyleRef menuStyle, StyleRef itemStyle, LayoutOverride&& constraints)
    {
        auto& level = menuManager.GetOrCreateNextLevel(idManager);
	    assert(level.windowID != ID::Invalid()); // must have a valid window ID for scroll buttons

        if (menuManager.currentDepth == -1)
        {
            // Register as a menu-bar trigger (ordered) so Left/Right nav is
            // data-driven across whatever triggers the app placed in the bar.
            if (menuManager.buildingMenuBar)
                menuManager.menuBarTriggers.push_back(menuID);
		    MenuTriggerButton(*this, menuID, text, buttonStyle, menuManager.buildingMenuBar, std::move(constraints));
        } else
        {
            // Inside another menu: create submenu trigger item
            auto& parentLevel = menuManager.levels[menuManager.currentDepth];

            // Register submenu trigger item (has its own handler for Right arrow).
            // Same row layout as a MenuItem, but the trailing slot shows a submenu
            // arrow instead of a shortcut so triggers align with sibling items.
            MenuItemRow(menuID,
                MakeSubmenuTriggerHandler(idManager, guiStateManager, elementStorage, menuManager, menuManager.currentDepth),
                itemStyle, parentLevel.reserveIconColumn, IconID::Invalid, text,
                "\xE2\x80\xBA" /* U+203A single right angle quote */, theme.menuItemArrowStyleRef,
                LayoutOverride{});

            // Auto-select only when focus moves into the menu (see MenuItem) - keeps
            // a menu-bar dropdown from cascading this submenu open before navigation.
            if (parentLevel.isOpen && parentLevel.activeItemID == ID::Invalid() && menuManager.focusSubmenuOnOpen)
            {
                parentLevel.activeItemID = menuID;
                guiStateManager.Interaction().SetFocusID(menuID);
            }

            // Update activeItemID on hover (only when mouse is not hidden for keyboard nav)
            if (!menuManager.mouseHiddenForMenu && guiStateManager.Interaction().GetHoveredID() == menuID)
            {
                if (parentLevel.activeItemID != menuID)
                {
                    parentLevel.activeItemID = menuID;
                    guiStateManager.Interaction().SetFocusID(menuID);
                    menuManager.suppressedSubmenuTriggerDepth = -1;
                }
            }

            // Open submenu when this trigger is the active item.
            // The submenu is always shown when navigating to a trigger (mouse or keyboard),
            // but focus only moves into it on mouse hover - keyboard requires Right arrow.
            if (parentLevel.activeItemID == menuID)
            {
                bool suppressed = (menuManager.suppressedSubmenuTriggerDepth == level.depth);
                if (!suppressed && (!level.isOpen || level.triggerID != menuID))
                {
                    // Close any previously open sibling submenu
                    menuManager.CloseFromDepth(level.depth);
                    level.isOpen = true;
                    level.triggerID = menuID;
                    level.activeItemID = ID::Invalid(); // will be set to first child during construction
                    // Mouse hover opens submenu and focuses into it; keyboard nav shows it but keeps focus on trigger
                    menuManager.focusSubmenuOnOpen = !menuManager.mouseHiddenForMenu;
                }
            }
            else if (level.isOpen && level.triggerID == menuID)
            {
                // Active item moved away from this trigger - close our submenu
                // (but only if no child level has an active item, i.e. we're not in a deeper submenu)
                bool childHasActive = false;
                for (int i = level.depth; i < MenuManager::kMaxDepth && menuManager.levels[i].isOpen; i++)
                {
                    if (menuManager.levels[i].activeItemID != ID::Invalid())
                    {
                        childHasActive = true;
                        break;
                    }
                }
                if (!childHasActive)
                {
                    menuManager.CloseFromDepth(level.depth);
                }
            }
        }

        // Check if this submenu should be shown
        if (level.isOpen && level.triggerID == menuID && level.windowID != ID::Invalid())
        {
            if (menuManager.currentDepth == -1)
            {
                menuManager.maxOpenDepth = -1;
            }

            AnchorPlacement anchor = (menuManager.currentDepth == -1)
                ? AnchorPlacement::OutsideBottom | AnchorPlacement::InsideLeft
                : AnchorPlacement::InsideTop | AnchorPlacement::OutsideRight;

            // Anchor popup below the trigger button (auto-flip, avoid overlapping trigger)
            auto pos = LayoutOverride{}.Anchored(menuID, anchor, {}, AutoFlipMode::FlipAvoidTarget);

            // Clamp menu height to monitor work area.
            const auto& workArea = windowManager->GetPrimaryMonitor().workArea;

            // Top-level width, based on where the popup landed last frame:
            //   - below/above the trigger: at least the trigger's width
            //   - auto-flipped to its side: natural content width
            // Submenus use a fixed minimum; they have no trigger button to match.
            const int kSubmenuFallbackMinWidth = styleManager.Px(160);
            auto windowConstraints = [&]() {
                if (menuManager.currentDepth != -1)
                    return pos.MinWidth(Pixels{ kSubmenuFallbackMinWidth }).MaxHeight(Pixels{ workArea.h });

                const interstice::IntRect* triggerRectPtr = m_menuRects->FindTriggerRect(menuID);
                if (!triggerRectPtr)
                    return pos.MaxHeight(Pixels{ workArea.h }); // no button rect yet - safest is content-width

                const interstice::IntRect& triggerRect = *triggerRectPtr;
                bool sidePlacedLastFrame = false;
                if (const interstice::IntRect* windowRectPtr = m_menuRects->FindWindowRect(menuID))
                {
                    const interstice::IntRect& windowRect = *windowRectPtr;
                    // Side-placed <=> the X ranges of the popup and the trigger button
                    // don't overlap. Below/above keeps them X-aligned.
                    const int32_t triggerLeft  = triggerRect.x;
                    const int32_t triggerRight = triggerRect.x + triggerRect.w;
                    const int32_t windowLeft   = windowRect.x;
                    const int32_t windowRight  = windowRect.x + windowRect.w;
                    sidePlacedLastFrame = (windowRight <= triggerLeft) || (windowLeft >= triggerRight);
                }

                return sidePlacedLastFrame
                    ? pos.MaxHeight(Pixels{ workArea.h })
                    : pos.MinWidth(Pixels{ triggerRect.w }).MaxHeight(Pixels{ workArea.h });
            }();

            level.windowID = menuManager.PopupWindowFor(menuID, idManager);
            menuManager.PrepareLevelWindow(level, guiStateManager.GetScrollManagement());
            if (BeginWindow(level.windowID, menuStyle, std::move(windowConstraints),
                            ElementFlags::IsMenuWindow))
            {
                // Snapshot the popup rect for next frame's side-placement check.
                // Top-level only; submenus are always side-placed.
                if (menuManager.currentDepth == -1)
                {
                    auto windowElementRef = elementStorage.FindElementRefByID(level.windowID);
                    if (windowElementRef.IsValid())
                    {
                        auto& windowRects = m_menuRects->WindowRects();
                        [[maybe_unused]] auto cdRes = elementStorage.AddPostLayout(windowElementRef,
                            [&windowRects, menuID](interstice::PostLayoutContext& ctx)
                            {
                                const interstice::IntRect r = ctx.ElementRect();
                                if (r.w > 0 && r.h > 0) windowRects.insert_or_assign(menuID, r);
                            });
                    }
                }

                menuManager.currentDepth = level.depth;
			    menuManager.maxOpenDepth = std::max(menuManager.maxOpenDepth, menuManager.currentDepth);
                // Reserve the icon column from last frame's content (stable per trigger),
                // then re-accumulate this frame as items declare icons.
                level.reserveIconColumn = menuManager.menuItemsHaveIcons[level.triggerID];
                level.itemsHaveIcons    = false;
                level.filtering         = false;
                level.scrollUpID   = idManager.AllocateDynamicID();
                level.scrollDownID = idManager.AllocateDynamicID();
                return true;
            } else
            {
                EndWindow();   // the popup window's skipped scope
                if (menuManager.currentDepth == -1)
                {
                    menuManager.justClosedTriggerID = level.triggerID;
                    menuManager.CloseAll(guiStateManager);
                } else
                {
                    menuManager.CloseFromDepth(level.depth);
                }
                return false;
            }
        }
        return false;
    }

    namespace
    {
        bool ContainsIgnoringCase(std::string_view text, std::string_view part)
        {
            auto lower = [](char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; };
            return std::search(text.begin(), text.end(), part.begin(), part.end(),
                               [&](char a, char b) { return lower(a) == lower(b); }) != text.end();
        }
    }

    void GUIApplication::MenuFilter()
    {
        if (menuManager.currentDepth < 0 || menuManager.currentDepth >= MenuManager::kMaxDepth)
        {
            ReportMisuse("GUIApplication: MenuFilter outside an open menu (ignored)");
            return;
        }
        auto& level = menuManager.levels[menuManager.currentDepth];
        ID fieldID = ID::Invalid();
        SearchField(idManager.AllocateDynamicID("menu_filter"), level.filter, theme.searchIcon,
                    TextEditBehavior::ContinuousUpdate, interstice::LayoutOverride{}.NoShrink(), &fieldID);
        // The filter takes the keyboard when the menu opens; holding the menu's active
        // slot keeps the first item from taking it.
        if (level.isOpen && level.activeItemID == ID::Invalid())
        {
            level.activeItemID = fieldID;
            guiStateManager.Interaction().SetFocusID(fieldID);
            guiStateManager.TextEditorFocus().Set(fieldID);
        }
        level.filtering = !level.filter.empty();
    }

    void GUIApplication::MenuHeader(std::string_view text)
    {
        if (menuManager.currentDepth >= 0 && menuManager.levels[menuManager.currentDepth].filtering)
            return;
        Text(idManager.AllocateDynamicID("menu_header"), text, theme.menuHeaderStyleRef, interstice::LayoutOverride{}.NoGrow().NoShrink());
    }

    void GUIApplication::MenuSeparator()
    {
        if (menuManager.currentDepth >= 0 && menuManager.levels[menuManager.currentDepth].filtering)
            return;
        const interstice::ID sepID = idManager.AllocateDynamicID();
        interstice::Element sep{};
        sep.id       = sepID;
        sep.styleRef = theme.menuSeparatorStyleRef;
        sep.flags    = interstice::ElementFlags::NoInput;
        sep.state    = guiStateManager.GetCurrentState(sepID);
        auto sRes = elementStorage.Add(std::move(sep),
            interstice::LayoutOverride{}.NoGrow().NoShrink());
        VERIFY(sRes.status);
        VERIFY(windowRegistry.Add(sRes.ref));
    }

    bool GUIApplication::MenuItem(ID id, const GUIContent& content, std::string_view shortcut, std::function<void()> onClick, StyleRef style, LayoutOverride&& constraints)
    {
        return MenuItemImpl(id, content, shortcut, std::move(onClick), style, false, std::move(constraints));
    }

    bool GUIApplication::DestructiveMenuItem(ID id, const GUIContent& content, std::string_view shortcut, std::function<void()> onClick, LayoutOverride&& constraints)
    {
        return MenuItemImpl(id, content, shortcut, std::move(onClick), theme.menuItemRowStyleRef, true, std::move(constraints));
    }

    bool GUIApplication::MenuItemImpl(ID id, const GUIContent& content, std::string_view shortcut, std::function<void()> onClick,
                                      StyleRef style, bool destructive, LayoutOverride&& constraints)
    {
        const IconID           icon = content.icon;
        const std::string_view text = content.text;
        ShowContentTooltip(id, content);
        if (menuManager.currentDepth < 0 || menuManager.currentDepth >= MenuManager::kMaxDepth)
        {
            ReportMisuse("GUIApplication: MenuItem outside an open menu (ignored)");
            return false;
        }
        auto& level = menuManager.levels[menuManager.currentDepth];
        if (level.filtering && !ContainsIgnoringCase(text, level.filter))
            return false;
        // A disabled item shows but can't be chosen, by mouse or keyboard.
        const bool disabled = guiStateManager.Construction().IsDisabled();
        if (disabled) onClick = nullptr;

        // Reserve the left column when this item has an icon or is checkable
        // (selected items show a checkmark there).
        const bool selected = (guiStateManager.GetCurrentState(id) & interstice::ElementState::Selected) != interstice::ElementState::None;
        if (icon != IconID::Invalid || selected)
            level.itemsHaveIcons = true;

        MenuItemRow(id,
            MakeMenuItemHandler(idManager, guiStateManager, elementStorage, menuManager, menuManager.currentDepth, onClick),
            style, level.reserveIconColumn, icon, text, shortcut, theme.menuItemShortcutStyleRef,
            std::move(constraints), destructive);
        if (disabled)
            return false;

        // Auto-select the first item only when focus is meant to move into the menu
        // (context menus, dropdowns, or after Down from a menu-bar trigger). In
        // menu-bar mode focus stays on the trigger, so nothing is pre-selected and a
        // leading submenu trigger won't auto-cascade its child open.
        if (level.isOpen && level.activeItemID == ID::Invalid() && menuManager.focusSubmenuOnOpen)
        {
            level.activeItemID = id;
            guiStateManager.Interaction().SetFocusID(id);
        }

        // Update activeItemID on hover (only when mouse is not hidden for keyboard nav)
        if (!menuManager.mouseHiddenForMenu && guiStateManager.Interaction().GetHoveredID() == id)
        {
            if (level.activeItemID != id)
            {
                level.activeItemID = id;
                guiStateManager.Interaction().SetFocusID(id);
                // Close any open child submenu since we moved to a regular item
                menuManager.CloseFromDepth(menuManager.currentDepth + 1);
                menuManager.suppressedSubmenuTriggerDepth = -1;
            }
        }

        bool activated = guiStateManager.Interaction().HasBeenClicked(id);
        if (activated)
            menuManager.requestCloseAll = true;
        return activated;
    }

    bool GUIApplication::MenuItem(CommandID command)
    {
        const CommandManager::Command* found = commandManager.GetCommand(command);
        if (found == nullptr) return false;
        const CommandManager::Command& c = *found;
        // No shortcut key -> empty trailing column.
        return MenuItem({ c.icon, c.description },
                        c.HasShortcut() ? std::string_view(c.displayString) : std::string_view{},
                        c.callback);
    }

    bool GUIApplication::CommandButton(CommandID command, StyleRef style, LayoutOverride&& constraints)
    {
        const CommandManager::Command* found = commandManager.GetCommand(command);
        if (found == nullptr) return false;
        const CommandManager::Command& c = *found;
        // Icon-only when the command has an icon; fall back to the label otherwise.
        const std::string_view text = (c.icon != IconID::Invalid)
            ? std::string_view{} : std::string_view(c.description);
        bool clicked = Button({ c.icon, text }, c.callback, style, std::move(constraints));
        if (c.HasShortcut())
            SetTooltip(c.description + " (" + c.displayString + ")");
        else
            SetTooltip(std::string_view(c.description));
        return clicked;
    }

    // Called for every BeginMenu / BeginContextMenu, open or not.
    void GUIApplication::EndMenu()
    {
        if (m_menuOpen.empty())
        {
            ReportMisuse("GUIApplication: EndMenu without BeginMenu (ignored)");
            return;
        }
        const bool open = m_menuOpen.back();
        m_menuOpen.pop_back();
        if (open)
            CloseMenu();
    }

    void GUIApplication::CloseMenu()
    {
        if (menuManager.currentDepth < 0)
        {
            ReportMisuse("GUIApplication: EndMenu without an open BeginMenu (ignored)");
            return;
        }
        auto& level = menuManager.levels[menuManager.currentDepth];

        // Remember whether this menu used icons (keyed by trigger) so the icon
        // column can be reserved on the very first frame of the next open.
        menuManager.menuItemsHaveIcons[level.triggerID] = level.itemsHaveIcons;

        // Add scroll buttons after all menu items so they render on top.
        // (IDs were pre-allocated in BeginMenu for stable dynamic ID ordering)
        const int32_t menuWindowWidth = windowManager->GetWindowRect(level.windowID).w;
        if (level.canScrollUp)
        {
            ShowScrollUpButton(*this, level.scrollUpID, level.windowID, theme.menuScrollButtonStyleRef, menuWindowWidth);
        }
        if (level.canScrollDown)
        {
            ShowScrollDownButton(*this, level.scrollDownID, level.windowID, theme.menuScrollButtonStyleRef, menuWindowWidth);
        }

        EndWindow();

        menuManager.currentDepth--;
        if (menuManager.requestCloseAll)
        {
            // Back outside all menus: finalize. CloseAll is the single point of responsibility
            // for restoring focus, and does nothing once the menu is marked closed.
            if (menuManager.currentDepth == -1)
                menuManager.CloseAll(guiStateManager);
            else
                level.isOpen = false;
        }

        if (menuManager.currentDepth <= -1)
            menuManager.requestCloseAll = false;
    }
}