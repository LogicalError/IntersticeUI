// Saved UI state (#21): the windows, dock trees, tables, toolbars and app values an app
// names, as JSON the host stores.

#include <gui_controls/gui_application.h>
#include <gui_controls/controls_toolbar_dock.h>
#include <gui_core/gui_json.h>

#include <algorithm>
#include <cmath>
#include <optional>

namespace tachyon
{
    namespace
    {
        constexpr int kStateVersion = 1;
        constexpr const char* kSideNames[kToolbarSideCount] = { "Top", "Right", "Bottom", "Left" };

        const char* ModeName(WindowMode mode)
        {
            switch (mode)
            {
                case WindowMode::Maximized:           return "Maximized";
                case WindowMode::Fullscreen:          return "Fullscreen";
                case WindowMode::ExclusiveFullscreen: return "ExclusiveFullscreen";
                default:                              return "Normal";   // a minimized window comes back normal
            }
        }

        WindowMode ModeFromName(std::string_view name)
        {
            if (name == "Maximized")           return WindowMode::Maximized;
            if (name == "Fullscreen")          return WindowMode::Fullscreen;
            if (name == "ExclusiveFullscreen") return WindowMode::ExclusiveFullscreen;
            return WindowMode::Normal;
        }

        // A panel's key: its type when it has one, else its title.
        std::string PanelKey(const Panel& panel) { return panel.panelType.empty() ? panel.title : panel.panelType; }

        DockNode* FirstLeaf(DockNode* node)
        {
            while (node != nullptr && !node->isLeaf)
                node = node->first ? node->first.get() : node->second.get();
            return node;
        }

        template<class Entry, class Target>
        void Upsert(std::vector<Entry>& entries, std::string key, Target target)
        {
            for (Entry& entry : entries)
                if (entry.key == key) { entry.target = target; return; }
            entries.push_back(Entry{ std::move(key), target });
        }
    }

    void GUIApplication::PersistWindow(std::string key, ID windowID)                     { Upsert(m_persistWindows, std::move(key), windowID); }
    void GUIApplication::PersistDocking(std::string key, DockingLayout& layout)          { Upsert(m_persistDocks, std::move(key), &layout); }
    void GUIApplication::PersistToolbars(std::string key, std::span<std::vector<ToolbarItem>, kToolbarSideCount> sides)
                                                                                         { Upsert(m_persistToolbars, std::move(key), sides); }
    void GUIApplication::PersistValue(std::string key, bool& value)                      { Upsert(m_persistValues, std::move(key), PersistedValue::Target(&value)); }
    void GUIApplication::PersistValue(std::string key, int& value)                       { Upsert(m_persistValues, std::move(key), PersistedValue::Target(&value)); }
    void GUIApplication::PersistValue(std::string key, float& value)                     { Upsert(m_persistValues, std::move(key), PersistedValue::Target(&value)); }
    void GUIApplication::PersistValue(std::string key, std::string& value)               { Upsert(m_persistValues, std::move(key), PersistedValue::Target(&value)); }
    void GUIApplication::PersistTable(std::string key, TableState& state, std::span<VirtualTableColumn> columns)
    {
        Upsert(m_persistTables, std::move(key), PersistedTable::Target{ &state, columns });
    }

    std::string GUIApplication::SaveUIState() const
    {
        JsonValue doc = JsonValue::MakeObject();
        doc.Set("version", kStateVersion);

        JsonValue windows = JsonValue::MakeObject();
        for (const auto& [key, windowID] : m_persistWindows)
        {
            if (!windowManager || windowManager->GetWindowHandle(windowID) == nullptr)
                continue;   // not open: its last saved state isn't known here
            const IntRect rect = windowManager->GetWindowRect(windowID);
            JsonValue w = JsonValue::MakeObject();
            w.Set("x", rect.x); w.Set("y", rect.y); w.Set("w", rect.w); w.Set("h", rect.h);
            w.Set("mode", ModeName(windowManager->GetWindowMode(windowID)));
            windows.Set(key, std::move(w));
        }
        doc.Set("windows", std::move(windows));

        JsonValue docking = JsonValue::MakeObject();
        for (const auto& [key, layout] : m_persistDocks)
        {
            if (!layout->root)
                continue;
            const std::string tree = SerializeDockTree(*layout->root, [layout](ID id) {
                const auto it = layout->panels.find(id);
                return it != layout->panels.end() ? PanelKey(it->second) : std::string();
            });
            if (Result<JsonValue> parsed = JsonValue::Parse(tree))
                docking.Set(key, std::move(*parsed));
        }
        doc.Set("docking", std::move(docking));

        JsonValue tables = JsonValue::MakeObject();
        for (const auto& [key, table] : m_persistTables)
        {
            JsonValue columns = JsonValue::MakeArray();
            for (size_t i = 0; i < table.columns.size(); ++i)
            {
                const TableColumn& column = table.columns[i].column;
                const int32_t width = i < table.state->columnWidths.size() ? table.state->columnWidths[i] : column.initialWidth.value;
                JsonValue c = JsonValue::MakeObject();
                c.Set("column", std::string(column.header));
                c.Set("width", width);
                columns.Push(std::move(c));
            }
            tables.Set(key, std::move(columns));
        }
        doc.Set("tables", std::move(tables));

        JsonValue toolbars = JsonValue::MakeObject();
        for (const auto& [key, sides] : m_persistToolbars)
        {
            JsonValue bySide = JsonValue::MakeObject();
            for (int s = 0; s < kToolbarSideCount; ++s)
            {
                JsonValue names = JsonValue::MakeArray();
                for (const ToolbarItem& item : sides[size_t(s)])
                    names.Push(item.debugName);
                bySide.Set(kSideNames[s], std::move(names));
            }
            toolbars.Set(key, std::move(bySide));
        }
        doc.Set("toolbars", std::move(toolbars));

        JsonValue values = JsonValue::MakeObject();
        for (const auto& [key, target] : m_persistValues)
            std::visit([&](auto* v) { values.Set(key, JsonValue(*v)); }, target);
        doc.Set("values", std::move(values));

        return doc.Write();
    }

