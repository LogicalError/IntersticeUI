#include "application.h"

#include <gui_core/diagnostics/gui_logging.h>

#include <algorithm>

static constexpr interstice::Diagnostics::LogCategory kLogDemo = interstice::Diagnostics::MakeLogCategory("demo");

//
// This file is for testing and prototyping various GUI features related to inspector-like panels, such as field layouts, context menus, etc.
//
// Rows live in a label-keyed table so the search box can filter them by label;
// the visible list is rebuilt only when the query changes. Rows are grouped in
// collapsible sections under the inspected object's header.
//

namespace interstice
{
    namespace
    {
        enum InspectorSection { Controls, Numbers, ColorAndVectors, Lists, Menus, SectionCount };
        const char* const kSectionTitles[SectionCount] = { "Controls", "Numbers", "Color & vectors", "Lists", "Menus" };

        struct InspectorRow
        {
            const char*           label;   // search key, shown by the row itself
            InspectorSection      section;
            std::function<void()> emit;    // emits the whole row (label + field + extras)
        };
        std::vector<InspectorRow> inspectorRows;
        std::vector<int>          inspectorVisible;
        std::string               inspectorSearch;
        bool                      inspectorFilterDirty = true;
        bool                      inspectorSectionOpen[SectionCount] = { true, true, true, true, true };
        ID                        inspectorLabelGroupID = ID::Invalid();
    }

