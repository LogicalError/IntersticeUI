#include "application.h"

#include <gui_core/diagnostics/gui_logging.h>

#include <algorithm>

static constexpr tachyon::Diagnostics::LogCategory kLogShowcase = tachyon::Diagnostics::MakeLogCategory("showcase");

//
// This file is for testing and prototyping various GUI features related to inspector-like panels, such as field layouts, context menus, etc.
//
// Rows live in a label-keyed table so the search box can filter them by label;
// the visible list is rebuilt only when the query changes. Rows are grouped in
// collapsible sections under the inspected object's header.
//

namespace tachyon
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
            Button("Click me", [this]() { showcase.buttonClickCount++; }, LayoutOverride{}.NoGrow().NoShrink());
            SetTooltip("Click to increment the counter");
            if (BeginContextMenu())
            {
                MenuItem("Reset counter", [this]() { showcase.buttonClickCount = 0; });
            }
            EndContextMenu();
            Text(idManager.AllocateDynamicID("buttonClicks"), "Clicks: " + std::to_string(showcase.buttonClickCount),
                 inspectorCaptionStyleRef);
            EndHorizontal();
        });

        add("Toggle", [this]() {
            Label("Toggle");
            if (Switch(showcase.toggleValue, showcase.toggleValue ? "On" : "Off"))
            {
                TACHYON_LOG_INFO(kLogShowcase, "toggle");
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
                    if (showcase.alignment == i) guiStateManager.Construction().PushSelected();
                    Button(labels[i], [this, i]() { showcase.alignment = i; });
                    if (showcase.alignment == i) guiStateManager.Construction().PopSelected();
                }
            }
            EndButtonGroup();
        });

        // Independent style flags: each toggles on its own.
        add("Style", [this]() {
            Label("Style");
            BeginToggleGroup(LayoutOverride{}.NoGrow());
            {
                Toggle(showcase.bold,      "B");
                Toggle(showcase.italic,    "I");
                Toggle(showcase.underline, "U");
            }
            EndToggleGroup();
        });

        add("Text box", [this]() {
            Label("Text box");
            Field(showcase.textBox1, TextEditBehavior::None);
            SetTooltip("Single-line text field");
            if (BeginContextMenu())
            {
                MenuItem("Cut",        "Ctrl+X", []() { TACHYON_LOG_INFO(kLogShowcase, "Cut"); });
                MenuItem("Copy",       "Ctrl+C", []() { TACHYON_LOG_INFO(kLogShowcase, "Copy"); });
                MenuItem("Paste",      "Ctrl+V", []() { TACHYON_LOG_INFO(kLogShowcase, "Paste"); });
                MenuItem("Select All", "Ctrl+A", []() { TACHYON_LOG_INFO(kLogShowcase, "Select All"); });
            }
            EndContextMenu();
        });

        add("Text box 2", [this]() {
            Label("Text box 2");
            Field(showcase.textBox2, TextEditBehavior::ContinuousUpdate | TextEditBehavior::Multiline);
        });

        add("Autocomplete", [this]() {
            Label("Autocomplete");
            static constexpr std::string_view kFruits[] = {
                "Apple", "Apricot", "Banana", "Blackberry", "Blueberry", "Cherry", "Grape",
                "Kiwi", "Lemon", "Lime", "Mango", "Orange", "Peach", "Pear", "Pineapple", "Plum" };
            AutocompleteField(showcase.fruit, kFruits);
            SetTooltip("Type to see matching fruit; Up / Down and Enter pick one");
        });

        add("Indeterminate", [this]() {
            BeginIndeterminate();
            DragLabel("Indeterminate");
            Field(showcase.floatValue);
            EndIndeterminate();
        });

        // ---- Numbers --------------------------------------------------------
        section = Numbers;
        // One per row: drag the label to scrub the value, or type into the field.
        add("Float",  [this]() { DragLabel("Float"); TypedField(showcase.floatValue); });
        add("Double", [this]() { DragLabel("Double"); TypedField(showcase.doubleValue); });
        add("Int64",  [this]() { DragLabel("Int64"); TypedField(showcase.int64Value); });
        add("Int32",  [this]() { DragLabel("Int32"); TypedField(showcase.int32Value); });
        add("Int16",  [this]() { DragLabel("Int16"); TypedField(showcase.int16Value); });
        add("Int8",   [this]() { DragLabel("Int8"); TypedField(showcase.int8Value); });
        add("UInt64", [this]() { DragLabel("UInt64"); TypedField(showcase.uint64Value); });
        add("UInt32", [this]() { DragLabel("UInt32"); TypedField(showcase.uint32Value); });
        add("UInt16", [this]() { DragLabel("UInt16"); TypedField(showcase.uint16Value); });
        add("UInt8",  [this]() { DragLabel("UInt8"); TypedField(showcase.uint8Value); });

        add("Float [0.25, 1]", [this]() {
            DragLabel("Float [0.25, 1]");
            Field(showcase.clampedFloat, NumericFieldConfig<float>{.minVal = 0.25f, .maxVal = 1.0f });
        });
        add("UInt8 [50, 100]", [this]() {
            DragLabel("UInt8 [50, 100]");
            Field(showcase.clampedUInt8, NumericFieldConfig<uint8_t>{.minVal = 50, .maxVal = 100 });
        });

        add("Volume", [this]() {
            DragLabel("Volume");
            SliderField(showcase.sliderFieldValue, 0.0f, 100.0f, "Volume");
        });

        // NaN and the infinities, side by side.
        add("Special NaN Inf", [this]() {
            Label("Special");
            BeginHorizontal(idManager.AllocateDynamicID("specialRow"), inspectorPairHalfStyleRef, LayoutOverride{}.FlexGrow(1));
            Field(showcase.nanFloat,    LayoutOverride{}.FlexGrow(1));
            Field(showcase.posInfFloat, LayoutOverride{}.FlexGrow(1));
            Field(showcase.negInfFloat, LayoutOverride{}.FlexGrow(1));
            EndHorizontal();
        });

        // ---- Color & vectors ------------------------------------------------
        section = ColorAndVectors;
        add("Color",    [this]() { DragLabel("Color");    ColorField(showcase.demoColor);       });
        add("Gradient", [this]() { DragLabel("Gradient"); GradientField(showcase.demoGradient); });
        add("Int2", [this]() { DragLabel("Int2"); Field(showcase.int2Value); });
        add("Int3", [this]() { DragLabel("Int3"); Field(showcase.int3Value); });
        add("Int4", [this]() { DragLabel("Int4"); Field(showcase.int4Value); });

        // ---- Lists ----------------------------------------------------------
        section = Lists;
        add("String list", [this]() {
            Label("String list", std::to_string(showcase.stringList.size()) + " items");
            Field(showcase.reorderStringListID, showcase.stringList, tachyon::LayoutOverride{}.FlexGrow(1));
        });
        add("Float list", [this]() {
            Label("Float list", std::to_string(showcase.floatList.size()) + " items");
            Field(showcase.reorderFloatListID, showcase.floatList, tachyon::LayoutOverride{}.FlexGrow(1));
        });
        add("Dictionary", [this]() {
            Label("Dictionary", std::to_string(showcase.dictionary.size()) + " entries");
            Field(showcase.dictionary, tachyon::LayoutOverride{}.FlexGrow(1));
        });

        // ---- Menus ----------------------------------------------------------
        // The triggers size to their text, no grow or shrink.
        section = Menus;
        add("Small menu", [this]() {
            Label("Small menu");
            if (BeginMenu(showcase.smallMenuID, "Open",
                          tachyon::LayoutOverride{}.NoGrow().NoShrink()))
            {
                ID item1ID = idManager.AllocateDynamicID();
                ID item2ID = idManager.AllocateDynamicID();
                ID more1ID = idManager.AllocateDynamicID();
                ID more2ID = idManager.AllocateDynamicID();

                ID subItem1ID = idManager.AllocateDynamicID();
                ID subItem2ID = idManager.AllocateDynamicID();
                ID subItem3ID = idManager.AllocateDynamicID();
                ID subItem4ID = idManager.AllocateDynamicID();

                MenuItem(item1ID, "Item 1", []() { TACHYON_LOG_INFO(kLogShowcase, "Item 1 selected"); });
                MenuItem(item2ID, "Item 2", []() { TACHYON_LOG_INFO(kLogShowcase, "Item 2 selected"); });
                if (BeginMenu(more1ID, "More 1..."))
                {
                    MenuItem(subItem1ID, "Sub 1", []() { TACHYON_LOG_INFO(kLogShowcase, "Sub 1"); });
                    MenuItem(subItem2ID, "Sub 2", []() { TACHYON_LOG_INFO(kLogShowcase, "Sub 2"); });
                }
                EndMenu();
                if (BeginMenu(more2ID, "More 2..."))
                {
                    MenuItem(subItem3ID, "Sub 3", []() { TACHYON_LOG_INFO(kLogShowcase, "Sub 3"); });
                    MenuItem(subItem4ID, "Sub 4", []() { TACHYON_LOG_INFO(kLogShowcase, "Sub 4"); });
                }
                EndMenu();
            }
            EndMenu();
        });

        // Scroll test: menu with 100 items
        add("Big menu", [this]() {
            Label("Big menu");
            if (BeginMenu(showcase.bigMenuID, "Open",
                          tachyon::LayoutOverride{}.NoGrow().NoShrink()))
            {
                for (int i = 0; i < 100; i++)
                {
                    char label[32];
                    snprintf(label, sizeof(label), "Item %d", i + 1);
                    int idx = i;
                    MenuItem(label, [idx]() { TACHYON_LOG_INFO(kLogShowcase, "Selected item {}", idx + 1); });
                }
            }
            EndMenu();
        });

        add("Dropdown", [this]() {
            Label("Dropdown");
            if (Dropdown(showcase.quality, { "Low", "Medium", "High", "Ultra" }))
                TACHYON_LOG_INFO(kLogShowcase, "Quality {}", showcase.quality);
        });

        // A long list: it scrolls, and opens on the current option.
        add("Long dropdown", [this]() {
            Label("Long dropdown");
            static const std::vector<std::string> names = [] {
                std::vector<std::string> list;
                for (int i = 1; i <= 40; ++i)
                    list.push_back("Font " + std::to_string(i));
                return list;
            }();
            static const std::vector<std::string_view> options(names.begin(), names.end());
            Dropdown(showcase.fontIndex, options);
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
                        tachyon::LayoutOverride::Auto().NoShrink());
        {
            tachyon::Element icon{};
            icon.id       = idManager.AllocateDynamicID("inspectorObjectIcon");
            icon.iconID   = showcase.boxIcon;
            icon.flags    = tachyon::ElementFlags::NoInput;
            icon.state    = guiStateManager.GetCurrentState(icon.id);
            icon.styleRef = inspectorObjectIconStyleRef;
            RegisterElement(std::move(icon), LayoutOverride{});

            BeginVertical(idManager.AllocateDynamicID("inspectorObjectNames"), inspectorObjectNamesStyleRef,
                          LayoutOverride{}.FlexGrow(1));
            Text(idManager.AllocateDynamicID("inspectorObjectName"), "Sphere", inspectorObjectTitleStyleRef);
            Text(idManager.AllocateDynamicID("inspectorObjectPath"), "Mesh · Scene / Meshes / Meshes", inspectorObjectPathStyleRef);
            EndVertical();

            Button(idManager.AllocateDynamicID("inspectorObjectMore"), { showcase.moreIcon, {}, "Object options" }, []() {}, iconToggleStyleRef);
        }
        EndHorizontal();

        // Search box: filters the rows by label.
        BeginHorizontal(idManager.AllocateDynamicID("inspectorSearchRow"),
                        inspectorSearchRowStyleRef, tachyon::LayoutOverride::Auto().NoShrink());
        if (SearchField(inspectorSearch, showcase.findIcon, TextEditBehavior::ContinuousUpdate,
                        tachyon::LayoutOverride{}.FlexGrow(1)))
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
                   { open ? showcase.chevronDownIcon : showcase.chevronRightIcon, kSectionTitles[s] },
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