    Result<void> GUIApplication::RestoreUIState(std::string_view json)
    {
        Result<JsonValue> parsed = JsonValue::Parse(json);
        if (!parsed)
            return parsed.GetError();
        const JsonValue& doc = *parsed;
        if (!doc.IsObject())
            return Error{ ErrorCode::ParseFailed, "UI state: not a JSON object" };
        if (const JsonValue* version = doc.Find("version"); version && version->AsNumber() > kStateVersion)
            return Error{ ErrorCode::Unsupported, "UI state: saved by a newer version" };

        auto section = [&](const char* name, std::string_view key) -> const JsonValue* {
            const JsonValue* s = doc.Find(name);
            return s ? s->Find(key) : nullptr;
        };

        // Windows: applied when each is next declared (OpenWindow).
        for (const auto& [key, windowID] : m_persistWindows)
        {
            const JsonValue* w = section("windows", key);
            if (!w || !w->IsObject())
                continue;
            RestoredWindow restored;
            restored.rect = IntRect{ int32_t(w->Find("x") ? w->Find("x")->AsNumber() : 0), int32_t(w->Find("y") ? w->Find("y")->AsNumber() : 0),
                                     int32_t(w->Find("w") ? w->Find("w")->AsNumber() : 0), int32_t(w->Find("h") ? w->Find("h")->AsNumber() : 0) };
            if (restored.rect.w <= 0 || restored.rect.h <= 0)
                continue;
            restored.mode = ModeFromName(w->Find("mode") ? w->Find("mode")->AsString() : std::string_view{});
            m_restoredWindows[windowID] = restored;
        }

        // Dock trees: panels by key; panels the saved tree doesn't have go to its first leaf.
        for (const auto& [key, layout] : m_persistDocks)
        {
            const JsonValue* tree = section("docking", key);
            if (!tree)
                continue;
            std::unique_ptr<DockNode> root = ParseDockTree(tree->Write(), [layout](std::string_view panelKey) -> std::optional<ID> {
                for (const auto& [id, panel] : layout->panels)
                    if (PanelKey(panel) == panelKey) return id;
                return std::nullopt;
            });
            if (!root)
                continue;   // nothing usable: the app's own layout stays
            std::vector<ID> all;
            for (const auto& [id, panel] : layout->panels) all.push_back(id);
            if (DockNode* leaf = FirstLeaf(root.get()))
                for (const ID id : UnplacedPanels(*root, all))
                    leaf->panelIds.push_back(id);
            layout->root = std::move(root);
        }

        // Tables: columns in their saved order (new ones after), with their saved widths.
        for (const auto& [key, table] : m_persistTables)
        {
            const JsonValue* saved = section("tables", key);
            if (!saved || !saved->IsArray())
                continue;
            const size_t count = table.columns.size();
            std::vector<size_t> order;
            std::vector<int32_t> widths;
            std::vector<bool> taken(count, false);
            for (const JsonValue& c : saved->Items())
            {
                const std::string& header = c.Find("column") ? c.Find("column")->AsString() : std::string();
                for (size_t i = 0; i < count; ++i)
                    if (!taken[i] && table.columns[i].column.header == header)
                    {
                        taken[i] = true;
                        order.push_back(i);
                        const TableColumn& col = table.columns[i].column;
                        const double w = c.Find("width") ? c.Find("width")->AsNumber(col.initialWidth.value) : col.initialWidth.value;
                        widths.push_back(std::clamp(int32_t(std::lround(w)), col.minWidth.value, col.maxWidth.value));
                        break;
                    }
            }
            for (size_t i = 0; i < count; ++i)
                if (!taken[i]) { order.push_back(i); widths.push_back(table.columns[i].column.initialWidth.value); }

            std::vector<VirtualTableColumn> reordered;
            reordered.reserve(count);
            for (const size_t i : order) reordered.push_back(std::move(table.columns[i]));
            std::move(reordered.begin(), reordered.end(), table.columns.begin());
            if (table.state->sortColumn >= 0 && size_t(table.state->sortColumn) < count)
                table.state->sortColumn = int(std::find(order.begin(), order.end(), size_t(table.state->sortColumn)) - order.begin());
            table.state->columnWidths = std::move(widths);
        }

        // Toolbars: each named item on its saved side and place; the rest stay where they are.
        for (const auto& [key, sides] : m_persistToolbars)
        {
            const JsonValue* saved = section("toolbars", key);
            if (!saved || !saved->IsObject())
                continue;
            std::vector<ToolbarItem> pool;
            std::vector<int>         poolSide;
            for (int s = 0; s < kToolbarSideCount; ++s)
                for (ToolbarItem& item : sides[size_t(s)]) { pool.push_back(std::move(item)); poolSide.push_back(s); }
            std::vector<bool> placed(pool.size(), false);
            std::vector<ToolbarItem> rebuilt[kToolbarSideCount];
            for (int s = 0; s < kToolbarSideCount; ++s)
                if (const JsonValue* names = saved->Find(kSideNames[s]))
                    for (const JsonValue& name : names->Items())
                        for (size_t i = 0; i < pool.size(); ++i)
                            if (!placed[i] && pool[i].debugName == name.AsString())
                            {
                                placed[i] = true;
                                rebuilt[s].push_back(std::move(pool[i]));
                                break;
                            }
            for (size_t i = 0; i < pool.size(); ++i)
                if (!placed[i]) rebuilt[poolSide[i]].push_back(std::move(pool[i]));
            for (int s = 0; s < kToolbarSideCount; ++s)
                sides[size_t(s)] = std::move(rebuilt[s]);
        }

        // App values, by their own type; a value of another type is skipped.
        for (const auto& [key, target] : m_persistValues)
        {
            const JsonValue* v = section("values", key);
            if (!v)
                continue;
            std::visit([&](auto* t) {
                using T = std::remove_pointer_t<decltype(t)>;
                if constexpr (std::is_same_v<T, bool>)             { if (v->IsBool())   *t = v->AsBool(); }
                else if constexpr (std::is_same_v<T, std::string>) { if (v->IsString()) *t = v->AsString(); }
                else                                               { if (v->IsNumber()) *t = T(v->AsNumber()); }
            }, target);
        }
        return {};
    }

