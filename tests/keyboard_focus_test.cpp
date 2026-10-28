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
    }
}
