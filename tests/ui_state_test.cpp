// Saved UI state (#21): windows, dock trees, tables, toolbars and app values as JSON.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_controls/gui_dock_node.h>
#include <gui_core/gui_json.h>

#include <string>
#include <vector>

namespace
{
    using namespace tachyon;
    using GUITest::HeadlessGUIApp;

    // ---- JSON ----

    TEST(JsonTest, ReadsWhatItWrites)
    {
        JsonValue doc = JsonValue::MakeObject();
        doc.Set("name", "a \"quoted\"\nline");
        doc.Set("count", 3);
        doc.Set("ratio", 1.5);
        doc.Set("on", true);
        doc.Set("none", nullptr);
        JsonValue list = JsonValue::MakeArray();
        list.Push(1); list.Push("two");
        doc.Set("list", std::move(list));

        const Result<JsonValue> back = JsonValue::Parse(doc.Write());
        ASSERT_TRUE(back) << back.GetError().message;
        EXPECT_EQ(back->Find("name")->AsString(), "a \"quoted\"\nline");
        EXPECT_EQ(back->Find("count")->AsNumber(), 3.0);
        EXPECT_EQ(back->Find("ratio")->AsNumber(), 1.5);
        EXPECT_TRUE(back->Find("on")->AsBool());
        EXPECT_TRUE(back->Find("none")->IsNull());
        ASSERT_EQ(back->Find("list")->Items().size(), 2u);
        EXPECT_EQ(back->Find("list")->Items()[1].AsString(), "two");
        EXPECT_EQ(back->Write(), doc.Write()) << "members keep their order";
    }

    TEST(JsonTest, DecodesUnicodeEscapes)
    {
        const Result<JsonValue> v = JsonValue::Parse(R"("caf\u00e9 \ud83d\ude00")");
        ASSERT_TRUE(v);
        EXPECT_EQ(v->AsString(), "caf\xC3\xA9 \xF0\x9F\x98\x80");
    }

    TEST(JsonTest, MalformedTextIsAnError)
    {
        for (const char* text : { "", "{", "[1,]", "{\"a\" 1}", "\"open", "tru", "{} x", "\"\\ud800\"" })
        {
            const Result<JsonValue> v = JsonValue::Parse(text);
            EXPECT_FALSE(v) << text;
            if (!v) { EXPECT_EQ(v.GetError().code, ErrorCode::ParseFailed); }
        }
        EXPECT_FALSE(JsonValue::Parse(std::string(100, '[') + std::string(100, ']'))) << "too deep";
    }

    // ---- UI state ----

    class UIStateTest : public ::testing::Test
    {
    protected:
        HeadlessGUIApp app;
        void SetUp() override { ASSERT_TRUE(app.Initialize()); }
        void TearDown() override { app.Cleanup(); }
    };

    TEST_F(UIStateTest, AppValuesComeBack)
    {
        bool on = true; int count = 7; float zoom = 1.25f; std::string theme = "dark";
        app.PersistValue("on", on);
        app.PersistValue("count", count);
        app.PersistValue("zoom", zoom);
        app.PersistValue("theme", theme);
        const std::string saved = app.SaveUIState();

        on = false; count = 0; zoom = 1.0f; theme = "light";
        ASSERT_TRUE(app.RestoreUIState(saved));
        EXPECT_TRUE(on);
        EXPECT_EQ(count, 7);
        EXPECT_FLOAT_EQ(zoom, 1.25f);
        EXPECT_EQ(theme, "dark");

        // A value saved as another type is skipped.
        ASSERT_TRUE(app.RestoreUIState(R"({"values":{"count":"seven"}})"));
        EXPECT_EQ(count, 7);
    }