    void GUIApplication::ApplyRestoredWindow(ID windowID, LayoutOverride& constraints, ElementFlags& flags)
    {
        const auto it = m_restoredWindows.find(windowID);
        if (it == m_restoredWindows.end())
            return;
        RestoredWindow& restored = it->second;
        const bool exists = windowManager->GetWindowHandle(windowID) != nullptr;
        if (!restored.rectDone)
        {
            // Off every monitor (a display unplugged, a position saved wrong): keep the size, let
            // the app place it. Its top, where it's grabbed and closed, must be on a monitor:
            // inside a work area, with some of its width. Checked here, in a frame, when the
            // monitors are known.
            restored.hasPosition = false;
            if (windowManager->Monitors().empty())   // the first frame's are read at its end
                windowManager->RefreshMonitors();
            for (const MonitorInfo& monitor : windowManager->Monitors())
            {
                const IntRect& a = monitor.workArea;
                const int32_t  top = restored.rect.y;
                const int32_t  visibleWidth = std::min(restored.rect.x + restored.rect.w, a.x + a.w)
                                            - std::max(restored.rect.x, a.x);
                if (top >= a.y && top < a.y + a.h - styleManager.Px(32)
                    && visibleWidth >= std::min(restored.rect.w, int32_t(styleManager.Px(100))))
                    restored.hasPosition = true;
            }
            if (exists)
            {
                IntRect rect = restored.rect;
                if (!restored.hasPosition)
                    rect.position = windowManager->GetWindowRect(windowID).position;
                (void)windowManager->SetWindowRect(windowID, rect);
            }
            else
            {
                // The OS window is made from this declaration's constraints.
                constraints.width.SetValue(Pixels{ restored.rect.w });
                constraints.height.SetValue(Pixels{ restored.rect.h });
                if (restored.hasPosition)
                {
                    constraints.Position(restored.rect.x, restored.rect.y);
                    flags = flags & ~ElementFlags::WindowIsCentered;
                }
            }
            restored.rectDone = true;
        }
        if (restored.mode == WindowMode::Normal)
            m_restoredWindows.erase(it);
        else if (exists)
        {
            windowManager->SetWindowMode(windowID, restored.mode);
            m_restoredWindows.erase(it);
        }
    }
}
