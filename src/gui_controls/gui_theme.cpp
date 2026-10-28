#include <gui_core/gui_base_types.h>
#include <gui_core/gui_resources.h>
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

namespace tachyon
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
            invalidStyle.textLayoutStyle = tachyon::TextLayoutStyle
            {
                .textDirection = tachyon::TextDirection::Auto,
                .textWrapping = tachyon::TextWrap::WordChar,
                .horizontalAlignment = tachyon::TextAlign::Start,
                .baseline = tachyon::TextBaseline::Middle
            };
            invalidStyle.borderWidths = noBorder;
            invalidStyle.borderRadius = noRadius;
            invalidStyle.margin = BoxEdges(2);
            invalidStyle.SetOverflow(tachyon::Overflow::Hidden);
            invalidStyle.padding = tachyon::BoxEdges(10);
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
            newStyle.SetOverflow(tachyon::Overflow::Visible);
            newStyle.display = tachyon::Display::Flex;
            newStyle.layoutData.flex.direction = tachyon::FlexDirection::Row;
            newStyle.layoutData.flex.wrap = tachyon::FlexWrap::Wrap;
            newStyle.layoutData.flex.alignItems = tachyon::AlignItems::Stretch;
            newStyle.layoutData.flex.justifyContent = tachyon::JustifyContent::FlexStart;
            newStyle.layoutData.flex.flexGrow = 1;
            newStyle.layoutData.flex.gap = 0;
            newStyle.padding = tachyon::BoxEdges(0);
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
            // Named "tachyon/...", so an app's own "info" or "error" doesn't get the library's.
            auto registerIcon = [&](const char* name, const char* shapes) {
                const std::string svg = std::string(R"(<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg" fill="none" stroke="#909090" )"
                                                    R"(stroke-width="2" stroke-linecap="round" stroke-linejoin="round">)") + shapes + "</svg>";
                const std::string fullName = std::string("tachyon/") + name;
                return textSystem->RegisterIconFromSVG(fullName.c_str(), svg.data(), svg.size());
            };
            treeArrowRight  = registerIcon("tree-arrow-right", R"(<path d="m9 18 6-6-6-6"/>)");
            treeArrowDown   = registerIcon("tree-arrow-down",  R"(<path d="m6 9 6 6 6-6"/>)");
            sortArrowUpIcon   = registerIcon("sort-arrow-up",   R"(<path d="m18 15-6-6-6 6"/>)");
            sortArrowDownIcon = registerIcon("sort-arrow-down", R"(<path d="m6 9 6 6 6-6"/>)");
            tabCloseIcon    = registerIcon("tab-close",  R"(<path d="M18 6 6 18"/><path d="m6 6 12 12"/>)");
            menuCheckIcon   = registerIcon("menu-check", R"(<path d="M20 6 9 17l-5-5"/>)");
            dropdownArrowIcon = registerIcon("dropdown-arrow", R"(<path d="m6 9 6 6 6-6"/>)");
            dropdownArrowUpIcon = registerIcon("dropdown-arrow-up", R"(<path d="m18 15-6-6-6 6"/>)");
            chevronLeftIcon   = registerIcon("chevron-left", R"(<path d="m15 18-6-6 6-6"/>)");
            duplicateIcon     = registerIcon("duplicate", R"(<rect x="8" y="8" width="13" height="13" rx="2"/><path d="M4 16V6a2 2 0 0 1 2-2h10"/>)");
            deleteIcon        = registerIcon("delete", R"(<path d="M3 6h18"/><path d="M8 6V4h8v2"/><path d="m6 6 1 14h10l1-14"/>)");
            reverseIcon       = registerIcon("reverse", R"(<path d="M4 8h15"/><path d="m16 5 3 3-3 3"/><path d="M20 16H5"/><path d="m8 13-3 3 3 3"/>)");
            distributeIcon    = registerIcon("distribute", R"(<path d="M4 4v16"/><path d="M20 4v16"/><rect x="9" y="7" width="6" height="10" rx="1"/>)");
            addIcon         = registerIcon("list-add",    R"(<path d="M5 12h14"/><path d="M12 5v14"/>)");
            removeIcon      = registerIcon("list-remove", R"(<path d="M5 12h14"/>)");
            alertIcon       = registerIcon("alert", R"(<path d="m21.7 18-8-14a2 2 0 0 0-3.4 0l-8 14A2 2 0 0 0 4 21h16a2 2 0 0 0 1.7-3ZM12 9v4M12 17h.01"/>)");
            powerIcon       = registerIcon("power", R"(<path d="M12 2v10M18.4 6.6a9 9 0 1 1-12.8 0"/>)");
            questionIcon    = registerIcon("question", R"(<path d="M12 3a9 9 0 1 0 0 18 9 9 0 0 0 0-18ZM9.1 9a3 3 0 0 1 5.8 1c0 2-3 3-3 3M12 17h.01"/>)");
            infoIcon        = registerIcon("info", R"(<path d="M12 3a9 9 0 1 0 0 18 9 9 0 0 0 0-18ZM12 16v-4M12 8h.01"/>)");
            errorIcon       = registerIcon("error", R"(<path d="M12 3a9 9 0 1 0 0 18 9 9 0 0 0 0-18ZM15 9l-6 6M9 9l6 6"/>)");
            chevronRightIcon = registerIcon("chevron-right", R"(<path d="m9 18 6-6-6-6"/>)");
            eyedropperIcon  = registerIcon("eyedropper", R"(<path d="m2 22 1-1h3l9-9M3 21v-3l9-9M15 6l3.4-3.4a2.1 2.1 0 1 1 3 3L18 9l.4.4a2.1 2.1 0 1 1-3 3l-3.8-3.8a2.1 2.1 0 1 1 3-3Z"/>)");
            dockIcon        = registerIcon("dock", R"(<path d="M3 5h18v14H3ZM15 5v14M8 12h4M10 10l2 2-2 2"/>)");
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
            auto makeEmptyFlexStyle = [&](tachyon::FlexDirection dir, tachyon::FlexWrap wrap) -> StyleRef
            {
                Style newStyle{};
                newStyle.textLayoutStyle = {};
                newStyle.borderWidths = noBorder;
                newStyle.borderRadius = noRadius;
                newStyle.SetOverflow(tachyon::Overflow::Auto);
                newStyle.display = tachyon::Display::Flex;
                newStyle.layoutData.flex.direction = dir;
                newStyle.layoutData.flex.wrap = wrap;
                newStyle.layoutData.flex.alignItems = tachyon::AlignItems::Stretch;
                newStyle.layoutData.flex.justifyContent = tachyon::JustifyContent::FlexStart;
                newStyle.padding = tachyon::BoxEdges(0);
                for (int i = 0; i < Style::kStateCount; i++)
                {
                    newStyle.states[i].SetBorderColor({});
                    newStyle.states[i].backgroundColor = {};
                }
                return styleManager.Register(newStyle);
            };
            emptyHorzStyleRef     = makeEmptyFlexStyle(tachyon::FlexDirection::Row, tachyon::FlexWrap::NoWrap);
            emptyHorzWrapStyleRef = makeEmptyFlexStyle(tachyon::FlexDirection::Row, tachyon::FlexWrap::Wrap);
            emptyVertStyleRef     = makeEmptyFlexStyle(tachyon::FlexDirection::Column, tachyon::FlexWrap::NoWrap);
            emptyVertWrapStyleRef = makeEmptyFlexStyle(tachyon::FlexDirection::Column, tachyon::FlexWrap::Wrap);
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
            // A font file that isn't on disk comes from the library's built-in copy.
            std::vector<FontFaceSource> sources;
            for (const std::string& path : paths)
            {
                std::error_code ec;
                const bool onDisk = std::filesystem::is_regular_file(path, ec);
                sources.push_back(FontFaceSource{ .file = path, .data = onDisk ? std::span<const std::byte>{} : FindBuiltInResource(path) });
            }
            if (textSystem.RegisterFontFamily(first.family, sources) == FontID::Invalid)
                TACHYON_LOG_ERROR(kLogTheme, "could not load font family '{}'", first.family);
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
        if (it != m_classStyles.end())
            return it->second;
        if (m_missingWarned.emplace(cssClass).second)
            TACHYON_LOG_WARNING(kLogTheme, "no style file defines the class .{}", cssClass);
        return StyleRef{};
    }

    Result<StyleRef> GUITheme::FindStyle(std::string_view cssClass) const
    {
        const auto it = m_classStyles.find(cssClass);
        if (it == m_classStyles.end())
            return Error{ ErrorCode::NotFound, "no style file defines the class ." + std::string(cssClass) };
        return it->second;
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
        // Each file is read from disk, or else from the library's built-in copy.
        Result<std::string> palette = ReadResource(themePath);
        if (!palette)
        {
            TACHYON_LOG_ERROR(kLogTheme, "theme file: {}", palette.GetError().message);
            return false;
        }

        // Palette first, then each style file, so var() references resolve at parse time.
        // Each part's first line in the concatenated text maps errors back to its file.
        const std::filesystem::path themeDir = std::filesystem::path(themePath).parent_path();
        struct Part { std::string name; int firstLine; };
        std::vector<Part> parts;
        std::ostringstream ss;
        int line = 1;
        auto append = [&](std::string name, std::string_view text)
        {
            parts.push_back({ std::move(name), line });
            ss << text << '\n';
            line += int(std::count(text.begin(), text.end(), '\n')) + 1;
        };
        append(themePath, *palette);
        for (const std::string& styleFile : styleFiles)
        {
            const std::filesystem::path path = std::filesystem::path(styleFile).is_absolute()
                ? std::filesystem::path(styleFile) : themeDir / styleFile;
            Result<std::string> text = ReadResource(path.string());
            if (!text)
            {
                TACHYON_LOG_ERROR(kLogTheme, "theme styles file: {}", text.GetError().message);
                return false;
            }
            append(path.string(), *text);
        }
        StyleSheet sheet = ParseStyleSheet(ss.view());

        for (const auto& err : sheet.errors)
        {
            const Part* part = &parts.front();
            for (const Part& p : parts)
                if (err.line >= p.firstLine) part = &p;
            TACHYON_LOG_ERROR(kLogTheme, "{}:{}:{}: {}", part->name, err.line - part->firstLine + 1, err.column, err.message);
        }

        if (textSystem)
            RegisterFontFaces(*textSystem, sheet.fontFaces, themeDir);
        TACHYON_LOG_INFO(kLogTheme, "loaded {}: {} styles, {} errors", themePath, sheet.styles.size(), sheet.errors.size());

        m_paletteColors.clear();
        for (const auto& [name, prop] : sheet.customProperties)
            if (prop.hasColor)
                m_paletteColors[name] = { prop.color.r / 255.0f, prop.color.g / 255.0f, prop.color.b / 255.0f, prop.color.a / 255.0f };

        // Line color styles from CSS custom properties (with fallbacks)
        auto lineColorFromCSS = [&](const char* varName, color4f fallback) -> tachyon::VisualColorStyleIndex
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
        dividerHoverLineStyle = lineColorFromCSS("--divider-hover", color4f{ 0.42f, 0.61f, 1.0f, 1.0f });
        if (textSystem)
            textSystem->SetCaretColor(lineColorFromCSS("--caret", color4f{ 0.42f, 0.61f, 1.0f, 1.0f }));
        nanTextColor          = lineColorFromCSS("--warn-text", color4f{ 0.9f, 0.7f, 0.33f, 1.0f });
        if (const auto* prop = sheet.GetCustomProperty("--scrim"); prop && prop->hasColor) scrim = prop->color;
        else                                                                                  scrim = color4b{ 0, 0, 0, 140 };
        checkerAColor         = lineColorFromCSS("--checker-a", color4f{ 0.16f, 0.18f, 0.22f, 1.0f });
        checkerBColor         = lineColorFromCSS("--checker-b", color4f{ 0.12f, 0.13f, 0.16f, 1.0f });
        alphaTrackColor       = lineColorFromCSS("--alpha-track", color4f{ 0.04f, 0.05f, 0.06f, 1.0f });
        alphaFillColor        = lineColorFromCSS("--alpha-fill", color4f{ 0.91f, 0.92f, 0.94f, 1.0f });
        accentColor           = lineColorFromCSS("--accent", color4f{ 0.42f, 0.61f, 1.0f, 1.0f });
        panelColor            = lineColorFromCSS("--bg-panel", color4f{ 0.07f, 0.08f, 0.09f, 1.0f });
        treeDropIndicatorStyle = lineColorFromCSS("--accent", color4f{ 0.0f, 0.478f, 0.8f, 1.0f });

        marqueeFillColorStyle = styleManager.RegisterColorStyle(
            ColorStyle::Create(color4f{ 0.3f, 0.6f, 1.0f, 0.15f }));

        // Registers a parsed style by name, with the global font scale applied. A class with
        // :first-child / :last-child rules is registered once, with its child styles.
        auto scaled = [&](Style s)
            {
                if (fontScale != 1.0f)
                {
                    for (int i = 0; i < Style::kStateCount; i++)
                        s.states[i].textStyle.fontSize *= fontScale;
                }
                return s;
            };
        auto registerCSS = [&](std::string_view name) -> StyleRef
            {
                const std::string key(name);
                if (const auto known = m_classStyles.find(key); known != m_classStyles.end())
                    return known->second;
                const Style* p = sheet.GetStyle(name);
                const Style s = scaled(p ? *p : sheet.baseStyle);
                if (const auto child = sheet.childStyles.find(key); child != sheet.childStyles.end())
                    return styleManager.RegisterWithChildStyles(s, scaled(child->second.first), scaled(child->second.last),
                                                               scaled(child->second.only));
                return styleManager.Register(s);
            };

        m_classStyles.clear();
        for (const auto& [name, style] : sheet.styles)
        {
            const StyleRef ref = registerCSS(name);
            m_classStyles.emplace(name, ref);
        }

        // -------------------------------------------------------------------------
        // Named widget styles from CSS
        // -------------------------------------------------------------------------
        regularControls.button       = registerCSS("button");
        regularControls.toggle       = registerCSS("toggle");
        regularControls.textbox      = registerCSS("textbox");
        regularControls.number       = registerCSS("number-field");
        regularControls.menuTrigger  = registerCSS("menu-trigger");

        toolbarControls.button       = registerCSS("toolbar-button");
        toolbarControls.toggle       = registerCSS("toolbar-toggle");
        toolbarControls.textbox      = registerCSS("toolbar-textbox");
        toolbarControls.number       = registerCSS("toolbar-number-field");
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
            imageStyle.borderWidths = tachyon::BoxEdges(1);
            imageStyle.padding      = tachyon::BoxEdges(0);
            imageStyle.margin       = tachyon::BoxEdges(8);
            imageStyle.SetOverflow(tachyon::Overflow::Hidden);
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
        groupButtonStyleRef               = registerCSS("group-button");
        toggleGroupStyleRef               = registerCSS("toggle-group");
        groupToggleStyleRef               = registerCSS("group-toggle");
        panelDropVisualizationStyleRef    = registerCSS("panel-drop-visualization");
        ghostWindowStyleRef               = registerCSS("ghost-window");
        ghostIconStyleRef                 = registerCSS("ghost-icon");
        knobStyleRef                      = registerCSS("knob");

        sliderFieldTrackStyleRef          = registerCSS("slider-field-track");
        colorGradientSliderRowStyleRef    = registerCSS("color-gradient-slider-row");
        sliderFieldBgStyleRef             = registerCSS("slider-field-bg");
        sliderFieldFillStyleRef           = registerCSS("slider-field-fill");
        sliderFieldTextStyleRef           = registerCSS("slider-field-text");
        sliderFieldValueStyleRef          = registerCSS("slider-field-value");
        sliderFieldEdgeStyleRef           = registerCSS("slider-field-edge");
        sliderStyleRef                    = registerCSS("slider");
        sliderGrooveStyleRef              = registerCSS("slider-groove");
        sliderFillStyleRef                = registerCSS("slider-fill");
        sliderKnobStyleRef                = registerCSS("slider-knob");
        switchStyleRef                    = registerCSS("switch");
        switchTrackStyleRef               = registerCSS("switch-track");
        switchKnobStyleRef                = registerCSS("switch-knob");
        switchLabelStyleRef               = registerCSS("switch-label");
        scrollbarVerticalTrackStyleRef    = registerCSS("scrollbar-vertical");
        scrollbarVerticalThumbStyleRef    = registerCSS("scrollbar-vertical-thumb");
        scrollbarHorizontalTrackStyleRef  = registerCSS("scrollbar-horizontal");
        if (const LayoutStyle* p = styleManager.GetLayoutStyle(scrollbarVerticalTrackStyleRef.layoutStyleIndex); p && p->layoutData.width > 0)
            scrollbarThickness = p->layoutData.width;
        scrollbarHorizontalThumbStyleRef  = registerCSS("scrollbar-horizontal-thumb");
        colorSwatchStyleRef               = registerCSS("color-swatch");
        colorFieldStyleRef                = registerCSS("color-field");
        colorHexStyleRef                  = registerCSS("color-hex");
        colorAlphaStyleRef                = registerCSS("color-alpha");
        colorAlphaInputStyleRef           = registerCSS("color-alpha-input");
        colorAlphaUnitStyleRef            = registerCSS("color-alpha-unit");
        colorAlphaCaptionStyleRef         = registerCSS("color-alpha-caption");
        colorPickerStyleRef               = registerCSS("color-picker");
        dialogHeaderStyleRef              = registerCSS("dialog-header");
        dialogTitleStyleRef               = registerCSS("dialog-title");
        dialogContextStyleRef             = registerCSS("dialog-context");
        colorPickerHdrValueStyleRef       = registerCSS("color-picker-hdr-value");
        dialogCloseStyleRef               = registerCSS("dialog-close");
        dialogFooterStyleRef              = registerCSS("dialog-footer");
        dialogStatusStyleRef              = registerCSS("dialog-status");
        dialogStatusDotStyleRef           = registerCSS("dialog-status-dot");
        dialogButtonStyleRef              = registerCSS("dialog-button");
        dialogPrimaryButtonStyleRef       = registerCSS("dialog-button-primary");
        dialogDangerButtonStyleRef        = registerCSS("dialog-button-danger");
        messageBoxStyleRef                = registerCSS("message-box");
        messageBoxCloseStyleRef           = registerCSS("message-box-close");
        messageBoxBodyStyleRef            = registerCSS("message-box-body");
        messageBoxWarningStyleRef         = registerCSS("message-box-warning");
        messageBoxQuestionStyleRef        = registerCSS("message-box-question");
        messageBoxErrorStyleRef           = registerCSS("message-box-error");
        messageBoxTextStyleRef            = registerCSS("message-box-text");
        messageBoxTitleStyleRef           = registerCSS("message-box-title");
        messageBoxMessageStyleRef         = registerCSS("message-box-message");
        messageBoxWellStyleRef            = registerCSS("message-box-well");
        messageBoxItemStyleRef            = registerCSS("message-box-item");
        messageBoxItemDotStyleRef         = registerCSS("message-box-item-dot");
        messageBoxItemNameStyleRef        = registerCSS("message-box-item-name");
        messageBoxItemMetaStyleRef        = registerCSS("message-box-item-meta");
        messageBoxDetailsToggleStyleRef   = registerCSS("message-box-details-toggle");
        messageBoxDetailsStyleRef         = registerCSS("message-box-details");
        checkboxStyleRef                  = registerCSS("checkbox");
        checkboxBoxStyleRef               = registerCSS("checkbox-box");
        checkboxLabelStyleRef             = registerCSS("checkbox-label");
        colorPickerBodyStyleRef           = registerCSS("color-picker-body");
        colorPickerPlanesStyleRef         = registerCSS("color-picker-planes");
        colorPickerSquareStyleRef         = registerCSS("color-picker-square");
        colorPickerStripStyleRef          = registerCSS("color-picker-strip");
        colorPickerControlsStyleRef       = registerCSS("color-picker-controls");
        colorPickerPreviewGroupStyleRef   = registerCSS("color-picker-preview-group");
        colorPickerCaptionsStyleRef       = registerCSS("color-picker-captions");
        colorPickerCaptionStyleRef        = registerCSS("color-picker-caption");
        colorPickerModeRowStyleRef        = registerCSS("color-picker-mode-row");
        colorPickerRangeStyleRef          = registerCSS("color-picker-range");
        colorPickerChannelsStyleRef       = registerCSS("color-picker-channels");
        colorPickerChannelStyleRef        = registerCSS("color-picker-channel");
        colorPickerChannelLabelStyleRef   = registerCSS("color-picker-channel-label");
        colorPickerTrackStyleRef          = registerCSS("color-picker-track");
        colorPickerChannelValueStyleRef   = registerCSS("color-picker-channel-value");
        colorPickerFieldRowStyleRef       = registerCSS("color-picker-field-row");
        colorPickerRowLabelStyleRef       = registerCSS("color-picker-row-label");
        colorPickerHdrStyleRef            = registerCSS("color-picker-hdr");
        colorPickerSwatchesStyleRef       = registerCSS("color-picker-swatches");
        colorPickerSwatchRowStyleRef      = registerCSS("color-picker-swatch-row");
        colorPickerSectionLabelStyleRef   = registerCSS("color-picker-section-label");
        colorPickerSwatchStyleRef         = registerCSS("color-picker-swatch");
        colorPickerRecentStyleRef         = registerCSS("color-picker-recent");
        colorPickerPreviewStyleRef        = registerCSS("color-picker-preview");
        colorPickerNewStyleRef            = registerCSS("color-picker-new");
        colorGradientSliderStyleRef       = registerCSS("color-gradient-slider");
        gradientSwatchStyleRef            = registerCSS("gradient-swatch");
        gradientEditorStyleRef            = registerCSS("gradient-editor");
        gradientEditorBarStyleRef         = registerCSS("gradient-editor-bar");
        dialogSmallButtonStyleRef             = registerCSS("dialog-small-button");
        dialogIconButtonStyleRef              = registerCSS("dialog-icon-button");
        dialogSquareButtonStyleRef            = registerCSS("dialog-square-button");
        dialogDeleteButtonStyleRef            = registerCSS("dialog-delete-button");
        gradientEditorPresetsStyleRef         = registerCSS("gradient-editor-presets");
        gradientEditorSectionLabelStyleRef    = registerCSS("gradient-editor-section-label");
        gradientEditorPresetStyleRef          = registerCSS("gradient-editor-preset");
        gradientEditorBadgeStyleRef           = registerCSS("gradient-editor-badge");
        gradientEditorColumnsStyleRef         = registerCSS("gradient-editor-columns");
        gradientEditorColumnStyleRef          = registerCSS("gradient-editor-column");
        gradientEditorRowStyleRef             = registerCSS("gradient-editor-row");
        gradientEditorHeadingStyleRef         = registerCSS("gradient-editor-heading");
        gradientEditorCountStyleRef           = registerCSS("gradient-editor-count");
        gradientEditorLabelStyleRef           = registerCSS("gradient-editor-label");
        gradientEditorLocationStyleRef        = registerCSS("gradient-editor-location");
        gradientEditorLocationInputStyleRef   = registerCSS("gradient-editor-location-input");
        gradientEditorLocationUnitStyleRef    = registerCSS("gradient-editor-location-unit");
        gradientEditorDividerStyleRef         = registerCSS("gradient-editor-divider");
        gradientEditorActionsStyleRef         = registerCSS("gradient-editor-actions");
        gradientEditorTableStyleRef           = registerCSS("gradient-editor-table");
        gradientEditorTableHeadStyleRef       = registerCSS("gradient-editor-table-head");
        gradientEditorTableRowStyleRef        = registerCSS("gradient-editor-table-row");
        gradientEditorTypeCellStyleRef        = registerCSS("gradient-editor-type-cell");
        gradientEditorSwatchCellStyleRef      = registerCSS("gradient-editor-swatch-cell");
        gradientEditorHeadCellStyleRef        = registerCSS("gradient-editor-head-cell");
        gradientEditorHeadLocationStyleRef    = registerCSS("gradient-editor-head-location");
        gradientEditorValueCellStyleRef       = registerCSS("gradient-editor-value-cell");
        gradientEditorLocationCellStyleRef    = registerCSS("gradient-editor-location-cell");
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
        typedFieldStyleRef                = registerCSS("typed-field");
        typedFieldInputStyleRef           = registerCSS("typed-field-input");
        typedFieldTypeStyleRef            = registerCSS("typed-field-type");
        collectionListStyleRef            = registerCSS("collection-list");
        collectionRowStyleRef             = registerCSS("collection-row");
        collectionIndexStyleRef           = registerCSS("collection-index");
        collectionRemoveStyleRef          = registerCSS("collection-remove");
        collectionAddStyleRef             = registerCSS("collection-add");
        collectionAddEntryStyleRef        = registerCSS("collection-add-entry");
        collectionEntryStyleRef           = registerCSS("collection-entry");
        collectionValueStyleRef           = registerCSS("collection-value");
        collectionHeaderStyleRef          = registerCSS("collection-header");
        collectionHeaderCellStyleRef      = registerCSS("collection-header-cell");
        collectionHeaderCaptionStyleRef   = registerCSS("collection-header-caption");
        collectionHeaderTypeStyleRef      = registerCSS("collection-header-type");
        groupLabelTopStyleRef             = registerCSS("group-label-top");
        groupLabelTitleStyleRef           = registerCSS("group-label-title");
        groupLabelCaptionStyleRef         = registerCSS("group-label-caption");
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
        tabOverflowStyleRef               = registerCSS("tab-overflow");
        tabContentStyleRef                = registerCSS("tab-content");
        tabCloseStyleRef                  = registerCSS("tab-close");

        // Docking container styles
        dockContainerStyleRef             = registerCSS("dock-container");
        dockContainerMainStyleRef         = registerCSS("dock-container-main");
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
        // Table styles (row x column data grid)
        // -------------------------------------------------------------------------
        tableStyleRef                     = registerCSS("table");
        tableFillStyleRef                 = registerCSS("table-fill");
        tableHeaderSectionStyleRef        = registerCSS("table-header-section");
        tableBodiesRowStyleRef            = registerCSS("table-bodies-row");
        tableScrollableBodyStyleRef       = registerCSS("table-scrollable-body");
        tableFitContentBodyStyleRef       = registerCSS("table-fit-content-body");
        tableRowStyleRef                  = registerCSS("table-row");
        tableRowAltStyleRef               = registerCSS("table-row-alt");
        tableRowBarStyleRef               = registerCSS("table-row-bar");
        listRowStyleRef                   = registerCSS("list-row");
        tableHeaderStyleRef               = registerCSS("table-header");
        tableHeaderLockedStyleRef         = registerCSS("table-header-locked");
        tableSizerStyleRef                = registerCSS("table-sizer");
        tableSeparatorStyleRef            = registerCSS("table-separator");
        tableCellStyleRef                 = registerCSS("table-cell");
        tableCellNumberStyleRef           = registerCSS("table-cell-number");
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
        dockWindowLeafStyleRef            = registerCSS("dock-window-leaf");
        dockWindowPanelStyleRef           = registerCSS("dock-window-panel");
        dockWindowBodyStyleRef            = registerCSS("dock-window-body");
        titleBarTabBarStyleRef            = registerCSS("title-bar-tab-bar");
        titleBarTabStyleRef               = registerCSS("title-bar-tab");
        titleBarGripStyleRef              = registerCSS("title-bar-grip");
        titleBarTitleStyleRef             = registerCSS("title-bar-title");
        titleBarTextButtonStyleRef        = registerCSS("title-bar-text-button");
        menuStyleRef                      = registerCSS("menu");
        menuItemStyleRef                  = registerCSS("menu-item");
        menuScrollButtonStyleRef          = registerCSS("menu-scroll-button");
        menuBarStyleRef                   = registerCSS("menu-bar");
        menuBarTriggerStyleRef            = registerCSS("menu-bar-trigger");
        titleBarStyleRef                  = registerCSS("title-bar");
        titleBarButtonStyleRef            = registerCSS("title-bar-button");
        titleBarCloseButtonStyleRef       = registerCSS("title-bar-close-button");
        menuSeparatorStyleRef             = registerCSS("menu-separator");
        menuHeaderStyleRef                = registerCSS("menu-header");
        reorderDropSlotStyleRef           = registerCSS("reorder-drop-slot");
        reorderLiftedStyleRef             = registerCSS("reorder-lifted");
        menuItemDestructiveLabelStyleRef  = registerCSS("menu-item-label-destructive");
        menuItemDestructiveIconStyleRef   = registerCSS("menu-item-icon-destructive");
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

        // Vector component labels share one width, the widest name with its padding and border.
        axisLabelWidth = 1;
        for (const char* name : { "X", "Y", "Z", "W", "R", "G", "B", "A" })
            for (const StyleRef style : axisLabelStyleRefs)
            {
                const TextID probeID = textSystem.RegisterText(ID::Invalid(), name, style, ElementState::None);
                int32_t w = textSystem.GetTextMeasurement(probeID).maxWidth;
                if (const LayoutStyle* ls = styleManager.GetLayoutStyle(style.layoutStyleIndex))
                    w += ls->padding.Horizontal() + ls->borderWidths.Horizontal();
                axisLabelWidth = std::max(axisLabelWidth, w);
            }

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
} // namespace tachyon
