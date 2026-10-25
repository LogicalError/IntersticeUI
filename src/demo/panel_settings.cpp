#include "application.h"

// Demo Settings pages and their registration with `demo.settings`. The panel
// chrome lives in gui_controls/controls_settings.cpp.

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

namespace interstice
{
    namespace
    {
        // Case-insensitive lexicographic less, for the Shortcuts-table column
        // sort comparators (ASCII labels / shortcut strings).
        bool CaseInsensitiveLess(std::string_view a, std::string_view b)
        {
            const size_t n = std::min(a.size(), b.size());
            for (size_t i = 0; i < n; i++)
            {
                char ca = a[i]; if (ca >= 'A' && ca <= 'Z') ca = char(ca + 32);
                char cb = b[i]; if (cb >= 'A' && cb <= 'Z') cb = char(cb + 32);
                if (ca != cb) return ca < cb;
            }
            return a.size() < b.size();
        }
    }

    // ---------------------------------------------------------------------------
    // Page registration
    // ---------------------------------------------------------------------------
    // Call once after icons have been registered (so the IconIDs captured below
    // are valid). The first registered page becomes the initial selection.
    void Application::InitSettingsPages()
    {
        demo.settings.AddPage(idManager.CreateStaticID(), "Shortcuts",  [this]() { RenderSettingsShortcuts(); });
        demo.settings.AddPage(idManager.CreateStaticID(), "Appearance", [this]() { RenderSettingsAppearance(); });
        demo.settings.AddPage(idManager.CreateStaticID(), "General",    [this]() { RenderSettingsGeneral(); });
        demo.settings.AddPage(idManager.CreateStaticID(), "Advanced",   [this]() { RenderSettingsAdvanced(); });
        demo.settings.AddPage(idManager.CreateStaticID(), "About",      [this]() { RenderSettingsAbout(); });
        demo.settings.treeWidth = 120; // narrower than the default page list
    }

    // ---------------------------------------------------------------------------
    // Page renderers
    // ---------------------------------------------------------------------------

    void Application::RenderSettingsAppearance()
    {
        // Labels have break-after: avoid, so each stays on one row with its field.
        Text(idManager.AllocateDynamicID("settingsTitle"), "Appearance", settingsTitleStyleRef);

        BeginLabelGroup(140);

        // Theme picker. Uses BeginMenu as a stand-in dropdown (#63): the menu button
        // shows the active theme name; opening it lists the available palettes.
        // RequestReloadTheme is a no-op when the params haven't changed, so there's
        // no ping-pong when the user reselects the current theme.
        Label("Theme");
        static const char* const themeNames[] = { "Dark", "Light" };
        static const char* const themePaths[] = { "themes/dark.css", "themes/light.css" };
        const char* currentName = themeNames[demo.settingsThemeIndex];
        if (BeginMenu(idManager.AllocateDynamicID("settingsThemeMenu"), currentName))
        {
            for (int i = 0; i < 2; i++)
            {
                int idx = i;
                bool isCurrent = (idx == demo.settingsThemeIndex);
                if (isCurrent) BeginSelected();
                MenuItem(themeNames[i], [this, idx]() {
                    demo.settingsThemeIndex = idx;
                    RequestReloadTheme(themePaths[idx], demo.settingsFontScalePct / 100.0f);
                });
                if (isCurrent) EndSelected();
            }
            EndMenu();
        }

        // Font size (percent), snapped to steps of 10 in [100, 300] before
        // SliderField so knob and textbox show the quantized value. SliderField's
        // return is false during a drag (it writes after OnGUI), so the reload is
        // requested unconditionally; it no-ops when the scale is unchanged.
        Label("Font size (%)");
        const float kFontStep = 10.0f;
        demo.settingsFontScalePct = std::clamp(
            std::round(demo.settingsFontScalePct / kFontStep) * kFontStep, 100.0f, 300.0f);
        SliderField(demo.settingsFontScalePct, 100.0f, 300.0f, /*label*/ "");
        RequestReloadTheme(GetThemePath(), demo.settingsFontScalePct / 100.0f);

        EndLabelGroup();
    }

