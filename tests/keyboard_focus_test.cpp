// Keyboard focus: Tab reaches every control, and every control it reaches shows it has the
// focus (its style changes when focused, or that of a box around it that takes its state).
// Controls in popup windows (the color picker, the gradient editor, a message dialog) are in
// their own windows' Tab order, which this doesn't reach.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_core/style/gui_style.h>

#include <algorithm>
#include <iterator>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace tachyon
{
    using GUITest::HeadlessGUIApp;

    namespace
    {
        class KeyboardFocusTest : public ::testing::Test
        {
        protected:
            void SetUp() override
            {
                ASSERT_TRUE(app.Initialize());
                for (ID* id : { &buttonID, &toggleID, &checkboxID, &switchID, &textID, &numberID,
                                &sliderFieldID, &intSliderFieldID, &sliderID, &colorID, &gradientID, &listID, &disabledID,
                                &toolbarButtonID, &toolbarToggleID, &toolbarMenuID, &breadcrumbID, &typedID,
                                &listViewID, &tableID, &dropdownID })
                    *id = app.idManager.CreateStaticID();
                tableColumns.push_back({
                    .column = { .header = "Name", .initialWidth = 120, .minWidth = 60, .maxWidth = 300 },
                    .generateCell = [this](int r) { app.Cell(r == 0 ? "First" : "Second"); } });
            }
            void TearDown() override { app.Cleanup(); }

            void Frame()
            {
                app.Frame([&] {
                    app.BeginRootWindow();
                    app.Button(buttonID, "Button", [] {});
                    app.Toggle(toggleID, toggle, "Toggle");
                    app.Checkbox(checkboxID, checkbox, "Checkbox", [this](bool v) { checkbox = v; });
                    app.Switch(switchID, switchOn, "Switch");
                    app.Field(textID, text);
                    app.Field(numberID, number);
                    app.SliderField(sliderFieldID, volume, 0.0f, 100.0f, "Volume");
                    app.SliderField(intSliderFieldID, count, 0, 10, "Count");
                    app.Slider(sliderID, level, 0, 10);
                    app.ColorField(colorID, color);
                    app.GradientField(gradientID, gradient);
                    app.Field(listID, list);
                    app.BeginToolbar();
                    app.Button(toolbarButtonID, "Tool", [] {});
                    app.Toggle(toolbarToggleID, toolbarToggle, "Snap");
                    app.BeginMenu(toolbarMenuID, "View");
                    app.EndMenu();
                    app.EndToolbar();
                    app.Breadcrumb(breadcrumbID, crumbs, [](int) {});
                    app.TypedField(typedID, typed);
                    if (app.BeginListView(listViewID, &listSelection, 2))
                    {
                        app.ListItem("First");
                        app.ListItem("Second");
                    }
                    app.EndListView();
                    app.VirtualTable(tableID, tableState, std::span<VirtualTableColumn>(tableColumns), 2, 0,
                                     app.theme.tableStyleRef, LayoutOverride{}.Size(200, 80));
                    app.Dropdown(dropdownID, quality, { "Low", "High" });
                    app.BeginDisabled();
                    app.Button(disabledID, "Disabled", [] {});
                    app.EndDisabled();
                    app.EndRootWindow();
                });
            }

            // Whether `id`'s element, or a box around it that takes its state, looks different
            // when focused.
            bool ShowsFocus(ID id) const
            {
                const auto& storage = app.elementStorage;
                for (ElementRef ref = storage.FindElementRefByID(id); ref.IsValid(); ref = storage.GetParentRef(ref))
                {
                    const Element& e = storage.GetElementUnsafe(ref);
                    const bool self = e.id == id;
                    if (!self && (e.flags & ElementFlags::PropagatesDescendantState) == ElementFlags::None)
                        continue;
                    const VisualBoxStyle* normal  = app.styleManager.GetVisualBoxStyle(e.styleRef.visualBoxStyleIndex, ElementState::None);
                    const VisualBoxStyle* focused = app.styleManager.GetVisualBoxStyle(e.styleRef.visualBoxStyleIndex, ElementState::Focused);
                    if (normal && focused && !(*normal == *focused))
                        return true;
                }
                return false;
            }

            // The control `id` is, or is part of: the nearest named ancestor, and its text.
            std::string Describe(ID id) const
            {
                const std::pair<ID, const char*> names[] = {
                    { buttonID, "Button" }, { toggleID, "Toggle" }, { checkboxID, "Checkbox" }, { switchID, "Switch" },
                    { textID, "Field(string)" }, { numberID, "Field(float)" }, { sliderFieldID, "SliderField(float)" },
                    { intSliderFieldID, "SliderField(int)" }, { sliderID, "Slider" }, { colorID, "ColorField" },
                    { gradientID, "GradientField" }, { listID, "Field(list)" }, { toolbarButtonID, "toolbar Button" },
                    { toolbarToggleID, "toolbar Toggle" }, { toolbarMenuID, "toolbar BeginMenu" }, { breadcrumbID, "Breadcrumb" },
                    { typedID, "TypedField" }, { listViewID, "ListView" }, { tableID, "VirtualTable" }, { dropdownID, "Dropdown" } };
                const auto& storage = app.elementStorage;
                std::string name = "an unnamed control";
                for (ElementRef ref = storage.FindElementRefByID(id); ref.IsValid(); ref = storage.GetParentRef(ref))
                {
                    const ID at = storage.GetElementUnsafe(ref).id;
                    const auto it = std::find_if(std::begin(names), std::end(names), [at](const auto& n) { return n.first == at; });
                    if (it == std::end(names)) continue;
                    name = it->first == id ? it->second : std::string("a part of ") + it->second;
                    break;
                }
                const std::string_view shown = app.TextOf(id);
                return shown.empty() ? name : name + " (\"" + std::string(shown) + "\")";
            }

            HeadlessGUIApp app;
            ID buttonID, toggleID, checkboxID, switchID, textID, numberID, sliderFieldID, intSliderFieldID,
               sliderID, colorID, gradientID, listID, disabledID, toolbarButtonID, toolbarToggleID, toolbarMenuID,
               breadcrumbID, typedID, listViewID, tableID, dropdownID;
            bool toolbarToggle = false;
            std::vector<std::string> crumbs { "Graphs", "Terrain" };
            int typed = 7, listSelection = 0, quality = 0;
            TableState tableState;
            std::vector<VirtualTableColumn> tableColumns;
            bool toggle = false, checkbox = false, switchOn = false;
            std::string text = "text";
            float number = 1.0f, volume = 50.0f;
            int count = 3, level = 4;
            color4f color { 1.0f, 0.5f, 0.0f, 1.0f };
            Gradient gradient;
            std::vector<std::string> list { "one", "two" };
        };

        TEST_F(KeyboardFocusTest, TabReachesEveryControlAndEachShowsTheFocus)
        {
            Frame();
            Frame();
            std::vector<ID> visited;
            for (int i = 0; i < 80; ++i)
            {
                app.InjectKey(KeyCode::Tab, KeyAction::KeyDown);
                Frame();
                app.InjectKey(KeyCode::Tab, KeyAction::KeyUp);
                Frame();
                const ID focused = app.guiStateManager.Interaction().GetFocusedID();
                if (focused == ID::Invalid() || (!visited.empty() && focused == visited.front()))
                    break;
                visited.push_back(focused);
            }
            ASSERT_FALSE(visited.empty());

            const std::set<ID> reached(visited.begin(), visited.end());
            for (const ID id : { buttonID, toggleID, checkboxID, switchID, textID, numberID,
                                 sliderFieldID, intSliderFieldID, sliderID, toolbarButtonID, toolbarToggleID,
                                 toolbarMenuID, typedID, listViewID, tableID, dropdownID })
                EXPECT_TRUE(reached.contains(id)) << "Tab skips a control: " << Describe(id);

            for (const ID id : visited)
            {
                EXPECT_NE(id, disabledID) << "Tab reaches a disabled control";
                // Its state is only current once it's been drawn focused.
                app.guiStateManager.Interaction().SetFocusID(id);
                Frame();
                EXPECT_TRUE(ShowsFocus(id)) << "a Tab stop shows no focus: " << Describe(id);
            }
        }

        // A control disabled while it has the focus doesn't act on keys, as it doesn't on clicks.
        TEST(KeyboardFocus, ADisabledControlTakesNoKeysEvenWithTheFocus)
        {
            HeadlessGUIApp app;
            ASSERT_TRUE(app.Initialize());
            const ID buttonID = app.idManager.CreateStaticID();
            bool disabled = false;
            int clicks = 0;
            auto frame = [&] {
                app.Frame([&] {
                    app.BeginRootWindow();
                    if (disabled) app.BeginDisabled();
                    app.Button(buttonID, "Run", [&clicks] { ++clicks; });
                    if (disabled) app.EndDisabled();
                    app.EndRootWindow();
                });
            };
            frame();
            app.guiStateManager.Interaction().SetFocusID(buttonID);
            frame();
            app.InjectKey(KeyCode::Space, KeyAction::KeyDown);
            frame();
            ASSERT_EQ(clicks, 1) << "test setup: Space runs a focused button";

            disabled = true;
            frame();
            app.InjectKey(KeyCode::Space, KeyAction::KeyDown);
            frame();
            frame();
            EXPECT_EQ(clicks, 1);
            app.Cleanup();
        }

        // A control's parts have IDs under its own: they are the same whether or not another
        // control like it was drawn before it.
        TEST(ControlIDs, PartsDontDependOnWhatCameBefore)
        {
            HeadlessGUIApp app;
            ASSERT_TRUE(app.Initialize());
            const ID firstID = app.idManager.CreateStaticID();
            const ID secondID = app.idManager.CreateStaticID();
            bool on = false;
            float volume = 0.5f;
            int count = 3;
            color4f color{ 1, 0, 0, 1 };
            std::string search;
            using Draw = std::function<void(ID)>;
            const std::vector<std::pair<const char*, Draw>> controls = {
                { "Checkbox",    [&](ID id) { app.Checkbox(id, on, "On"); } },
                { "Switch",      [&](ID id) { app.Switch(id, on, "On"); } },
                { "SliderField", [&](ID id) { app.SliderField(id, volume, 0.0f, 1.0f, "Volume"); } },
                { "SliderField(int)", [&](ID id) { app.SliderField(id, count, 0, 10, "Count"); } },
                { "ColorField",  [&](ID id) { app.ColorField(id, color); } },
                { "SearchField", [&](ID id) { app.SearchField(id, search, app.theme.searchIcon); } },
                { "TypedField",  [&](ID id) { app.TypedField(id, volume); } },
            };
            // The IDs of everything under `id`'s element, in order.
            auto partsOf = [&](ID id) {
                std::vector<ID> parts;
                std::function<void(ElementRef)> walk = [&](ElementRef ref) {
                    const IndexRange range = app.elementStorage.GetChildRange(ref);
                    for (uint32_t i = range.begin; i < range.end; ++i)
                    {
                        const ElementRef child{ ref.depth + 1, i };
                        parts.push_back(app.elementStorage.GetElementUnsafe(child).id);
                        walk(child);
                    }
                };
                ElementRef ref = app.elementStorage.FindElementRefByID(id);
                // A control drawn inside a box of its own (TypedField): the box's parts.
                if (ref.IsValid() && !app.elementStorage.HasChildren(ref))
                    ref = app.elementStorage.GetParentRef(ref);
                if (ref.IsValid()) walk(ref);
                return parts;
            };
            for (const auto& [name, draw] : controls)
            {
                std::vector<ID> alone, afterAnother;
                app.Frame([&] { app.BeginRootWindow(); draw(secondID); alone = partsOf(secondID); app.EndRootWindow(); });
                app.Frame([&] { app.BeginRootWindow(); draw(firstID); draw(secondID); afterAnother = partsOf(secondID); app.EndRootWindow(); });
                ASSERT_FALSE(alone.empty()) << name;
                EXPECT_EQ(alone, afterAnother) << name;
            }
            app.Cleanup();
        }

        // Every control has the four forms (ID or call site, own style or the theme's), and a
        // style given goes on the control's outer element, the one SetTooltip attaches to.
        TEST(ControlOverloads, EveryFormBuildsAndTakesTheStyle)
        {
            HeadlessGUIApp app;
            ASSERT_TRUE(app.Initialize());
            const StyleRef style = app.theme.emptyHorzStyleRef;   // any style, to look for
            bool on = false;
            float f = 0.5f;
            int i = 2, pick = 0;
            color4f c4f{ 1, 0, 0, 1 };
            color4b c4b{ 255, 0, 0, 255 };
            Gradient gradient;
            gradient.AddStop(color4f{ 0, 0, 0, 1 }, 0.0f);
            gradient.AddStop(color4f{ 1, 1, 1, 1 }, 1.0f);
            std::string text, search, typed;
            const std::string_view suggestions[] = { "alpha", "beta" };
            std::vector<std::string> list{ "a" };
            std::vector<std::pair<std::string, float>> dictionary{ { "k", 1.0f } };
            std::unordered_map<std::string, int> map{ { "k", 1 } };
            const std::string crumbs[] = { "A", "B" };
            CommandSearchState commands;
            std::vector<std::pair<const char*, ID>> styled;   // the ID forms given `style`
            auto id = [&](const char* name) { const ID v = app.idManager.CreateStaticID(); styled.push_back({ name, v }); return v; };
            app.Frame([&] {
                app.BeginRootWindow();
                // ID + style
                app.Checkbox(id("Checkbox"), on, "On", style);
                app.Switch(id("Switch"), on, "On", style);
                app.SliderField(id("SliderField"), f, 0.0f, 1.0f, "F", style);
                app.SliderField(id("SliderField(int)"), i, 0, 10, "I", style);
                app.Slider(id("Slider"), i, 0, 10, style);
                app.ColorField(id("ColorField"), c4f, false, style);
                app.ColorField(id("ColorField(4b)"), c4b, style);
                app.GradientField(id("GradientField"), gradient, style);
                app.SearchField(id("SearchField"), search, app.theme.searchIcon, TextEditBehavior::ContinuousUpdate, style);
                app.AutocompleteField(id("AutocompleteField"), text, suggestions, style);
                app.Dropdown(id("Dropdown"), pick, { "One", "Two" }, style);
                app.StatusText(id("StatusText"), "Ready", style);
                app.CommandSearch(id("CommandSearch"), commands, style);
                app.Label(id("Label(caption)"), "Items", "2 items", style);
                app.Field(id("Field(float, style)"), f, style);
                app.Field(id("List"), list, style);
                app.Field(id("Dictionary"), dictionary, style);
                app.Field(id("Map"), map, style);
                app.Breadcrumb(id("Breadcrumb"), crumbs, nullptr, style);
                // Call site, with and without a style
                app.Checkbox(on, "On", style);
                app.Checkbox(on, "On");
                app.Switch(on, "On", style);
                app.SliderField(f, 0.0f, 1.0f, "F", style);
                app.Slider(i, 0, 10, style);
                app.ColorField(c4f, false, style);
                app.ColorField(c4b, style);
                app.GradientField(gradient, style);
                app.SearchField(search, app.theme.searchIcon, TextEditBehavior::ContinuousUpdate, style);
                app.AutocompleteField(text, suggestions, style);
                app.Dropdown(pick, { "One", "Two" }, style);
                app.StatusText("Ready", style);
                app.CommandSearch(commands, style);
                app.Label("Items", "2 items", style);
                app.TypedField(f, style);
                app.Field(f, style);
                app.Field(list, style);
                app.Field(list);
                app.Field(dictionary);
                app.Field(map, style);
                app.Breadcrumb(crumbs, nullptr, style);
                app.EndRootWindow();
            });
            for (const auto& [name, controlID] : styled)
            {
                ASSERT_TRUE(app.HasElement(controlID)) << name;
                const ElementRef ref = app.elementStorage.FindElementRefByID(controlID);
                EXPECT_TRUE(app.elementStorage.GetElementUnsafe(ref).styleRef == style) << name << " takes the style";
            }
            app.Cleanup();
        }

        // Alt+Down / Up moves the list row the focus is in, as a drag of its grip does; the
        // focus goes with the row, to the same field.
        TEST(KeyboardFocus, AltArrowsMoveAListRow)
        {
            HeadlessGUIApp app;
            ASSERT_TRUE(app.Initialize());
            const ID listID = app.idManager.CreateStaticID();
            std::vector<std::string> items{ "alpha", "beta", "gamma" };
            auto frame = [&] {
                app.Frame([&] {
                    app.BeginRootWindow();
                    app.Field(listID, items);
                    app.EndRootWindow();
                });
            };
            auto focusedText = [&] { return std::string(app.TextOf(app.guiStateManager.Interaction().GetFocusedID())); };
            frame();
            frame();
            for (int i = 0; i < 4 && focusedText() != "alpha"; ++i)
            {
                app.InjectKey(KeyCode::Tab);
                frame();
                frame();
            }
            ASSERT_EQ(focusedText(), "alpha") << "test setup: the first row's field has the focus";

            app.InjectKey(KeyCode::Down, KeyAction::KeyDown, KeyModifier::Alt);
            frame();
            frame();
            EXPECT_EQ(items, (std::vector<std::string>{ "beta", "alpha", "gamma" }));
            EXPECT_EQ(focusedText(), "alpha") << "the focus went with the row";

            app.InjectKey(KeyCode::Down, KeyAction::KeyDown, KeyModifier::Alt);
            frame();
            frame();
            app.InjectKey(KeyCode::Down, KeyAction::KeyDown, KeyModifier::Alt);
            frame();
            frame();
            EXPECT_EQ(items, (std::vector<std::string>{ "beta", "gamma", "alpha" })) << "it stops at the end";
            EXPECT_EQ(focusedText(), "alpha");

            app.InjectKey(KeyCode::Up, KeyAction::KeyDown, KeyModifier::Alt);
            frame();
            frame();
            EXPECT_EQ(items, (std::vector<std::string>{ "beta", "alpha", "gamma" }));
            app.Cleanup();
        }
    }
}
