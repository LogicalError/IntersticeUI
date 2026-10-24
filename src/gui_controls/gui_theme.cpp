#include <gui_core/gui_base_types.h>
#include <gui_core/diagnostics/gui_logging.h>

#include <gui_core/style/gui_style_ref.h>
#include <gui_core/style/gui_style_text.h>
#include <gui_core/style/gui_style.h>
#include <gui_core/style/gui_style_sheet.h>
#include <gui_core/style/gui_style_manager.h>

#include <gui_core/text/gui_text_system.h>

#include <gui_controls/gui_theme.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace interstice
{
    namespace
    {
        constexpr Diagnostics::LogCategory kLogTheme = Diagnostics::MakeLogCategory("theme");
    }

    void GUITheme::InitializeDefaultStyles(StyleManager& styleManager, ITextSystem* textSystem)
    {
        BoxEdges   noBorder = { 0, 0, 0, 0 };
        BoxCorners noRadius = { 0, 0, 0, 0 };

        // Used for boxes rendered with an invalid style index.
        {
            Style invalidStyle{};
            invalidStyle.textLayoutStyle = interstice::TextLayoutStyle
            {
                .textDirection = interstice::TextDirection::Auto,
                .textWrapping = interstice::TextWrap::WordChar,
                .horizontalAlignment = interstice::TextAlign::Start,
                .baseline = interstice::TextBaseline::Middle
            };
            invalidStyle.borderWidths = noBorder;
            invalidStyle.borderRadius = noRadius;
            invalidStyle.margin = BoxEdges(2);
            invalidStyle.SetOverflow(interstice::Overflow::Hidden);
            invalidStyle.padding = interstice::BoxEdges(10);
            auto normal_border_color_style = ColorStyle::Create(color4b(121, 153, 169, 255));
            auto normal_background_color_style = ColorStyle::Create(color4b(101, 133, 149, 255));
            for (int i = 0; i < Style::kStateCount; i++)
            {
                invalidStyle.states[i].SetBorderColor({});
                invalidStyle.states[i].backgroundColor = normal_border_color_style;
            }
            invalidStyle.states[Style::kNormal].SetBorderColor(normal_border_color_style);
            invalidStyle.states[Style::kNormal].backgroundColor = normal_background_color_style;
            styleManager.Register(invalidStyle);
        }

        {
            Style newStyle{};
            newStyle.textLayoutStyle = {};
            newStyle.borderWidths = noBorder;
            newStyle.borderRadius = noRadius;
            newStyle.SetOverflow(interstice::Overflow::Visible);
            newStyle.display = interstice::Display::Flex;
            newStyle.layoutData.flex.direction = interstice::FlexDirection::Row;
            newStyle.layoutData.flex.wrap = interstice::FlexWrap::Wrap;
            newStyle.layoutData.flex.alignItems = interstice::AlignItems::Stretch;
            newStyle.layoutData.flex.justifyContent = interstice::JustifyContent::FlexStart;
            newStyle.layoutData.flex.flexGrow = 1;
            newStyle.layoutData.flex.gap = 0;
            newStyle.padding = interstice::BoxEdges(0);
            for (int i = 0; i < Style::kStateCount; i++)
            {
                newStyle.states[i].SetBorderColor({});
                newStyle.states[i].backgroundColor = {};
            }
            fieldElementGroupStyleRef = styleManager.Register(newStyle);
        }

        // Built-in icons: Lucide shapes (ISC), 24-unit strokes with round caps. One
        // neutral grey, so they draw as masks in the color of where they're shown.
        {
            auto registerIcon = [&](const char* name, const char* shapes) {
                const std::string svg = std::string(R"(<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg" fill="none" stroke="#909090" )"
                                                    R"(stroke-width="2" stroke-linecap="round" stroke-linejoin="round">)") + shapes + "</svg>";
                return textSystem->RegisterIconFromSVG(name, svg.data(), svg.size());
            };
            treeArrowRight  = registerIcon("tree-arrow-right", R"(<path d="m9 18 6-6-6-6"/>)");
            treeArrowDown   = registerIcon("tree-arrow-down",  R"(<path d="m6 9 6 6 6-6"/>)");
            sortArrowUpIcon   = registerIcon("sort-arrow-up",   R"(<path d="m18 15-6-6-6 6"/>)");
            sortArrowDownIcon = registerIcon("sort-arrow-down", R"(<path d="m6 9 6 6 6-6"/>)");
            tabCloseIcon    = registerIcon("tab-close",  R"(<path d="M18 6 6 18"/><path d="m6 6 12 12"/>)");
            menuCheckIcon   = registerIcon("menu-check", R"(<path d="M20 6 9 17l-5-5"/>)");
            addIcon         = registerIcon("list-add",    R"(<path d="M5 12h14"/><path d="M12 5v14"/>)");
            removeIcon      = registerIcon("list-remove", R"(<path d="M5 12h14"/>)");
            dragHandleIcon  = registerIcon("drag-handle",
                R"(<circle cx="9" cy="12" r="1"/><circle cx="9" cy="5" r="1"/><circle cx="9" cy="19" r="1"/>)"
                R"(<circle cx="15" cy="12" r="1"/><circle cx="15" cy="5" r="1"/><circle cx="15" cy="19" r="1"/>)");
            searchIcon      = registerIcon("search", R"(<path d="m21 21-4.34-4.34"/><circle cx="11" cy="11" r="8"/>)");
            windowMinimizeIcon = registerIcon("window-minimize", R"(<path d="M5 12h14"/>)");
            windowMaximizeIcon = registerIcon("window-maximize", R"(<rect x="4" y="4" width="16" height="16" rx="2"/>)");
            windowRestoreIcon  = registerIcon("window-restore",
                R"(<rect x="4" y="8" width="12" height="12" rx="2"/><path d="M8 8V6a2 2 0 0 1 2-2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2h-2"/>)");
        }

        {
            // Undecorated flex containers: the BeginHorizontal / BeginVertical defaults.
            auto makeEmptyFlexStyle = [&](interstice::FlexDirection dir, interstice::FlexWrap wrap) -> StyleRef
            {
                Style newStyle{};
                newStyle.textLayoutStyle = {};
                newStyle.borderWidths = noBorder;
                newStyle.borderRadius = noRadius;
                newStyle.SetOverflow(interstice::Overflow::Auto);
                newStyle.display = interstice::Display::Flex;
                newStyle.layoutData.flex.direction = dir;
                newStyle.layoutData.flex.wrap = wrap;
                newStyle.layoutData.flex.alignItems = interstice::AlignItems::Stretch;
                newStyle.layoutData.flex.justifyContent = interstice::JustifyContent::FlexStart;
                newStyle.padding = interstice::BoxEdges(0);
                for (int i = 0; i < Style::kStateCount; i++)
                {
                    newStyle.states[i].SetBorderColor({});
                    newStyle.states[i].backgroundColor = {};
                }
                return styleManager.Register(newStyle);
            };
            emptyHorzStyleRef     = makeEmptyFlexStyle(interstice::FlexDirection::Row, interstice::FlexWrap::NoWrap);
            emptyHorzWrapStyleRef = makeEmptyFlexStyle(interstice::FlexDirection::Row, interstice::FlexWrap::Wrap);
            emptyVertStyleRef     = makeEmptyFlexStyle(interstice::FlexDirection::Column, interstice::FlexWrap::NoWrap);
            emptyVertWrapStyleRef = makeEmptyFlexStyle(interstice::FlexDirection::Column, interstice::FlexWrap::Wrap);
        }
    }

    // Faces sharing a family name register as one family, in source order.
    // On a theme reload, families whose faces changed pick up the new files.
    static void RegisterFontFaces(ITextSystem& textSystem, const std::vector<FontFace>& faces,
                                  const std::filesystem::path& baseDir)
    {
        std::vector<FontID> done;
        for (const FontFace& first : faces)
        {
            const FontID id = FontIDFromName(first.family);
            if (std::find(done.begin(), done.end(), id) != done.end())
                continue;
            done.push_back(id);

            std::vector<std::string> paths;
            for (const FontFace& face : faces)
                if (FontIDFromName(face.family) == id)
                    paths.push_back((baseDir / face.src).lexically_normal().string());
            const std::vector<std::string_view> views(paths.begin(), paths.end());
            if (textSystem.RegisterFontFamily(first.family, views) == FontID::Invalid)
                INTERSTICE_LOG_ERROR(kLogTheme, "could not load font family '{}'", first.family);
        }
    }

    color4f GUITheme::GetColor(std::string_view varName, color4f fallback) const
    {
        const auto it = m_paletteColors.find(std::string(varName));
        return it != m_paletteColors.end() ? it->second : fallback;
    }

    StyleRef GUITheme::GetStyle(std::string_view cssClass) const
    {
        const auto it = m_classStyles.find(cssClass);
        return it != m_classStyles.end() ? it->second : StyleRef{};
    }

    bool GUITheme::LoadTheme(StyleManager& styleManager, ITextSystem* textSystem, const std::string& themePath,
                             std::span<const std::string> styleFiles, float fontScale)
    {
        // -------------------------------------------------------------------------
        // Theme = palette file (dark.css / light.css, only `:root` vars) + a
        // shared styles file (styles.css next to it) consuming those vars. We
        // concatenate palette-then-styles before parsing so var() references
        // resolve at parse time.
        // -------------------------------------------------------------------------
        std::ifstream paletteFile(themePath);
        if (!paletteFile.good())
        {
            INTERSTICE_LOG_ERROR(kLogTheme, "theme file not found: {}", themePath);
            return false;
        }

        // Palette first, then each style file, so var() references resolve at parse time.
        // Each part's first line in the concatenated text maps errors back to its file.
        const std::filesystem::path themeDir = std::filesystem::path(themePath).parent_path();
        struct Part { std::string name; int firstLine; };
        std::vector<Part> parts;
        std::ostringstream ss;
        int line = 1;
        auto append = [&](std::string name, std::ifstream& file)
        {
            std::ostringstream text;
            text << file.rdbuf();
            parts.push_back({ std::move(name), line });
            ss << text.view() << '\n';
            line += int(std::count(text.view().begin(), text.view().end(), '\n')) + 1;
        };
        append(themePath, paletteFile);
        for (const std::string& styleFile : styleFiles)
        {
            const std::filesystem::path path = std::filesystem::path(styleFile).is_absolute()
                ? std::filesystem::path(styleFile) : themeDir / styleFile;
            std::ifstream file(path);
            if (!file.good())
            {
                INTERSTICE_LOG_ERROR(kLogTheme, "theme styles file not found: {}", path.string());
                return false;
            }
            append(path.string(), file);
        }
        StyleSheet sheet = ParseStyleSheet(ss.view());

        for (const auto& err : sheet.errors)
        {
            const Part* part = &parts.front();
            for (const Part& p : parts)
                if (err.line >= p.firstLine) part = &p;
            INTERSTICE_LOG_ERROR(kLogTheme, "{}:{}:{}: {}", part->name, err.line - part->firstLine + 1, err.column, err.message);
        }

        if (textSystem)
            RegisterFontFaces(*textSystem, sheet.fontFaces, themeDir);
        INTERSTICE_LOG_INFO(kLogTheme, "loaded {}: {} styles, {} errors", themePath, sheet.styles.size(), sheet.errors.size());

        m_paletteColors.clear();
        for (const auto& [name, prop] : sheet.customProperties)
            if (prop.hasColor)
                m_paletteColors[name] = { prop.color.r / 255.0f, prop.color.g / 255.0f, prop.color.b / 255.0f, prop.color.a / 255.0f };

        // Line color styles from CSS custom properties (with fallbacks)
        auto lineColorFromCSS = [&](const char* varName, color4f fallback) -> interstice::VisualColorStyleIndex
            {
                const auto* prop = sheet.GetCustomProperty(varName);
                if (prop && prop->hasColor)
                    return styleManager.RegisterColorStyle(ColorStyle::Create(prop->color));
                return styleManager.RegisterColorStyle(ColorStyle::Create(fallback));
            };

        lineGrayStyle      = lineColorFromCSS("--text-3", color4f{ 0.4f, 0.4f, 0.4f, 1.0f });
        lineYellowStyle    = lineColorFromCSS("--warn", color4f{ 1.0f, 0.8f, 0.2f, 1.0f });
        lineWhiteStyle     = lineColorFromCSS("--text-1", color4f{ 1.0f, 1.0f, 1.0f, 1.0f });
        lineDarkStyle      = lineColorFromCSS("--bg-app", color4f{ 0.1f, 0.1f, 0.1f, 1.0f });
        treeGuideLineStyle = lineColorFromCSS("--line-node", color4f{ 0.5f, 0.5f, 0.5f, 1.0f });
        treeDropIndicatorStyle = lineColorFromCSS("--accent", color4f{ 0.0f, 0.478f, 0.8f, 1.0f });

        {
            // Distance-from-edge gradient (the quad shader feeds the rounded-box
            // SDF in): transparent at the edge → black just inside, giving the
            // offset shadow a soft edge. Early dark stop keeps the ramp a few px
            // wide rather than spanning half the box.
            Gradient shadowGradient = Gradient::Linear(0.0f);
            shadowGradient.AddStop(color4f{ 0.0f, 0.0f, 0.0f, 0.0f }, 0.00f);
            shadowGradient.AddStop(color4f{ 0.0f, 0.0f, 0.0f, 0.5f }, 0.12f);
            shadowGradient.AddStop(color4f{ 0.0f, 0.0f, 0.0f, 0.5f }, 1.00f);
            shadowColorStyle = styleManager.RegisterColorStyle(
                ColorStyle::Create(shadowGradient, ColorType::SDFGradient));
        }

        marqueeFillColorStyle = styleManager.RegisterColorStyle(
            ColorStyle::Create(color4f{ 0.3f, 0.6f, 1.0f, 0.15f }));

        // Registers a parsed style by name, with the global font scale applied.
        auto registerCSS = [&](std::string_view name) -> StyleRef
            {
                const Style* p = sheet.GetStyle(name);
                Style s = p ? *p : sheet.baseStyle;
                if (fontScale != 1.0f)
                {
                    for (int i = 0; i < Style::kStateCount; i++)
                        s.states[i].textStyle.fontSize *= fontScale;
                }
                return styleManager.Register(s);
            };

        m_classStyles.clear();
        for (const auto& [name, style] : sheet.styles)
            m_classStyles.emplace(name, registerCSS(name));

        // -------------------------------------------------------------------------
        // Named widget styles from CSS
        // -------------------------------------------------------------------------
        regularControls.button       = registerCSS("button");
        regularControls.toggle       = registerCSS("toggle");
        regularControls.textbox      = registerCSS("textbox");
        regularControls.menuTrigger  = registerCSS("menu-trigger");

        toolbarControls.button       = registerCSS("toolbar-button");
        toolbarControls.toggle       = registerCSS("toolbar-toggle");
        toolbarControls.textbox      = registerCSS("toolbar-textbox");
        toolbarControls.menuTrigger  = registerCSS("toolbar-menu-trigger");

        labelStyleRef                     = registerCSS("label");
        iconLabelStyleRef                 = registerCSS("icon-label");
        iconLabelNoIconStyleRef           = registerCSS("icon-label-no-icon");
        dragLabelStyleRef                 = registerCSS("drag-label");
        staticTextStyleRef                = registerCSS("static-text");
        textStyleRef                      = registerCSS("text-style");

        // Image() default box. Built in code: the shader only samples textures for
        // texture-typed colour styles (StretchedTexture), which CSS can't express.
        {
            Style imageStyle{};
            imageStyle.borderWidths = interstice::BoxEdges(1);
            imageStyle.padding      = interstice::BoxEdges(0);
            imageStyle.margin       = interstice::BoxEdges(8);
            imageStyle.SetOverflow(interstice::Overflow::Hidden);
            for (int i = 0; i < Style::kStateCount; i++)
            {
                imageStyle.states[i].backgroundColor = ColorStyle::Create(
                    color4b(255, 255, 255, 255), color4b(255, 255, 255, 255), ColorType::StretchedTexture);
                imageStyle.states[i].SetBorderColor(ColorStyle::Create(color4b(85, 85, 85, 255)));
            }
            imageStyleRef = styleManager.Register(imageStyle);
        }

        groupLabelStyleRef                = registerCSS("group-label");
        groupDragLabelStyleRef            = registerCSS("group-drag-label");

        dashedStyleRef                    = registerCSS("dashed");

        buttonGroupStyleRef               = registerCSS("button-group");

        toolbarSideHorzStyleRef           = registerCSS("toolbar-side-horz");
        toolbarSideVertStyleRef           = registerCSS("toolbar-side-vert");
        toolbarSideHorzEmptyStyleRef      = registerCSS("toolbar-side-horz-empty");
        toolbarSideVertEmptyStyleRef      = registerCSS("toolbar-side-vert-empty");

        toolbarItemHorzStyleRef           = registerCSS("toolbar-item-horz");
        toolbarItemVertStyleRef           = registerCSS("toolbar-item-vert");    

        toolbarThumbHorzStyleRef          = registerCSS("toolbar-thumb-horz");
        toolbarThumbVertStyleRef          = registerCSS("toolbar-thumb-vert");

        toolbarFloatingWindowStyleRef     = registerCSS("toolbar-floating-window");
        viewContentStyleRef               = registerCSS("view-content");
        firstButtonStyleRef               = registerCSS("first-button");
        midButtonStyleRef                 = registerCSS("mid-button");
        lastButtonStyleRef                = registerCSS("last-button");
        toggleGroupStyleRef               = registerCSS("toggle-group");
        firstToggleStyleRef               = registerCSS("first-toggle");
        midToggleStyleRef                 = registerCSS("mid-toggle");
        lastToggleStyleRef                = registerCSS("last-toggle");
        panelDropVisualizationStyleRef    = registerCSS("panel-drop-visualization");
        ghostWindowStyleRef               = registerCSS("ghost-window");
        ghostIconStyleRef                 = registerCSS("ghost-icon");
        knobStyleRef                      = registerCSS("knob");

        sliderFieldTrackStyleRef          = registerCSS("slider-field-track");
        colorGradientSliderRowStyleRef    = registerCSS("color-gradient-slider-row");
        colorPickerTabRowStyleRef         = registerCSS("color-picker-tab-row");
        colorPickerPreviewsStyleRef       = registerCSS("color-picker-previews");
        gradientEditorTabRowStyleRef      = registerCSS("gradient-editor-tab-row");
        sliderFieldBgStyleRef             = registerCSS("slider-field-bg");
        sliderFieldFillStyleRef           = registerCSS("slider-field-fill");
        sliderFieldTextStyleRef           = registerCSS("slider-field-text");
        sliderStyleRef                    = registerCSS("slider");
        sliderGrooveStyleRef              = registerCSS("slider-groove");
        sliderKnobStyleRef                = registerCSS("slider-knob");
        scrollbarVerticalTrackStyleRef    = registerCSS("scrollbar-vertical");
        scrollbarVerticalThumbStyleRef    = registerCSS("scrollbar-vertical-thumb");
        scrollbarHorizontalTrackStyleRef  = registerCSS("scrollbar-horizontal");
        if (const LayoutStyle* p = styleManager.GetLayoutStyle(scrollbarVerticalTrackStyleRef.layoutStyleIndex); p && p->layoutData.width > 0)
            scrollbarThickness = p->layoutData.width;
        scrollbarHorizontalThumbStyleRef  = registerCSS("scrollbar-horizontal-thumb");
        colorSwatchStyleRef               = registerCSS("color-swatch");
        colorPickerStyleRef               = registerCSS("color-picker");
        colorPickerWheelStyleRef          = registerCSS("color-picker-wheel");
        colorPickerPreviewStyleRef        = registerCSS("color-picker-preview");
        colorPickerNewStyleRef            = registerCSS("color-picker-new");
        colorGradientSliderStyleRef       = registerCSS("color-gradient-slider");
        gradientSwatchStyleRef            = registerCSS("gradient-swatch");
        gradientEditorStyleRef            = registerCSS("gradient-editor");
        gradientEditorBarStyleRef         = registerCSS("gradient-editor-bar");
        itemListStyleRef                  = registerCSS("item-list");

        nodeViewStyleRef                  = registerCSS("node-view");
        viewBackgroundStyleRef            = registerCSS("view-background");
        nodeStyleRef                      = registerCSS("node");
        nodeHeaderStyleRef                = registerCSS("node-header");
        nodeCategoryStyleRef              = registerCSS("node-category");
        nodeTitleStyleRef                 = registerCSS("node-title");
        nodeCaptionStyleRef               = registerCSS("node-caption");
        nodeViewOverlayStyleRef           = registerCSS("node-view-overlay");
        nodeViewOverlayButtonStyleRef     = registerCSS("node-view-overlay-button");
        nodeViewMinimapStyleRef           = registerCSS("node-view-minimap");
        breadcrumbStyleRef                = registerCSS("breadcrumb");
        breadcrumbSegmentStyleRef         = registerCSS("breadcrumb-segment");
        breadcrumbCurrentStyleRef         = registerCSS("breadcrumb-current");
        breadcrumbSeparatorStyleRef       = registerCSS("breadcrumb-separator");
        axisLabelStyleRefs[0]             = registerCSS("axis-label-x");
        axisLabelStyleRefs[1]             = registerCSS("axis-label-y");
        axisLabelStyleRefs[2]             = registerCSS("axis-label-z");
        axisLabelStyleRefs[3]             = registerCSS("axis-label-w");
        vectorComponentStyleRef           = registerCSS("vector-component");
        statusBarStyleRef                 = registerCSS("status-bar");
        statusTextStyleRef                = registerCSS("status-text");
        commandSearchStyleRef             = registerCSS("command-search");
        commandSearchHintStyleRef         = registerCSS("command-search-hint");
        commandSearchKeyStyleRef          = registerCSS("command-search-key");
        commandSearchListStyleRef         = registerCSS("command-search-list");
        commandSearchCurrentRowStyleRef   = registerCSS("command-search-current-row");
        nodeRowStyleRef                   = registerCSS("node-row");
        nodeBodyCenterStyleRef            = registerCSS("node-body-center");
        nodeSocketStyleRef                = registerCSS("node-socket");
        if (const LayoutStyle* p = styleManager.GetLayoutStyle(nodeSocketStyleRef.layoutStyleIndex); p && p->layoutData.width > 0)
            socketSize = p->layoutData.width;

        // Text-only node properties: one CSS rule, registered per alignment (follows the sockets).
        {
            const Style* base = sheet.GetStyle("node-text");
            auto registerAligned = [&](TextAlign align) -> StyleRef
            {
                Style s = base ? *base : sheet.baseStyle;
                if (fontScale != 1.0f)
                    for (int i = 0; i < Style::kStateCount; i++)
                        s.states[i].textStyle.fontSize *= fontScale;
                s.textLayoutStyle.horizontalAlignment = align;
                return styleManager.Register(s);
            };
            nodeTextLeftStyleRef   = registerAligned(TextAlign::Start);
            nodeTextCenterStyleRef = registerAligned(TextAlign::Center);
            nodeTextRightStyleRef  = registerAligned(TextAlign::End);
        }
        itemStyleRef                      = registerCSS("item");
        collectionStyleRef                = registerCSS("collection");

        reorderableItemStyleRef           = registerCSS("reorderableItem");
        horzReorderableGroupStyleRef      = registerCSS("horzReorderableGroup");
        vertReorderableGroupStyleRef      = registerCSS("vertReorderableGroup");
        horzReorderableGroupWrapStyleRef  = registerCSS("horzReorderableGroupWrap");
        vertReorderableGroupWrapStyleRef  = registerCSS("vertReorderableGroupWrap");
        listAreaStyleRef                  = registerCSS("list-area");
        collectionAreaStyleRef            = registerCSS("collection-area");
        iconButtonStyleRef                = registerCSS("icon-button");
        iconButtonEndStyleRef             = registerCSS("icon-button-end");

        listViewStyleRef                  = registerCSS("list-view");
        listItemStyleRef                  = registerCSS("list-item");
        listItemRowStyleRef               = registerCSS("list-item-row");
        listItemDragKnobHorzStyleRef      = registerCSS("list-item-drag-knob-horz");
        listItemDragKnobVertStyleRef      = registerCSS("list-item-drag-knob-vert");
        listItemBodyStyleRef              = registerCSS("list-item-body");
        treeViewStyleRef                  = registerCSS("tree-view");
        gridViewStyleRef                  = registerCSS("grid-view");
        gridViewItemStyleRef              = registerCSS("grid-view-item");
        gridViewScrollStyleRef            = registerCSS("grid-view-scroll");
        gridViewRowStyleRef               = registerCSS("grid-view-row");
        gridViewListItemStyleRef          = registerCSS("grid-view-list-item");
        gridViewRenameItemStyleRef        = registerCSS("grid-view-rename-item");
        gridViewRenameListItemStyleRef    = registerCSS("grid-view-rename-list-item");
        gridViewRenameIconStyleRef        = registerCSS("grid-view-rename-icon");
        gridViewRenameEditStyleRef        = registerCSS("grid-view-rename-edit");
        flexFillRowStyleRef               = registerCSS("flex-fill-row");
        flexFillColStyleRef               = registerCSS("flex-fill-col");
        flatButtonStyleRef                = registerCSS("flat-button");
        searchTextboxStyleRef             = registerCSS("search-textbox");
        searchTextboxIconStyleRef         = registerCSS("search-textbox-icon");
        searchTextboxFieldStyleRef        = registerCSS("search-textbox-field");
                                          
        searchRowStyleRef                 = registerCSS("search-row");

        // Keep the whole parsed grid-view-item style (font-scale applied, same
        // as registerCSS) so callers can clone it with a custom iconSize.
        if (const Style* p = sheet.GetStyle("grid-view-item"))
        {
            gridViewItemBaseStyle = *p;
            if (fontScale != 1.0f)
                for (int i = 0; i < Style::kStateCount; i++)
                    gridViewItemBaseStyle.states[i].textStyle.fontSize *= fontScale;
        }
        tabBarStyleRef                    = registerCSS("tab-bar");
        tabStyleRef                       = registerCSS("tab");
        tabContentStyleRef                = registerCSS("tab-content");
        tabCloseStyleRef                  = registerCSS("tab-close");

        // Docking container styles
        dockContainerStyleRef             = registerCSS("dock-container");
        dockSplitHorzStyleRef             = registerCSS("dock-split-horz");
        dockSplitVertStyleRef             = registerCSS("dock-split-vert");
        dockSizerHorzStyleRef             = registerCSS("dock-sizer-horz");
        dockSizerVertStyleRef             = registerCSS("dock-sizer-vert");
        dockLeafStyleRef                  = registerCSS("dock-leaf");
        dockContentStyleRef               = registerCSS("dock-content");

        firstIconStyleRef                 = registerCSS("first-icon");
        iconLeftStyleRef                  = registerCSS("icon-left");
        iconRightStyleRef                 = registerCSS("icon-right");
        iconTopStyleRef                   = registerCSS("icon-top");
        iconBottomStyleRef                = registerCSS("icon-bottom");
        

        // -------------------------------------------------------------------------
        // Table styles (row × column data grid)
        // -------------------------------------------------------------------------
        tableStyleRef                     = registerCSS("table");
        tableFillStyleRef                 = registerCSS("table-fill");
        tableHeaderSectionStyleRef        = registerCSS("table-header-section");
        tableBodiesRowStyleRef            = registerCSS("table-bodies-row");
        tableScrollableBodyStyleRef       = registerCSS("table-scrollable-body");
        tableFitContentBodyStyleRef       = registerCSS("table-fit-content-body");
        tableRowStyleRef                  = registerCSS("table-row");
        tableRowAltStyleRef               = registerCSS("table-row-alt");
        tableHeaderStyleRef               = registerCSS("table-header");
        tableHeaderLockedStyleRef         = registerCSS("table-header-locked");
        tableSizerStyleRef                = registerCSS("table-sizer");
        tableSeparatorStyleRef            = registerCSS("table-separator");
        tableCellStyleRef                 = registerCSS("table-cell");
        tableCellCompactStyleRef          = registerCSS("table-cell-compact");
        treeTableCellStyleRef             = registerCSS("tree-table-cell");
        treeTableCellRowModeStyleRef      = registerCSS("tree-table-cell-row-mode");
        treeTableArrowGlyphStyleRef       = registerCSS("tree-table-arrow-glyph");
        treeTableIndentStyleRef           = registerCSS("tree-table-indent");
        treeTableArrowStyleRef            = registerCSS("tree-table-arrow");
        treeTableIconStyleRef             = registerCSS("tree-table-icon");
        treeTableTextStyleRef             = registerCSS("tree-table-text");
        treeTableTextEditStyleRef         = registerCSS("tree-table-text-edit");

        // -------------------------------------------------------------------------
        // Window and menu styles from CSS
        // -------------------------------------------------------------------------
        windowStyleRef                    = registerCSS("window");
        mainWindowStyleRef                = registerCSS("main-window");
        dockWindowStyleRef                = registerCSS("dock-window");
        menuStyleRef                      = registerCSS("menu");
        menuItemStyleRef                  = registerCSS("menu-item");
        menuScrollButtonStyleRef          = registerCSS("menu-scroll-button");
        menuBarStyleRef                   = registerCSS("menu-bar");
        menuBarTriggerStyleRef            = registerCSS("menu-bar-trigger");
        titleBarStyleRef                  = registerCSS("title-bar");
        titleBarButtonStyleRef            = registerCSS("title-bar-button");
        titleBarCloseButtonStyleRef       = registerCSS("title-bar-close-button");
        menuSeparatorStyleRef             = registerCSS("menu-separator");
        menuItemRowStyleRef               = registerCSS("menu-item-row");
        menuItemIconStyleRef              = registerCSS("menu-item-icon");
        menuItemLabelStyleRef             = registerCSS("menu-item-label");
        menuItemShortcutStyleRef          = registerCSS("menu-item-shortcut");
        menuItemArrowStyleRef             = registerCSS("menu-item-arrow");
        tooltipStyleRef                   = registerCSS("tooltip");
        tooltipTextStyleRef               = registerCSS("tooltip-text");
        settingsPageSelectionRef          = registerCSS("settings-page-selection");
        settingsPageStyleRef              = registerCSS("settings-page");

        return true;
    }

    void GUITheme::ComputeRowMetrics(StyleManager& styleManager, ITextSystem& textSystem)
    {
        // Measure "Mg" (ascender + descender) in both cell styles; the max gives mixed tables
        // a uniform row height. Must be real metrics: VirtualTable's spacer math uses it
        // verbatim, and an estimate makes the max scroll oscillate.
        auto measure = [&](StyleRef style) -> int32_t {
            const TextID probeID = textSystem.RegisterText(
                ID::Invalid(), "Mg", style, ElementState::None);
            const auto m = textSystem.GetTextMeasurement(probeID);
            const LayoutStyle* ls = styleManager.GetLayoutStyle(style.layoutStyleIndex);
            int32_t h = m.minHeight;
            if (ls) h += ls->padding.Vertical() + ls->borderWidths.Vertical();
            return std::max<int32_t>(1, h);
        };
        tableRowHeight = std::max(measure(tableCellStyleRef),
                                  measure(treeTableCellStyleRef));

        // Tree-table cell metrics: the stylesheet owns them; a missing value falls back to the
        // text line height so it still follows the font.
        {
            const TextID probeID = textSystem.RegisterText(
                ID::Invalid(), "Mg", treeTableTextStyleRef, ElementState::None);
            const int32_t lineHeight = std::max<int32_t>(1, textSystem.GetTextMeasurement(probeID).minHeight);
            auto cssWidth = [&](StyleRef s) -> int32_t {
                const LayoutStyle* ls = styleManager.GetLayoutStyle(s.layoutStyleIndex);
                return (ls && ls->layoutData.width != LayoutData::kAutoSize) ? ls->layoutData.width : lineHeight;
            };
            auto cssIconSize = [&](StyleRef s, int32_t fallback) -> int32_t {
                const LayoutStyle* ls = styleManager.GetLayoutStyle(s.layoutStyleIndex);
                return (ls && ls->iconSize > 0) ? static_cast<int32_t>(ls->iconSize) : fallback;
            };
            treeTableIndent         = cssWidth(treeTableIndentStyleRef);
            treeTableExpanderWidth  = cssWidth(treeTableArrowStyleRef);
            treeTableIconSize       = cssIconSize(treeTableIconStyleRef, lineHeight);
            treeTableArrowGlyphSize = cssIconSize(treeTableArrowGlyphStyleRef, treeTableIconSize);
        }

        // Same for the files grid (icon-on-top label line, icon-left list row), so names never clip.
        auto measureText = [&](StyleRef style) -> int32_t {
            const TextID probeID = textSystem.RegisterText(
                ID::Invalid(), "Mg", style, ElementState::None);
            return std::max<int32_t>(1, textSystem.GetTextMeasurement(probeID).minHeight);
        };
        gridViewItemLineHeight = measureText(gridViewItemStyleRef);
        {
            const int32_t      listText = measureText(gridViewListItemStyleRef);
            const LayoutStyle* ls       = styleManager.GetLayoutStyle(gridViewListItemStyleRef.layoutStyleIndex);
            const int32_t      pad       = ls ? (ls->padding.Vertical() + ls->borderWidths.Vertical()) : 0;
            const int32_t      iconSize  = ls ? static_cast<int32_t>(ls->iconSize) : 0;
            gridViewListRowHeight = std::max(listText, iconSize) + pad;
        }

        // The node view's single grid pitch (node/wire snapping and the drawn grid): half a
        // property row, so row edges and centers (sockets) land on grid lines. The row lays
        // the field out at its margin box, so the margin is included.
        float fieldHeight = (float)measure(regularControls.textbox);
        if (const LayoutStyle* tb = styleManager.GetLayoutStyle(regularControls.textbox.layoutStyleIndex))
            fieldHeight += tb->margin.Vertical();
        fieldHeight /= 2.0f;   // one property row = two grid cells
        nodeGridSize = (int)ceil(fieldHeight);
    }
} // namespace interstice
