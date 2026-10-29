#include <gui_controls/gui_application.h>

#include <algorithm>
#include <string>

namespace tachyon
{

    // ---------------------------------------------------------------------------
    // TabBar
    // ---------------------------------------------------------------------------

    namespace
    {
        // Which tabs fit in the bar, from last frame's sizes: the first ones, in order, and the
        // selected one in place of the last ones that fit when it isn't among them. Room is left
        // for the overflow button. Empty: every tab fits.
        std::vector<bool> ShownTabs(const TabBarMemory& fit, int selected, int32_t gap)
        {
            const int count = int(fit.widths.size());
            if (count == 0 || fit.barWidth <= 0)
                return {};
            int64_t total = int64_t(gap) * (count - 1);
            for (const int32_t w : fit.widths)
                total += w;
            if (total <= fit.barWidth)
                return {};

            const int64_t room = int64_t(fit.barWidth) - fit.overflowWidth - gap;
            std::vector<int> taken;
            int64_t used = 0;
            auto widthWith = [&](int index) { return used + (taken.empty() ? 0 : gap) + fit.widths[size_t(index)]; };
            for (int i = 0; i < count && widthWith(i) <= room; ++i)
            {
                used = widthWith(i);
                taken.push_back(i);
            }
            if (selected >= 0 && selected < count && std::find(taken.begin(), taken.end(), selected) == taken.end())
            {
                while (!taken.empty() && widthWith(selected) > room)
                {
                    taken.pop_back();
                    used = 0;
                    for (size_t k = 0; k < taken.size(); ++k)
                        used += (k == 0 ? 0 : gap) + fit.widths[size_t(taken[k])];
                }
                taken.push_back(selected);   // after the others, as its index is higher
            }
            if (taken.empty())
                taken.push_back(0);

            std::vector<bool> shown(size_t(count), false);
            for (const int i : taken)
                shown[size_t(i)] = true;
            return shown;
        }

        // The bar's layout: the caller's, or the one the bar keeps by its ID.
        TabBarLayout& LayoutOf(GUIApplication& app, ID barID, TabBarLayout* layout)
        {
            return layout ? *layout : app.guiStateManager.GetElementDataStore().GetOrCreate<TabBarMemory>(barID).layout;
        }

        // The two layouts as menu items, the current one checked; picking one sets it.
        void TabLayoutMenuItems(GUIApplication& app, ID barID, TabBarLayout* layout)
        {
            const TabBarLayout current = LayoutOf(app, barID, layout);
            auto item = [&](TabBarLayout to, std::string_view text)
            {
                app.MenuItem({ current == to ? app.theme.menuCheckIcon : IconID::Invalid, text },
                             [&app, barID, layout, to] { LayoutOf(app, barID, layout) = to; });
            };
            item(TabBarLayout::OneRow, "Collapse Rows");
            item(TabBarLayout::Rows,   "Multiple Rows");
        }
    }