    void Application::EnableInspectorPanel()
    {
        inspectorRows.clear();
        InspectorSection section = Controls;
        auto add = [&section](const char* label, std::function<void()> emit) {
            inspectorRows.push_back({ label, section, std::move(emit) });
        };

        // ---- Controls -------------------------------------------------------
        section = Controls;
        add("Button", [this]() {
            Label("Button");
            BeginHorizontal(idManager.AllocateDynamicID("buttonRow"), inspectorPairHalfStyleRef, LayoutOverride{}.FlexGrow(1));
            Button("Click me", [this]() { demo.buttonClickCount++; }, LayoutOverride{}.NoGrow().NoShrink());
            SetTooltip("Click to increment the counter");
            if (BeginContextMenu())
            {
                MenuItem("Reset counter", [this]() { demo.buttonClickCount = 0; });
            }
            EndContextMenu();
            Text(idManager.AllocateDynamicID("buttonClicks"), "Clicks: " + std::to_string(demo.buttonClickCount),
                 inspectorCaptionStyleRef);
            EndHorizontal();
        });

        add("Toggle", [this]() {
            Label("Toggle");
            if (Switch(demo.toggleValue, demo.toggleValue ? "On" : "Off"))
            {
                INTERSTICE_LOG_INFO(kLogDemo, "toggle");
            }
            SetTooltip("Switch on or off");
        });

        // Segmented alignment picker: the current choice is selected.
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

        // Independent style flags: each toggles on its own.
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

        add("Text box", [this]() {
            Label("Text box");
            Field(demo.textBox1, TextEditBehavior::None);
            SetTooltip("Single-line text field");
            if (BeginContextMenu())
            {
                MenuItem("Cut",        "Ctrl+X", []() { INTERSTICE_LOG_INFO(kLogDemo, "Cut"); });
                MenuItem("Copy",       "Ctrl+C", []() { INTERSTICE_LOG_INFO(kLogDemo, "Copy"); });
                MenuItem("Paste",      "Ctrl+V", []() { INTERSTICE_LOG_INFO(kLogDemo, "Paste"); });
                MenuItem("Select All", "Ctrl+A", []() { INTERSTICE_LOG_INFO(kLogDemo, "Select All"); });
            }
            EndContextMenu();
        });

        add("Text box 2", [this]() {
            Label("Text box 2");
            Field(demo.textBox2, TextEditBehavior::ContinuousUpdate | TextEditBehavior::Multiline);
        });

        add("Indeterminate", [this]() {
            BeginIndeterminate();
            DragLabel("Indeterminate");
            Field(demo.floatValue);
            EndIndeterminate();
        });

        // ---- Numbers --------------------------------------------------------
        section = Numbers;
        // One per row: drag the label to scrub the value, or type into the field.
        add("Float",  [this]() { DragLabel("Float"); TypedField(demo.floatValue); });
        add("Double", [this]() { DragLabel("Double"); TypedField(demo.doubleValue); });
        add("Int64",  [this]() { DragLabel("Int64"); TypedField(demo.int64Value); });
        add("Int32",  [this]() { DragLabel("Int32"); TypedField(demo.int32Value); });
        add("Int16",  [this]() { DragLabel("Int16"); TypedField(demo.int16Value); });
        add("Int8",   [this]() { DragLabel("Int8"); TypedField(demo.int8Value); });
        add("UInt64", [this]() { DragLabel("UInt64"); TypedField(demo.uint64Value); });
        add("UInt32", [this]() { DragLabel("UInt32"); TypedField(demo.uint32Value); });
        add("UInt16", [this]() { DragLabel("UInt16"); TypedField(demo.uint16Value); });
        add("UInt8",  [this]() { DragLabel("UInt8"); TypedField(demo.uint8Value); });

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

        // NaN and the infinities, side by side.
        add("Special NaN Inf", [this]() {
            Label("Special");
            BeginHorizontal(idManager.AllocateDynamicID("specialRow"), inspectorPairHalfStyleRef, LayoutOverride{}.FlexGrow(1));
            Field(demo.nanFloat,    LayoutOverride{}.FlexGrow(1));
            Field(demo.posInfFloat, LayoutOverride{}.FlexGrow(1));
            Field(demo.negInfFloat, LayoutOverride{}.FlexGrow(1));
            EndHorizontal();
        });

        // ---- Color & vectors ------------------------------------------------
        section = ColorAndVectors;
        add("Color",    [this]() { DragLabel("Color");    ColorField(demo.demoColor);       });
        add("Gradient", [this]() { DragLabel("Gradient"); GradientField(demo.demoGradient); });
        add("Int2", [this]() { DragLabel("Int2"); Field(demo.int2Value); });
        add("Int3", [this]() { DragLabel("Int3"); Field(demo.int3Value); });
        add("Int4", [this]() { DragLabel("Int4"); Field(demo.int4Value); });

        // ---- Lists ----------------------------------------------------------
        section = Lists;
        add("String list", [this]() {
            Label("String list", std::to_string(demo.stringList.size()) + " items");
            Field(demo.reorderStringListID, demo.stringList, interstice::LayoutOverride{}.FlexGrow(1));
        });
        add("Float list", [this]() {
            Label("Float list", std::to_string(demo.floatList.size()) + " items");
            Field(demo.reorderFloatListID, demo.floatList, interstice::LayoutOverride{}.FlexGrow(1));
        });
        add("Dictionary", [this]() {
            Label("Dictionary", std::to_string(demo.dictionary.size()) + " entries");
            Field(demo.dictionary, interstice::LayoutOverride{}.FlexGrow(1));
        });

        // ---- Menus ----------------------------------------------------------
        // The triggers size to their text, no grow or shrink.
        section = Menus;
        add("Small menu", [this]() {
            Label("Small menu");
            if (BeginMenu(demo.smallMenuID, "Open",
                          interstice::LayoutOverride{}.NoGrow().NoShrink()))
            {
                ID item1ID = idManager.AllocateDynamicID();
                ID item2ID = idManager.AllocateDynamicID();
                ID more1ID = idManager.AllocateDynamicID();
                ID more2ID = idManager.AllocateDynamicID();

                ID subItem1ID = idManager.AllocateDynamicID();
                ID subItem2ID = idManager.AllocateDynamicID();
                ID subItem3ID = idManager.AllocateDynamicID();
                ID subItem4ID = idManager.AllocateDynamicID();

                MenuItem(item1ID, "Item 1", []() { INTERSTICE_LOG_INFO(kLogDemo, "Item 1 selected"); });
                MenuItem(item2ID, "Item 2", []() { INTERSTICE_LOG_INFO(kLogDemo, "Item 2 selected"); });
                if (BeginMenu(more1ID, "More 1..."))
                {
                    MenuItem(subItem1ID, "Sub 1", []() { INTERSTICE_LOG_INFO(kLogDemo, "Sub 1"); });
                    MenuItem(subItem2ID, "Sub 2", []() { INTERSTICE_LOG_INFO(kLogDemo, "Sub 2"); });
                }
                EndMenu();
                if (BeginMenu(more2ID, "More 2..."))
                {
                    MenuItem(subItem3ID, "Sub 3", []() { INTERSTICE_LOG_INFO(kLogDemo, "Sub 3"); });
                    MenuItem(subItem4ID, "Sub 4", []() { INTERSTICE_LOG_INFO(kLogDemo, "Sub 4"); });
                }
                EndMenu();
            }
            EndMenu();
        });

        // Scroll test: menu with 100 items
        add("Big menu", [this]() {
            Label("Big menu");
            if (BeginMenu(demo.bigMenuID, "Open",
                          interstice::LayoutOverride{}.NoGrow().NoShrink()))
            {
                for (int i = 0; i < 100; i++)
                {
                    char label[32];
                    snprintf(label, sizeof(label), "Item %d", i + 1);
                    int idx = i;
                    MenuItem(label, [idx]() { INTERSTICE_LOG_INFO(kLogDemo, "Selected item {}", idx + 1); });
                }
            }
            EndMenu();
        });

        if (inspectorLabelGroupID == ID::Invalid())
            inspectorLabelGroupID = idManager.CreateStaticID();
        inspectorFilterDirty = true;
    }

