#include "application.h"

#include <gui_core/diagnostics/gui_logging.h>

static constexpr GUI::Diagnostics::LogCategory kLogDemo = GUI::Diagnostics::MakeLogCategory("demo");

//
// This file is for testing and prototyping various GUI features related to inspector-like panels, such as field layouts, context menus, etc.
//
// Rows live in a label-keyed table so the search box can filter them by label;
// the visible list is rebuilt only when the query changes.
//

namespace GUI
{
    namespace
    {
        struct InspectorRow
        {
            const char*           label; // search key, shown by the row itself
            std::function<void()> emit;  // emits the whole row (label + field + extras)
        };
        std::vector<InspectorRow> inspectorRows;
        std::vector<int>          inspectorVisible;
        std::string               inspectorSearch;
        bool                      inspectorFilterDirty = true;
    }

    void Application::EnableInspectorPanel()
    {
        inspectorRows.clear();
        auto add = [](const char* label, std::function<void()> emit) {
            inspectorRows.push_back({ label, std::move(emit) });
        };

        // Menu / Popup example. The trigger sizes to its text — no grow/shrink.
        add("Small Menu", [this]() {
            if (BeginMenu(demo.smallMenuID, "Small Menu",
                          GUI::LayoutOverride{}.NoGrow().NoShrink()))
            {
                ID item1ID = idManager.AllocateDynamicID();
                ID item2ID = idManager.AllocateDynamicID();
                ID more1ID = idManager.AllocateDynamicID();
                ID more2ID = idManager.AllocateDynamicID();

                ID subItem1ID = idManager.AllocateDynamicID();
                ID subItem2ID = idManager.AllocateDynamicID();
                ID subItem3ID = idManager.AllocateDynamicID();
                ID subItem4ID = idManager.AllocateDynamicID();

                MenuItem(item1ID, "Item 1", []() { LOG_INFO(kLogDemo, "Item 1 selected"); });
                MenuItem(item2ID, "Item 2", []() { LOG_INFO(kLogDemo, "Item 2 selected"); });
                if (BeginMenu(more1ID, "More 1..."))
                {
                    MenuItem(subItem1ID, "Sub 1", []() { LOG_INFO(kLogDemo, "Sub 1"); });
                    MenuItem(subItem2ID, "Sub 2", []() { LOG_INFO(kLogDemo, "Sub 2"); });
                    EndMenu();
                }
                if (BeginMenu(more2ID, "More 2..."))
                {
                    MenuItem(subItem3ID, "Sub 3", []() { LOG_INFO(kLogDemo, "Sub 3"); });
                    MenuItem(subItem4ID, "Sub 4", []() { LOG_INFO(kLogDemo, "Sub 4"); });
                    EndMenu();
                }
                EndMenu();
            }
        });

        // Scroll test: menu with 100 items
        add("Big Menu", [this]() {
            if (BeginMenu(demo.bigMenuID, "Big Menu",
                          GUI::LayoutOverride{}.NoGrow().NoShrink()))
            {
                for (int i = 0; i < 100; i++)
                {
                    char label[32];
                    snprintf(label, sizeof(label), "Item %d", i + 1);
                    int idx = i;
                    MenuItem(label, [idx]() { LOG_INFO(kLogDemo, "Selected item {}", idx + 1); });
                }
                EndMenu();
            }
        });

        add("Button", [this]() {
            Label("Button");
            static int prevDemoButtonClickCount = 0;
            static std::string buttonText = "Button clicks: 0";
            if (prevDemoButtonClickCount != demo.buttonClickCount)
            {
                buttonText = "Button clicks: " + std::to_string(demo.buttonClickCount);
                prevDemoButtonClickCount = demo.buttonClickCount;
            }
            Button(buttonText, [this]() { demo.buttonClickCount++; });
            SetTooltip("Click to increment the counter");
            if (BeginContextMenu())
            {
                MenuItem("Reset counter", [this]() { demo.buttonClickCount = 0; });
                EndContextMenu();
            }
        });

        add("Toggle", [this]() {
            Label("Toggle");
            if (Field(demo.toggleValue, demo.toggleValue ? "ON" : "OFF"))
            {
                LOG_INFO(kLogDemo, "toggle");
            }
            SetTooltip("Toggle between ON and OFF");
        });

        // Segmented alignment picker — three buttons packed tight, current
        // selection rendered with the ::highlight(selected) state.
        add("Alignment", [this]() {
            Label("Alignment");
            BeginButtonGroup(LayoutOverride{}.NoGrow());
            {
                const char* labels[] = { "Left", "Center", "Right" };
                for (int i = 0; i < 3; ++i)
                {
                    if (demo.alignment == i) guiStateManager.Construction().PushSelected();
                    Button(labels[i], [this, i]() { demo.alignment = i; });
                    if (demo.alignment == i) guiStateManager.Construction().PopSelected();
                }
            }
            EndButtonGroup();
        });

        // Independent style flags — three toggles packed tight; each toggles
        // on its own and shows the ::highlight(selected) state when on.
        add("Style", [this]() {
            Label("Style");
            BeginToggleGroup(LayoutOverride{}.NoGrow());
            {
                Toggle(demo.bold,      "B");
                Toggle(demo.italic,    "I");
                Toggle(demo.underline, "U");
            }
            EndToggleGroup();
        });

        add("Text Box", [this]() {
            Label("Text Box");
            Field(demo.textBox1, TextEditBehavior::None);
            SetTooltip("Single-line text field");
            if (BeginContextMenu())
            {
                MenuItem("Cut",        "Ctrl+X", []() { LOG_INFO(kLogDemo, "Cut"); });
                MenuItem("Copy",       "Ctrl+C", []() { LOG_INFO(kLogDemo, "Copy"); });
                MenuItem("Paste",      "Ctrl+V", []() { LOG_INFO(kLogDemo, "Paste"); });
                MenuItem("Select All", "Ctrl+A", []() { LOG_INFO(kLogDemo, "Select All"); });
                EndContextMenu();
            }
        });

        add("Text Box2", [this]() {
            Label("Text Box2");
            Field(demo.textBox2, TextEditBehavior::ContinuousUpdate | TextEditBehavior::Multiline);
        });

        add("Float Value",  [this]() { DragLabel("Float Value");  Field(demo.floatValue);  });
        add("Double Value", [this]() { DragLabel("Double Value"); Field(demo.doubleValue); });
        add("Int64 Value",  [this]() { DragLabel("Int64 Value");  Field(demo.int64Value);  });
        add("Int32 Value",  [this]() { DragLabel("Int32 Value");  Field(demo.int32Value);  });
        add("Int16 Value",  [this]() { DragLabel("Int16 Value");  Field(demo.int16Value);  });
        add("Int8 Value",   [this]() { DragLabel("Int8 Value");   Field(demo.int8Value);   });
        add("UInt64 Value", [this]() { DragLabel("UInt64 Value"); Field(demo.uint64Value); });
        add("UInt32 Value", [this]() { DragLabel("UInt32 Value"); Field(demo.uint32Value); });
        add("UInt16 Value", [this]() { DragLabel("UInt16 Value"); Field(demo.uint16Value); });
        add("UInt8 Value",  [this]() { DragLabel("UInt8 Value");  Field(demo.uint8Value);  });

        add("Float [0.25, 1]", [this]() {
            DragLabel("Float [0.25, 1]");
            Field(demo.clampedFloat, NumericFieldConfig<float>{.minVal = 0.25f, .maxVal = 1.0f });
        });
        add("UInt8 [50, 100]", [this]() {
            DragLabel("UInt8 [50, 100]");
            Field(demo.clampedUInt8, NumericFieldConfig<uint8_t>{.minVal = 50, .maxVal = 100 });
        });

        add("Volume", [this]() {
            DragLabel("Volume");
            SliderField(demo.sliderFieldValue, 0.0f, 100.0f, "Volume");
        });

        add("Color",    [this]() { DragLabel("Color");    ColorField(demo.demoColor);       });
        add("Gradient", [this]() { DragLabel("Gradient"); GradientField(demo.demoGradient); });

        add("Float (NaN)",  [this]() { DragLabel("Float (NaN)");  Field(demo.nanFloat);    });
        add("Float (+Inf)", [this]() { DragLabel("Float (+Inf)"); Field(demo.posInfFloat); });
        add("Float (-Inf)", [this]() { DragLabel("Float (-Inf)"); Field(demo.negInfFloat); });

        add("Int2 Value", [this]() { DragLabel("Int2 Value"); Field(demo.int2Value); });
        add("Int3 Value", [this]() { DragLabel("Int3 Value"); Field(demo.int3Value); });
        add("Int4 Value", [this]() { DragLabel("Int4 Value"); Field(demo.int4Value); });

        add("Indeterminate", [this]() {
            BeginIndeterminate();
            DragLabel("Indeterminate");
            Field(demo.floatValue);
            EndIndeterminate();
        });

        add("String List", [this]() {
            Label("String List");
            Field(demo.reorderStringListID, demo.stringList, GUI::LayoutOverride{}.FlexGrow(1));
        });
        add("Float List", [this]() {
            Label("Float List");
            Field(demo.reorderFloatListID, demo.floatList, GUI::LayoutOverride{}.FlexGrow(1));
        });
        add("Dictionary", [this]() {
            Label("Dictionary");
            Field(demo.dictionary, GUI::LayoutOverride{}.FlexGrow(1));
        });

        inspectorFilterDirty = true;
    }

    void Application::DisableInspectorPanel()
    {
        inspectorRows.clear();
        inspectorVisible.clear();
    }

    void Application::RenderInspectorPanel()
    {
        // Search box (top of panel).
        BeginHorizontal(idManager.AllocateDynamicID("inspectorSearchRow"),
                        theme.searchRowStyleRef, GUI::LayoutOverride::Auto().NoShrink());
        if (SearchField(inspectorSearch, demo.findIcon, TextEditBehavior::ContinuousUpdate,
                        GUI::LayoutOverride{}.FlexGrow(1)))
            inspectorFilterDirty = true;
        EndHorizontal();

        // Refilter on demand: match row labels only when the query changed.
        if (inspectorFilterDirty)
        {
            const std::string q = SearchToLower(inspectorSearch);
            inspectorVisible.clear();
            for (int i = 0; i < int(inspectorRows.size()); ++i)
                if (SearchMatches(inspectorRows[i].label, q))
                    inspectorVisible.push_back(i);
            inspectorFilterDirty = false;
        }

        BeginLabelGroup(/*defaultWidth=*/120, /*minWidth=*/40, /*maxWidth=*/300);
        for (int i : inspectorVisible)
            inspectorRows[i].emit();
        EndLabelGroup();
    }

}