    bool GUIApplication::BeginTabBar(ID id, int* selectedIndex,
                                  std::function<void(int, int)> onReorder,
                                  std::function<void(int)> onClose,
                                  std::function<void(int, tachyon::int2)> onDragOut,
                                  std::function<void(int, tachyon::int2)> dragVisualizer,
                                  StyleRef style, LayoutOverride&& constraints, TabBarLayout* layout)
    {
        m_tabBarState = {};
        m_tabBarState.barID         = id;
        m_tabBarState.selectedIndex = selectedIndex;
        m_tabBarState.layout        = layout;
        m_tabBarState.onClose       = std::move(onClose);
        m_tabBarState.onDragOut     = onDragOut; // keep a copy for bookkeeping

        if (style == StyleRef{})
            style = theme.tabBarStyleRef;

        // On several rows every tab is shown and the bar wraps; on one row the bar doesn't, and
        // the tabs that fit, from last frame's sizes, are shown: the rest are listed by an
        // overflow button.
        const bool rows = LayoutOf(*this, id, layout) == TabBarLayout::Rows;
        style = styleManager.WithFlexWrap(style, rows ? FlexWrap::Wrap : FlexWrap::NoWrap);
        if (!rows)
        {
            TabBarMemory fit = guiStateManager.GetElementDataStore().GetOrCreate<TabBarMemory>(id);
            if (fit.overflowWidth <= 0)
                fit.overflowWidth = styleManager.Px(48);   // until it has been laid out once
            const LayoutStyle* barLayout = styleManager.GetLayoutStyle(style.layoutStyleIndex);
            const int32_t gap = barLayout ? int32_t(barLayout->layoutData.flex.gap) : 0;
            m_tabBarState.shown = ShownTabs(fit, selectedIndex ? *selectedIndex : -1, gap);
        }

        // The reorderable group counts only the tabs it holds: map its indices to the tabs'.
        // onDragOut/dragVisualizer go through the group so handlers capture them;
        // m_tabBarState is cleared in EndTabBar, before input dispatch runs.
        std::vector<int> order;
        for (size_t i = 0; i < m_tabBarState.shown.size(); ++i)
            if (m_tabBarState.shown[i])
                order.push_back(int(i));
        if (!order.empty())
        {
            auto tabIndex = [order](int i) { return i >= 0 && i < int(order.size()) ? order[size_t(i)] : i; };
            if (onReorder)
                onReorder = [inner = std::move(onReorder), tabIndex](int from, int to) { inner(tabIndex(from), tabIndex(to)); };
            if (onDragOut)
                onDragOut = [inner = std::move(onDragOut), tabIndex](int i, int2 at) { inner(tabIndex(i), at); };
            if (dragVisualizer)
                dragVisualizer = [inner = std::move(dragVisualizer), tabIndex](int i, int2 at) { inner(tabIndex(i), at); };
        }

        const bool open = BeginReorderableGroup(id, ReorderDirection::Horizontal, /*isWrapped*/ rows,
                                                std::move(onReorder), style, std::move(constraints),
                                                std::move(onDragOut), std::move(dragVisualizer));
        if (const ElementRef barRef = elementStorage.FindElementRefByID(id); barRef.IsValid())
            [[maybe_unused]] auto added = elementStorage.AddPostLayout(barRef, [this, id](PostLayoutContext& ctx)
                { guiStateManager.GetElementDataStore().GetOrCreate<TabBarMemory>(id).barWidth = ctx.ElementInnerRect().w; });
        return open;
    }