    TEST_F(UIStateTest, TableColumnOrderAndWidthsComeBack)
    {
        auto makeColumns = [] {
            std::vector<VirtualTableColumn> columns(3);
            columns[0].column.header = "Name";
            columns[1].column.header = "Size";
            columns[2].column.header = "Date";
            return columns;
        };
        TableState state;
        std::vector<VirtualTableColumn> columns = makeColumns();
        std::swap(columns[0], columns[2]);                    // the user moved Date first
        state.columnWidths = { 90, 60, 200 };                 // Date, Size, Name
        app.PersistTable("files", state, columns);
        const std::string saved = app.SaveUIState();

        TableState fresh;
        std::vector<VirtualTableColumn> freshColumns = makeColumns();
        freshColumns.push_back({});
        freshColumns.back().column.header = "Kind";           // added since the save
        app.PersistTable("files", fresh, freshColumns);
        ASSERT_TRUE(app.RestoreUIState(saved));
        ASSERT_EQ(freshColumns.size(), 4u);
        EXPECT_EQ(freshColumns[0].column.header, "Date");
        EXPECT_EQ(freshColumns[1].column.header, "Size");
        EXPECT_EQ(freshColumns[2].column.header, "Name");
        EXPECT_EQ(freshColumns[3].column.header, "Kind") << "a new column comes last";
        EXPECT_EQ(fresh.columnWidths, (std::vector<int32_t>{ 90, 60, 200, 120 }));
    }

    TEST_F(UIStateTest, ADockTreeComesBackByPanelKey)
    {
        auto addPanels = [](DockingLayout& layout, std::initializer_list<const char*> titles) {
            std::vector<ID> ids;
            uint32_t n = 1;
            for (const char* title : titles)
            {
                ID id = ID::Invalid();
                id.value.integer = 1000 + n++;   // stable test IDs
                layout.panels[id] = Panel{ .id = id, .title = title };
                ids.push_back(id);
            }
            return ids;
        };
        DockingLayout layout;
        const std::vector<ID> a = addPanels(layout, { "Scene", "Details", "Log" });
        layout.root = MakeSplit(DockSplitMode::LeftPinned, 250, MakeLeaf({ a[0] }), MakeLeaf({ a[1], a[2] }));
        app.PersistDocking("dock", layout);
        const std::string saved = app.SaveUIState();

        DockingLayout fresh;   // a new run: new IDs, the panels in one leaf, and a new one
        const std::vector<ID> b = addPanels(fresh, { "Log", "Details", "Scene", "Help" });
        fresh.root = MakeLeaf({ b[0], b[1], b[2], b[3] });
        app.PersistDocking("dock", fresh);
        ASSERT_TRUE(app.RestoreUIState(saved));

        ASSERT_TRUE(fresh.root);
        ASSERT_FALSE(fresh.root->isLeaf);
        EXPECT_EQ(fresh.root->splitSize, CssPx(250));
        EXPECT_EQ(fresh.root->first->panelIds, (std::vector<ID>{ b[2], b[3] })) << "Scene, then the new Help";
        EXPECT_EQ(fresh.root->second->panelIds, (std::vector<ID>{ b[1], b[0] })) << "Details, Log";
    }

    TEST_F(UIStateTest, ToolbarsComeBackOnTheirSides)
    {
        auto item = [](const char* name) { ToolbarItem i; i.debugName = name; return i; };
        std::vector<ToolbarItem> sides[kToolbarSideCount];
        sides[int(ToolbarSide::Top)]  = { item("File"), item("Edit") };
        sides[int(ToolbarSide::Left)] = { item("Tools") };
        app.PersistToolbars("tools", sides);
        const std::string saved = app.SaveUIState();

        std::vector<ToolbarItem> fresh[kToolbarSideCount];
        fresh[int(ToolbarSide::Top)] = { item("Tools"), item("Edit"), item("File"), item("View") };
        app.PersistToolbars("tools", fresh);
        ASSERT_TRUE(app.RestoreUIState(saved));
        auto names = [](const std::vector<ToolbarItem>& side) {
            std::vector<std::string> n;
            for (const ToolbarItem& i : side) n.push_back(i.debugName);
            return n;
        };
        EXPECT_EQ(names(fresh[int(ToolbarSide::Top)]), (std::vector<std::string>{ "File", "Edit", "View" }));
        EXPECT_EQ(names(fresh[int(ToolbarSide::Left)]), (std::vector<std::string>{ "Tools" }));
    }