    void Application::DisableInspectorPanel()
    {
        inspectorRows.clear();
        inspectorVisible.clear();
    }

    void Application::RenderInspectorPanel()
    {
        // ---- The inspected object: icon, name and where it is ----------------
        BeginHorizontal(idManager.AllocateDynamicID("inspectorHeader"), inspectorHeaderStyleRef,
                        interstice::LayoutOverride::Auto().NoShrink());
        {
            interstice::Element icon{};
            icon.id       = idManager.AllocateDynamicID("inspectorObjectIcon");
            icon.iconID   = demo.boxIcon;
            icon.flags    = interstice::ElementFlags::NoInput;
            icon.state    = guiStateManager.GetCurrentState(icon.id);
            icon.styleRef = inspectorObjectIconStyleRef;
            RegisterElement(std::move(icon), LayoutOverride{});

            BeginVertical(idManager.AllocateDynamicID("inspectorObjectNames"), inspectorObjectNamesStyleRef,
                          LayoutOverride{}.FlexGrow(1));
            Text(idManager.AllocateDynamicID("inspectorObjectName"), "Sphere", inspectorObjectTitleStyleRef);
            Text(idManager.AllocateDynamicID("inspectorObjectPath"), "Mesh · Scene / Meshes / Meshes", inspectorObjectPathStyleRef);
            EndVertical();

            Button(idManager.AllocateDynamicID("inspectorObjectMore"), demo.moreIcon, {}, []() {}, iconToggleStyleRef);
            SetTooltip("Object options");
        }
        EndHorizontal();

        // Search box: filters the rows by label.
        BeginHorizontal(idManager.AllocateDynamicID("inspectorSearchRow"),
                        inspectorSearchRowStyleRef, interstice::LayoutOverride::Auto().NoShrink());
        if (SearchField(inspectorSearch, demo.findIcon, TextEditBehavior::ContinuousUpdate,
                        interstice::LayoutOverride{}.FlexGrow(1)))
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

        // ---- Sections: a header that folds it, then its rows ------------------
        bool firstSection = true;
        for (int s = 0; s < SectionCount; ++s)
        {
            const auto rowInSection = [s](int i) { return inspectorRows[size_t(i)].section == s; };
            if (std::none_of(inspectorVisible.begin(), inspectorVisible.end(), rowInSection))
                continue;

            const bool open = inspectorSectionOpen[s];
            Button(idManager.AllocateDynamicID("inspectorSection"),
                   open ? demo.chevronDownIcon : demo.chevronRightIcon, kSectionTitles[s],
                   [s]() { inspectorSectionOpen[s] = !inspectorSectionOpen[s]; },
                   firstSection ? inspectorSectionFirstStyleRef : inspectorSectionStyleRef,
                   LayoutOverride{}.NoGrow().NoShrink());
            firstSection = false;
            if (!open)
                continue;

            BeginVertical(idManager.AllocateDynamicID("inspectorSectionBody"), inspectorSectionBodyStyleRef,
                          LayoutOverride::Auto().NoShrink());
            BeginLabelGroup(inspectorLabelGroupID, /*defaultWidth=*/116, /*minWidth=*/40, /*maxWidth=*/300);
            for (int i : inspectorVisible)
                if (rowInSection(i))
                    inspectorRows[size_t(i)].emit();
            EndLabelGroup();
            EndVertical();
        }
    }

}