    void GUIApplication::EndTabBar()
    {
        const ID barID = m_tabBarState.barID;
        if (barID != ID::Invalid())
        {
            auto& store = guiStateManager.GetElementDataStore();
            store.GetOrCreate<TabBarMemory>(barID).tabCount = m_tabBarState.nextTabIndex;
            store.GetOrCreate<TabBarMemory>(barID).widths.resize(size_t(m_tabBarState.nextTabIndex), 0);
        }

        // "+N" after the tabs that fit: a menu of the rest; picking one selects it, and then
        // it's shown in the bar.
        if (barID != ID::Invalid() && !m_tabBarState.hidden.empty())
        {
            [[maybe_unused]] const bool pushed = idManager.PushParent(barID);
            const ID overflowID = idManager.AllocateDynamicID("tab_overflow");
            if (pushed) (void)idManager.PopParent();
            const std::string label = "+" + std::to_string(m_tabBarState.hidden.size());
            int* const selectedIndex = m_tabBarState.selectedIndex;
            if (BeginMenu(overflowID, label, theme.tabOverflowStyleRef, theme.menuStyleRef, theme.menuItemRowStyleRef,
                          LayoutOverride{}.NoGrow().NoShrink()))
            {
                for (const TabBarState::HiddenTab& hidden : m_tabBarState.hidden)
                    MenuItem({ hidden.icon, hidden.text }, [selectedIndex, index = hidden.index] { if (selectedIndex) *selectedIndex = index; });
            }
            EndMenu();
            // Right-click it -> the layouts, as on the strip.
            if (BeginContextMenu(overflowID))
                TabLayoutMenuItems(*this, barID, m_tabBarState.layout);
            EndContextMenu();
            if (const ElementRef ref = elementStorage.FindElementRefByID(overflowID); ref.IsValid())
                [[maybe_unused]] auto added = elementStorage.AddPostLayout(ref, [this, barID](PostLayoutContext& ctx)
                    { guiStateManager.GetElementDataStore().GetOrCreate<TabBarMemory>(barID).overflowWidth = ctx.ElementRect().w; });
        }

        EndReorderableGroup();

        // Right-click the strip -> the layouts.
        if (barID != ID::Invalid())
        {
            if (BeginContextMenu(barID))
                TabLayoutMenuItems(*this, barID, m_tabBarState.layout);
            EndContextMenu();
        }
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
        TabBarLayout* const layout = m_tabBarState.layout;
        bool        isSelected    = selectedIndex && *selectedIndex == tabIndex;

        auto capturedIndex = tabIndex;
        const ID barID = m_tabBarState.barID;
        [[maybe_unused]] const bool pushed = idManager.PushParent(id);
        tachyon::ID bodyID  = idManager.AllocateDynamicID("tab_body");
        tachyon::ID contentID = idManager.AllocateDynamicID("tab_content");
        if (pushed) (void)idManager.PopParent();

        // A tab that doesn't fit goes to the overflow button's list. Its width, for deciding
        // what fits, is its last one, or measured from its text until it has been shown.
        auto& fit = guiStateManager.GetElementDataStore().GetOrCreate<TabBarMemory>(barID);
        if (fit.widths.size() <= size_t(tabIndex))
            fit.widths.resize(size_t(tabIndex) + 1, 0);
        const auto& shown = m_tabBarState.shown;
        if (!shown.empty() && (size_t(tabIndex) >= shown.size() || !shown[size_t(tabIndex)]))
        {
            if (fit.widths[size_t(tabIndex)] <= 0)
            {
                const TextID measured = textSystem->RegisterText(contentID, icon, text, theme.tabContentStyleRef, ElementState::None);
                int32_t width = textSystem->GetTextMeasurement(measured).maxWidth;
                if (const LayoutStyle* tabLayout = styleManager.GetLayoutStyle(style.layoutStyleIndex))
                    width += int32_t(tabLayout->padding.Horizontal() + tabLayout->borderWidths.Horizontal());
                fit.widths[size_t(tabIndex)] = width;
            }
            m_tabBarState.hidden.push_back({ tabIndex, icon, std::string(text) });
            return false;
        }

        if (isSelected) guiStateManager.Construction().PushSelected();

        BeginReorderableItem(id, std::move(constraints));

        // Tab body: container only; icon+text and close button live as children. Its ID is
        // the tab's own, so keyboard focus stays on the tab when tabs move.

        // The keyboard reaches the bar at its selected tab (the first without a selection);
        // arrows then move along the tabs.
        auto& keyboard = guiStateManager.GetElementDataStore().GetOrCreate<TabBarMemory>(barID);
        if (isSelected || (!selectedIndex && tabIndex == 0))
            RegisterTabStop(bodyID);
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
                .eventHandler = [this, id, barID, capturedIndex, selectedIndex, onClose](ElementEvent&& event) -> bool
                {
                    if (event.type == tachyon::EventType::MouseDown && event.state.LeftPressed())
                    {
                        // A click focuses the tab, so the arrow keys go on from it.
                        guiStateManager.Interaction().SetFocusID(event.id);
                        guiStateManager.TextEditorFocus().Clear();
                        if (selectedIndex)
                            *selectedIndex = capturedIndex;
                        guiStateManager.Interaction().SetClicked(id);
					    // Not consumed, so MouseUp still arrives for drag-out detection.
                    }
                    if (event.type == tachyon::EventType::KeyDown &&
                        (guiStateManager.GetCurrentState(event.id) & ElementState::Focused) != ElementState::None)
                    {
                        // Ctrl+W or Ctrl+F4 closes the focused tab, as "Close Panel" does.
                        if (onClose && (event.state.modifiers & KeyModifier::Control) != KeyModifier::None &&
                            (event.state.lastKey == KeyCode::KeyW || event.state.lastKey == KeyCode::F4))
                        {
                            onClose(capturedIndex);
                            return true;
                        }
                        auto& keys = guiStateManager.GetElementDataStore().GetOrCreate<TabBarMemory>(barID);
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
        if (const ElementRef itemRef = elementStorage.FindElementRefByID(id); itemRef.IsValid())
            [[maybe_unused]] auto added = elementStorage.AddPostLayout(itemRef, [this, barID, tabIndex](PostLayoutContext& ctx)
            {
                auto& widths = guiStateManager.GetElementDataStore().GetOrCreate<TabBarMemory>(barID).widths;
                if (widths.size() > size_t(tabIndex))
                    widths[size_t(tabIndex)] = ctx.ElementRect().w;
            });

        // Right-click a tab -> "Close Panel" (when tabs close) and the bar's layouts.
        if (BeginContextMenu(bodyID))
        {
            if (onClose != nullptr)
            {
                MenuItem("Close Panel", [onClose, capturedIndex]() { onClose(capturedIndex); });
                MenuSeparator();
            }
            TabLayoutMenuItems(*this, barID, layout);
        }
        EndContextMenu();

        if (isSelected) guiStateManager.Construction().PopSelected();

        return guiStateManager.Interaction().HasBeenClicked(id);
    }

}