    // A window not open yet is laid out at its saved rect (its OS window is made from that);
    // one saved off every monitor keeps only its size.
    TEST_F(UIStateTest, AWindowIsMadeAtItsSavedRect)
    {
        const ID windowID = app.idManager.CreateStaticID();
        app.PersistWindow("main", windowID);
        ASSERT_TRUE(app.RestoreUIState(R"({"windows":{"main":{"x":100,"y":50,"w":640,"h":480,"mode":"Normal"}}})"));
        app.Frame([&] {
            if (app.BeginWindow(windowID, "Main", LayoutOverride{}.Size(300, 200)))
                app.Label("inside");
            app.EndWindow();
        });
        const IntRect created = app.GetElementRect(windowID);
        EXPECT_EQ(created.w, 640);
        EXPECT_EQ(created.h, 480);
        EXPECT_EQ(created.x, 100);
        EXPECT_EQ(created.y, 50);
    }

    TEST_F(UIStateTest, AWindowSavedOffEveryMonitorKeepsOnlyItsSize)
    {
        const ID windowID = app.idManager.CreateStaticID();
        app.PersistWindow("main", windowID);
        app.Frame([&] {});   // the monitors are known from the first frame
        ASSERT_TRUE(app.RestoreUIState(R"({"windows":{"main":{"x":-90000,"y":-90000,"w":640,"h":480}}})"));
        app.Frame([&] {
            if (app.BeginWindow(windowID, "Main", LayoutOverride{}.Size(300, 200).Position(10, 20)))
                app.Label("inside");
            app.EndWindow();
        });
        const IntRect created = app.GetElementRect(windowID);
        EXPECT_EQ(created.w, 640);
        EXPECT_EQ(created.x, 10) << "the app's own position, not off-screen";
        EXPECT_EQ(created.y, 20);
    }

    // On the screen but with its top above it, where it can't be grabbed or closed: placed by
    // the app, as if off every monitor.
    TEST_F(UIStateTest, AWindowSavedWithItsTopOffTheScreenIsPlacedByTheApp)
    {
        const ID windowID = app.idManager.CreateStaticID();
        app.PersistWindow("main", windowID);
        app.Frame([&] {});
        ASSERT_TRUE(app.RestoreUIState(R"({"windows":{"main":{"x":331,"y":-77,"w":1700,"h":900}}})"));
        app.Frame([&] {
            if (app.BeginWindow(windowID, "Main", LayoutOverride{}.Size(300, 200).Position(10, 20)))
                app.Label("inside");
            app.EndWindow();
        });
        const IntRect created = app.GetElementRect(windowID);
        EXPECT_EQ(created.h, 900) << "its size is kept";
        EXPECT_EQ(created.y, 20) << "the app's own position, not with its top off the screen";
    }

    // A window saved with its monitor: on that monitor where it was; with the displays
    // rearranged, at the same place on the monitor of the same size; with that monitor gone,
    // at the same place on the primary one, inside its work area.
    class WindowMonitorTest : public UIStateTest
    {
    protected:
        ID windowID = ID::Invalid();
        void SetUp() override
        {
            UIStateTest::SetUp();
            windowID = app.idManager.CreateStaticID();
            app.PersistWindow("main", windowID);
        }
        void SetMonitors(std::vector<MonitorInfo> monitors)
        {
            ON_CALL(app.mockBackend, GetMonitors()).WillByDefault(::testing::Return(monitors));
            app.windowManager->RefreshMonitors();
        }
        static MonitorInfo Monitor(IntRect rect, bool primary)
        {
            MonitorInfo m{};
            m.rect = rect;
            m.workArea = IntRect{ rect.x, rect.y, rect.w, rect.h - 40 };   // a taskbar along the bottom
            m.isPrimary = primary;
            return m;
        }
        IntRect Restore(std::string_view json)
        {
            EXPECT_TRUE(app.RestoreUIState(json));
            app.Frame([&] {
                if (app.BeginWindow(windowID, "Main", LayoutOverride{}.Size(300, 200).Position(10, 20)))
                    app.Label("inside");
                app.EndWindow();
            });
            return app.GetElementRect(windowID);
        }
    };