    void Application::RenderSettingsGeneral()
    {
        Text(idManager.AllocateDynamicID("settingsTitle"), "General", settingsTitleStyleRef);
        BeginLabelGroup(140);

        Label("User name");
        Field(demo.settingsUserName, TextEditBehavior::None);

        Label("Autosave");
        Switch(demo.settingsAutosave, demo.settingsAutosave ? "On" : "Off");

        EndLabelGroup();
    }

    void Application::RenderSettingsAdvanced()
    {
        Text(idManager.AllocateDynamicID("settingsTitle"), "Advanced", settingsTitleStyleRef);
        BeginLabelGroup(140);

        Label("Max undo levels");
        Field(demo.settingsMaxUndo, TextEditBehavior::None);

        EndLabelGroup();
    }

    void Application::RenderSettingsShortcuts()
    {
        // Layout: category tree | sizer | shortcuts table.
        Text(idManager.AllocateDynamicID("settingsTitle"), "Shortcuts", settingsTitleStyleRef);

        const auto& entries      = commandManager.GetCommands();
        const uint32_t modeCount = commandManager.GetEditModeCount();

        // The dynamic-ID counter makes repeated calls with one hint unique and
        // stable frame to frame.
        const interstice::ID allCatID = idManager.AllocateDynamicID("shortcutCat_All");
        interstice::ID catIDs[32];
        const uint32_t cappedCount = modeCount <= 32 ? modeCount : 32;
        for (uint32_t m = 0; m < cappedCount; m++)
            catIDs[m] = idManager.AllocateDynamicID("shortcutCat");

        auto& selection = demo.settingsShortcutsCatSelection;

        // Own row so a child's `break-after: avoid` can't mark the outer column
        // keep-with-next, which disables its flex-grow and collapses the split.
        BeginHorizontal(idManager.AllocateDynamicID("shortcutSearchRow"),
                        theme.searchRowStyleRef,
                        interstice::LayoutOverride::Auto().NoShrink());
        if (SearchField(demo.settingsShortcutsSearch, demo.findIcon,
                        TextEditBehavior::ContinuousUpdate,
                        interstice::LayoutOverride{}.FlexGrow(1)))
        {
            demo.settingsShortcutsSearchLower.clear();
            demo.settingsShortcutsSearchLower.reserve(demo.settingsShortcutsSearch.size());
            for (char c : demo.settingsShortcutsSearch)
                demo.settingsShortcutsSearchLower += (c >= 'A' && c <= 'Z') ? char(c + 32) : c;
        }
        EndHorizontal();

        const std::string& query = demo.settingsShortcutsSearchLower;
        auto matchesSearch = [&](std::string_view desc) -> bool
        {
            if (query.empty()) return true;
            if (query.size() > desc.size()) return false;
            for (size_t i = 0; i + query.size() <= desc.size(); i++)
            {
                bool ok = true;
                for (size_t j = 0; j < query.size(); j++)
                {
                    char a = desc[i + j]; if (a >= 'A' && a <= 'Z') a = char(a + 32);
                    if (a != query[j]) { ok = false; break; }
                }
                if (ok) return true;
            }
            return false;
        };

        // flexFillRow (flex-basis:0) bounds the split to the page height so the
        // table scrolls (the outer split in controls_settings.cpp is bounded too).
        BeginHorizontal(idManager.AllocateDynamicID("shortcutSplit"),
                        theme.flexFillRowStyleRef,
                        interstice::LayoutOverride::Auto().FlexGrow(1));

        // Row IDs are the static category IDs, so the sync below can map
        // sub-range row IDs to and from `selection`.
        auto& catRows       = demo.settingsShortcutsCatRows;
        auto& catVisibleIdx = demo.settingsShortcutsCatVisibleIdx;
        auto& catTableState = demo.settingsShortcutsCatTableState;
        auto& catInternal   = demo.settingsShortcutsCatInternalMulti;
        catTableState.multiSelect = &catInternal;

        catRows.clear();
        catRows.reserve(1 + cappedCount);
        {
            TreeViewRow r{};
            r.id   = allCatID;
            r.name = "All";
            r.icon = interstice::IconID::Invalid;
            catRows.push_back(std::move(r));
        }
        for (uint32_t m = 0; m < cappedCount; m++)
        {
            TreeViewRow r{};
            r.id   = catIDs[m];
            r.name = commandManager.GetEditModeName(interstice::EditMode(m));
            r.icon = interstice::IconID::Invalid;
            catRows.push_back(std::move(r));
        }
        RebuildTreeViewIndex(catRows);

        // Reverse sync (catInternal -> selection) must run before the forward
        // sync: clicks only mutate catInternal, and the forward sync would
        // clobber them.
        selection.selectedIDs.clear();
        for (const interstice::ID& subRangeID : catInternal.selectedIDs)
        {
            if (subRangeID.GetMode() != interstice::IDMode::SubRange) continue;
            const int visIdx = static_cast<int>(subRangeID.value.subRangeParts.index);
            if (visIdx < 0 || visIdx >= int(catVisibleIdx.size())) continue;
            const int flatIdx = catVisibleIdx[visIdx];
            if (flatIdx < 0 || flatIdx >= int(catRows.size())) continue;
            selection.selectedIDs.insert(catRows[flatIdx].id);
        }
        // An empty selection would list nothing; fall back to "All".
        if (selection.selectedIDs.empty())
            selection.selectedIDs.insert(allCatID);

        // Forward sync (selection -> catInternal). Row IDs exist once the table
        // has run once.
        if (catTableState.RowIDInRange(0) != interstice::ID::Invalid())
        {
            catInternal.selectedIDs.clear();
            for (size_t i = 0; i < catRows.size(); ++i)
            {
                if (selection.selectedIDs.count(catRows[i].id))
                {
                    catInternal.selectedIDs.insert(catTableState.RowIDInRange(static_cast<int>(i)));
                }
            }
        }

        const bool allSelected = selection.selectedIDs.count(allCatID) > 0;

        VirtualTreeView(idManager.AllocateDynamicID("shortcutCatTree"),
                        catRows, catVisibleIdx, catTableState,
                        shortcutCategoryStyleRef,
                        interstice::LayoutOverride::Auto().FixedWidth(styleManager.Px(demo.settingsShortcutsCategoryWidth)).NoGrow(),
                        "Category");

        {
            interstice::ID sizerID = idManager.AllocateDynamicID("shortcutSizer");
            interstice::Element sizer{};
            sizer.id       = sizerID;
            sizer.styleRef = theme.dockSizerHorzStyleRef;
            sizer.state    = guiStateManager.GetCurrentState(sizerID);
            sizer.eventHandler = interstice::MakeHorizontalSizerHandler(
                guiStateManager, demo.settingsShortcutsCategoryWidth, 100, 400, nullptr, nullptr, false, styleManager.GetUIScale());
            auto sRes = elementStorage.Add(std::move(sizer), interstice::LayoutOverride::FixedWidth(styleManager.Px(4)));
            VERIFY(sRes.status);
            VERIFY(windowRegistry.Add(sRes.ref));
        }

        // flex-fill-row (flex-basis:0) bounds the table to the pane height so its
        // body scrolls instead of overflowing.
        BeginHorizontal(idManager.AllocateDynamicID("shortcutGridCol"),
                        theme.flexFillRowStyleRef,
                        interstice::LayoutOverride::Auto().FlexGrow(1));

        // The Context column only shows when more than one mode is selected.
        uint32_t selectedModes = allSelected ? modeCount : 0;
        if (!allSelected)
            for (uint32_t m = 0; m < cappedCount; m++)
                if (selection.selectedIDs.count(catIDs[m])) selectedModes++;
        const bool showCategoryCol = selectedModes > 1;

        // The filtered rows are rebuilt only when the fingerprint (selected-mode
        // mask + query) changes; the table caches its sort order too.
        uint64_t filterMask = 0;
        if (allSelected) filterMask = ~uint64_t(0);
        else for (uint32_t m = 0; m < cappedCount; m++)
            if (selection.selectedIDs.count(catIDs[m])) filterMask |= (uint64_t(1) << m);

        // visRows and the columns live on demo state: they're read during input
        // dispatch, after this function returns.
        auto& visRows = demo.settingsShortcutsVisRows;
        if (filterMask != demo.settingsShortcutsFilterMask
            || query != demo.settingsShortcutsFilterQuery)
        {
            demo.settingsShortcutsFilterMask  = filterMask;
            demo.settingsShortcutsFilterQuery = query;
            visRows.clear();
            for (uint32_t mode = 0; mode < cappedCount; mode++)
            {
                if (!allSelected && selection.selectedIDs.count(catIDs[mode]) == 0)
                    continue;
                for (uint32_t i : commandManager.GetCommandsForMode(interstice::EditMode(mode)))
                {
                    const auto& e = entries[i];
                    std::string_view desc = e.description.empty()
                        ? std::string_view("(no description)")
                        : std::string_view(e.description);
                    if (!matchesSearch(desc)) continue;
                    visRows.push_back({ mode, i });
                }
            }
            // Filtered set changed -> the table's cached sort permutation is stale.
            demo.shortcutsTableState.InvalidateRowOrder();
        }

        static const std::string rebindPrompt = "Press a key... (Esc to cancel)";

        // Column template, built once. Comparators take visRows indices; the
        // lambdas capture only `this` and read visRows fresh each frame.
        auto& cols = demo.settingsShortcutsColumns;
        if (cols.empty())
        {
            cols.resize(4);

            // Command - optional per-command icon shown inline before the name.
            cols[0].column = { .header = "Command", .initialWidth = 180, .minWidth = 80, .maxWidth = 600 };
            cols[0].sortAscending = [this](int a, int b) {
                const auto& vr   = demo.settingsShortcutsVisRows;
                const auto& cmds = commandManager.GetCommands();
                auto desc = [&](int r) -> std::string_view {
                    const auto& e = cmds[vr[r].entryIdx];
                    return e.description.empty() ? std::string_view("(no description)")
                                                 : std::string_view(e.description);
                };
                return CaseInsensitiveLess(desc(a), desc(b));
            };
            cols[0].generateCell = [this](int rowIdx) {
                auto& vr = demo.settingsShortcutsVisRows;
                if (rowIdx < 0 || rowIdx >= int(vr.size())) return;
                const auto& e = commandManager.GetCommands()[vr[rowIdx].entryIdx];
                // Copy the label into the cell lambda - Cell may defer it for
                // record/replay. Commands without an icon use the indented
                // style so their labels line up with the icon'd ones.
                std::string label = e.description.empty() ? "(no description)" : e.description;
                const interstice::IconID icon = e.icon;
                const interstice::StyleRef labelStyle = (icon != interstice::IconID::Invalid)
                    ? theme.iconLabelStyleRef : theme.iconLabelNoIconStyleRef;
                Cell([this, icon, labelStyle, label = std::move(label)]() {
                    const interstice::ID id = idManager.AllocateDynamicID();
                    interstice::Element el{};
                    el.id       = id;
                    el.flags    = interstice::ElementFlags::NoInput;
                    el.state    = guiStateManager.GetCurrentState(id);
                    el.styleRef = labelStyle;
                    el.textID   = textSystem->RegisterText(id, icon, label, el.styleRef, el.state);
                    auto res = elementStorage.Add(std::move(el), interstice::LayoutOverride::Auto());
                    VERIFY(res.status);
                    VERIFY(windowRegistry.Add(res.ref));
                });
            };

            // Context - the edit mode the command belongs to.
            cols[1].column = { .header = "Context", .initialWidth = 110, .minWidth = 60, .maxWidth = 400 };
            cols[1].sortAscending = [this](int a, int b) {
                const auto& vr = demo.settingsShortcutsVisRows;
                return CaseInsensitiveLess(
                    commandManager.GetEditModeName(interstice::EditMode(vr[a].mode)),
                    commandManager.GetEditModeName(interstice::EditMode(vr[b].mode)));
            };
            cols[1].generateCell = [this](int rowIdx) {
                auto& vr = demo.settingsShortcutsVisRows;
                if (rowIdx < 0 || rowIdx >= int(vr.size())) return;
                Cell(commandManager.GetEditModeName(interstice::EditMode(vr[rowIdx].mode)));
            };

            // Shortcut - the bound key combo (a flat rebind button).
            cols[2].column = { .header = "Shortcut", .initialWidth = 140, .minWidth = 80, .maxWidth = 400 };
            cols[2].sortAscending = [this](int a, int b) {
                const auto& vr   = demo.settingsShortcutsVisRows;
                const auto& cmds = commandManager.GetCommands();
                return CaseInsensitiveLess(cmds[vr[a].entryIdx].displayString,
                                           cmds[vr[b].entryIdx].displayString);
            };
            cols[2].generateCell = [this](int rowIdx) {
                auto& vr = demo.settingsShortcutsVisRows;
                if (rowIdx < 0 || rowIdx >= int(vr.size())) return;
                const uint32_t i = vr[rowIdx].entryIdx;
                const auto& e = commandManager.GetCommands()[i];
                const bool rebinding = (commandManager.RebindIndex() == int32_t(i));
                // Unbound commands show an empty cell (rather than the
                // formatted "(unbound)" string) until a key is bound.
                const bool unset = !rebinding && !e.HasShortcut();
                const std::string label = rebinding ? rebindPrompt
                                        : unset      ? std::string()
                                                     : e.displayString;
                Cell([this, label, i]() {
                    Button(idManager.AllocateDynamicID("shortcutBtn"),
                           label,
                           [this, i]() {
                               if (commandManager.RebindIndex() == int32_t(i))
                                   commandManager.CancelRebind();
                               else
                                   commandManager.BeginRebind(i);
                           },
                           theme.flatButtonStyleRef);
                });
            };

            // Type - how the shortcut fires: an instant action (one callback on
            // press) or a held begin/end action (RegisterHeldCommand).
            cols[3].column = { .header = "Type", .initialWidth = 80, .minWidth = 56, .maxWidth = 200 };
            cols[3].sortAscending = [this](int a, int b) {
                const auto& vr   = demo.settingsShortcutsVisRows;
                const auto& cmds = commandManager.GetCommands();
                return CaseInsensitiveLess(cmds[vr[a].entryIdx].IsHeld() ? "Hold" : "Instant",
                                           cmds[vr[b].entryIdx].IsHeld() ? "Hold" : "Instant");
            };
            cols[3].generateCell = [this](int rowIdx) {
                auto& vr = demo.settingsShortcutsVisRows;
                if (rowIdx < 0 || rowIdx >= int(vr.size())) return;
                Cell(commandManager.GetCommands()[vr[rowIdx].entryIdx].IsHeld() ? "Hold" : "Instant");
            };
        }

        // Active columns, in a persisted vector so the sizer double-click's
        // internal.maxContentWidth pointer outlives the frame.
        auto& active = demo.settingsShortcutsActiveColumns;
        active.clear();
        active.push_back(cols[0]);                       // Command
        if (showCategoryCol) active.push_back(cols[1]);  // Context (multi-category only)
        active.push_back(cols[3]);                       // Type
        active.push_back(cols[2]);                       // Shortcut

        // Toggling the Context column changes the column count and shifts what
        // each index means, so on the transition we drop persisted widths and
        // reset the sort to keep both aligned with the new column layout.
        const size_t colCount = active.size();
        if (demo.shortcutsTableState.columnWidths.size() != colCount)
        {
            demo.shortcutsTableState.columnWidths.clear();
            demo.shortcutsTableState.sortColumn = -1;
            demo.shortcutsTableState.InvalidateRowOrder();
        }

        VirtualTable(idManager.AllocateDynamicID("shortcutTable"),
                     demo.shortcutsTableState,
                     std::span<VirtualTableColumn>(active.data(), colCount),
                     int(visRows.size()),
                     /*rowHeight*/0,
                     shortcutTableStyleRef,   // bound to the pane; columns scroll instead of growing the table
                     interstice::LayoutOverride{}.FlexGrow(1),
                     /*showHeaders*/ true, shortcutCellStyleRef);

        EndHorizontal();  // shortcutGridCol
        EndHorizontal();  // shortcutSplit
    }

    void Application::RenderSettingsAbout()
    {
        Text(idManager.AllocateDynamicID("settingsTitle"), "About", settingsTitleStyleRef);
        Text("IntersticeUI — Vulkan-based GUI framework demo.");
        Text("These are placeholder settings pages.");
    }

}