    TEST_F(WindowMonitorTest, OnItsMonitorWhereItWas)
    {
        SetMonitors({ Monitor({ 0, 0, 1920, 1080 }, true), Monitor({ 1920, 0, 2560, 1440 }, false) });
        const IntRect r = Restore(R"({"windows":{"main":{"x":2100,"y":150,"w":800,"h":600,"monitor":{"x":1920,"y":0,"w":2560,"h":1440}}}})");
        EXPECT_EQ(r.x, 2100);
        EXPECT_EQ(r.y, 150);
    }

    TEST_F(WindowMonitorTest, DisplaysRearrangedGoesToTheMonitorOfTheSameSize)
    {
        // Saved with the big monitor on the left; now it's on the right.
        SetMonitors({ Monitor({ 0, 0, 1920, 1080 }, true), Monitor({ 1920, 0, 2560, 1440 }, false) });
        const IntRect r = Restore(R"({"windows":{"main":{"x":-2460,"y":150,"w":800,"h":600,"monitor":{"x":-2560,"y":0,"w":2560,"h":1440}}}})");
        EXPECT_EQ(r.x, 1920 + 100) << "the same place on it";
        EXPECT_EQ(r.y, 150);
        EXPECT_EQ(r.w, 800);
    }

    TEST_F(WindowMonitorTest, MonitorGoneGoesToThePrimaryInsideItsWorkArea)
    {
        SetMonitors({ Monitor({ 0, 0, 1920, 1080 }, true) });
        const IntRect r = Restore(R"({"windows":{"main":{"x":3500,"y":900,"w":2400,"h":600,"monitor":{"x":1920,"y":0,"w":2560,"h":1440}}}})");
        EXPECT_EQ(r.w, 1920) << "no wider than the monitor";
        EXPECT_EQ(r.x, 0);
        EXPECT_EQ(r.y, 1080 - 40 - 600) << "pulled up above the taskbar";
        EXPECT_EQ(r.h, 600);
    }

    // Saving records the monitor the window's center is on.
    TEST_F(WindowMonitorTest, SavingRecordsTheMonitor)
    {
        using ::testing::_;
        SetMonitors({ Monitor({ 0, 0, 1920, 1080 }, true), Monitor({ 1920, 0, 2560, 1440 }, false) });
        // An OS window, 800x600 at (2100, 150): on the second monitor.
        ON_CALL(app.mockBackend, CreateWindow(_, ::testing::A<IntRect>(), _, _))
            .WillByDefault([](std::string_view, IntRect, WindowFlags, OSWindowHandle) { return reinterpret_cast<OSWindowHandle>(uintptr_t(0x100)); });
        ON_CALL(app.mockBackend, IsWindow(_)).WillByDefault(::testing::Return(true));
        ON_CALL(app.mockBackend, GetWindowRect(_)).WillByDefault(::testing::Return(IntRect{ 2100, 150, 800, 600 }));
        app.Frame([&] { (void)app.windowManager->GetOrCreateWindow(windowID, "Main", IntRect{ 2100, 150, 800, 600 }, nullptr); });
        ASSERT_NE(app.windowManager->GetWindowHandle(windowID), nullptr);

        Result<JsonValue> saved = JsonValue::Parse(app.SaveUIState());
        ASSERT_TRUE(saved);
        const JsonValue* window = saved->Find("windows") ? saved->Find("windows")->Find("main") : nullptr;
        ASSERT_NE(window, nullptr);
        const JsonValue* monitor = window->Find("monitor");
        ASSERT_NE(monitor, nullptr);
        EXPECT_EQ(monitor->Find("x")->AsNumber(), 1920);
        EXPECT_EQ(monitor->Find("w")->AsNumber(), 2560);
        EXPECT_EQ(monitor->Find("h")->AsNumber(), 1440);
    }

    TEST_F(UIStateTest, MalformedOrNewerStateIsAnError)
    {
        EXPECT_FALSE(app.RestoreUIState("{ not json"));
        const Result<void> newer = app.RestoreUIState(R"({"version":99})");
        ASSERT_FALSE(newer);
        EXPECT_EQ(newer.GetError().code, ErrorCode::Unsupported);
        EXPECT_TRUE(app.RestoreUIState(R"({"version":1,"unknown":{"x":1}})")) << "unknown sections are skipped";
    }
}
