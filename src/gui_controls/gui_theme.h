#pragma once

#include <gui_core/style/gui_style_ref.h>
#include <gui_core/style/gui_style.h>

#include <map>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

namespace interstice
{
    class StyleManager;
    class ITextSystem;

    // Default control styles. GUITheme::controls points at the active bundle;
    // BeginToolbar/EndToolbar swap in the toolbar variants.
    struct ControlStyles
    {
        StyleRef button;
        StyleRef toggle;
        StyleRef textbox;
        StyleRef menuTrigger;
    };

    class GUITheme
    {
    public:
        ControlStyles regularControls {};
        ControlStyles toolbarControls {};
        const ControlStyles* controls = &regularControls;

        StyleRef emptyHorzStyleRef;     // horizontal flex, no wrap — used for grouping elements w/o drawing anything
        StyleRef emptyHorzWrapStyleRef; // horizontal flex, wrap
        StyleRef emptyVertStyleRef;     // vertical flex, no wrap
        StyleRef emptyVertWrapStyleRef; // vertical flex, wrap
        StyleRef fieldElementGroupStyleRef;

        // View panel toolbars: side bins (one reorderable group per side) plus
        // the per-toolbar item container and the central content placeholder.
        StyleRef toolbarSideHorzStyleRef     {}; // top / bottom: row + flex-wrap
        StyleRef toolbarSideVertStyleRef     {}; // left / right: column + flex-wrap
        
        StyleRef toolbarItemHorzStyleRef     {}; // horizontal sides: flex-row, thumb left of body
        StyleRef toolbarItemVertStyleRef     {}; // vertical sides: flex-column, thumb above body
        StyleRef toolbarThumbHorzStyleRef    {};
        StyleRef toolbarThumbVertStyleRef    {};

        // Zero-thickness variants for an empty side, used only while no drag is in
        // flight so a drag can still hit the side.
        StyleRef toolbarSideHorzEmptyStyleRef {};
        StyleRef toolbarSideVertEmptyStyleRef {};
        StyleRef toolbarFloatingWindowStyleRef {}; // floating tear-off window content host (no padding)

        StyleRef viewContentStyleRef          {};

        StyleRef panelDropVisualizationStyleRef{};
        StyleRef ghostWindowStyleRef {};
        StyleRef ghostIconStyleRef {};
        StyleRef textStyleRef {};
        StyleRef staticTextStyleRef {};
        StyleRef imageStyleRef {};            // default box for the Image() element
        StyleRef labelStyleRef {};
        StyleRef iconLabelStyleRef {};   // inline icon + text, centered (e.g. command cells)
        StyleRef iconLabelNoIconStyleRef {}; // icon-label with no icon, left-padded to align under icon'd siblings
        StyleRef dragLabelStyleRef {};

        // Tight-packed button/toggle group: container + first/mid/last item styles.
        // Used by BeginButtonGroup / BeginToggleGroup so adjacent items share
        // borders and only the outer corners are rounded.
        StyleRef buttonGroupStyleRef {};
        StyleRef firstButtonStyleRef {};
        StyleRef midButtonStyleRef {};
        StyleRef lastButtonStyleRef {};
        StyleRef toggleGroupStyleRef {};
        StyleRef firstToggleStyleRef {};
        StyleRef midToggleStyleRef {};
        StyleRef lastToggleStyleRef {};

        StyleRef dashedStyleRef{};
        StyleRef firstIconStyleRef {};
        StyleRef iconLeftStyleRef {};   // Icon to the left, text to the right
        StyleRef iconRightStyleRef {};  // Text to the left, icon to the right
        StyleRef iconTopStyleRef {};    // Icon at top, text below
        StyleRef iconBottomStyleRef {}; // Text at top, icon at bottom

        // Demo: 4 icon sides × 3 block vertical alignments, used by the
        // panel_misc vertical-align demo grid. [side][valign] where
        //   side  = 0:Left, 1:Right, 2:Top, 3:Bottom
        //   valign = 0:Top,  1:Middle, 2:Bottom
        StyleRef knobStyleRef {};
        StyleRef sliderFieldTrackStyleRef {};
        StyleRef colorGradientSliderRowStyleRef {}; // outer row of EmitColorGradientSlider (bar + numeric field)
        StyleRef colorPickerTabRowStyleRef {};      // colour-space tabs in ColorPicker
        StyleRef colorPickerPreviewsStyleRef {};    // old/new + palette swatch row in ColorPicker
        StyleRef gradientEditorTabRowStyleRef {};   // colour-space tabs in GradientEditor
        StyleRef sliderFieldBgStyleRef {};   // unfilled half painted by the slider's custom-draw
        StyleRef sliderFieldFillStyleRef {}; // filled half painted by the slider's custom-draw
        StyleRef sliderFieldTextStyleRef {}; // value/label text overlaid on the track
        StyleRef sliderStyleRef {};          // Slider: thin line+knob element (no value text)
        StyleRef sliderGrooveStyleRef {};    // Slider: groove line painted by the custom-draw
        StyleRef sliderKnobStyleRef {};      // Slider: knob painted by the custom-draw
        // Per axis: the track element, and the thumb custom-drawn on top of it.
        StyleRef scrollbarVerticalTrackStyleRef   {};
        StyleRef scrollbarVerticalThumbStyleRef   {};
        StyleRef scrollbarHorizontalTrackStyleRef {};
        StyleRef scrollbarHorizontalThumbStyleRef {};
        int      scrollbarThickness = 12;         // .scrollbar-vertical width (read from CSS, at the UI scale)
        StyleRef colorSwatchStyleRef {};
        StyleRef colorPickerStyleRef {};
        StyleRef colorPickerWheelStyleRef {};
        StyleRef colorPickerPreviewStyleRef {};
        StyleRef colorPickerNewStyleRef{};
        StyleRef colorGradientSliderStyleRef {};
        StyleRef gradientSwatchStyleRef {};
        StyleRef gradientEditorStyleRef {};
        StyleRef gradientEditorBarStyleRef {};
        StyleRef tableStyleRef {};
        StyleRef tableFillStyleRef {};       // table bounded to its flex container (flex-basis:0) — overflows scroll, table doesn't grow
        // Header grid, a sibling of the body so it doesn't scroll with the rows.
        StyleRef tableHeaderSectionStyleRef {};
        StyleRef tableBodiesRowStyleRef {};
        StyleRef tableScrollableBodyStyleRef {};
        // SectionSizing::FitContent section body.
        StyleRef tableFitContentBodyStyleRef {};
        StyleRef tableRowStyleRef {};     // row backdrop (odd rows)
        StyleRef tableRowAltStyleRef {};  // row backdrop (even rows, zebra stripe)
        // Row-mode tree cell: transparent hover/selected, so the row backdrop shows them.
        StyleRef treeTableCellRowModeStyleRef {};
        // The arrow's NoInput glyph, inside a full-row-height hit wrapper
        // (treeTableArrowStyleRef) for forgiving clicks.
        StyleRef treeTableArrowGlyphStyleRef {};
        // Inline-rename editor. Same box as treeTableTextStyleRef (both have a 1px border),
        // so the text doesn't move when a rename starts.
        StyleRef treeTableTextEditStyleRef {};
        StyleRef tableHeaderStyleRef {};
        // Locked column headers: default cursor, since they can't be dragged.
        StyleRef tableHeaderLockedStyleRef {};
        StyleRef tableSizerStyleRef {};
        StyleRef tableSeparatorStyleRef {};
        StyleRef tableCellStyleRef {};
        StyleRef tableCellCompactStyleRef {}; // tighter-padded cell for dense tables (opt-in via VirtualTable cellStyle)
        // Tree column: [indent][arrow|spacer][icon?][text], guide lines custom-drawn.
        StyleRef treeTableCellStyleRef {};
        StyleRef treeTableIndentStyleRef {};
        StyleRef treeTableArrowStyleRef {};
        StyleRef treeTableIconStyleRef {};
        StyleRef treeTableTextStyleRef {};
        StyleRef nodeViewStyleRef {};
        StyleRef viewBackgroundStyleRef {}; // solid backdrop (--bg-view) for the View panel's toolbar dock
        StyleRef nodeStyleRef {};       // single node container (vertical column with border + bg)
        StyleRef nodeHeaderStyleRef {}; // node title bar (distinct background, sits at top of node)
        StyleRef nodeRowStyleRef {};    // node property row (socket | body | socket)
        StyleRef nodeBodyCenterStyleRef {}; // centred body for full-body content (image previews)
        StyleRef nodeSocketStyleRef {}; // socket hit-target; overflow:visible so the dot + hover halo aren't clipped to its box
        int      socketSize = 12;       // .node-socket box size (read from CSS); dot is custom-drawn at this, on-border insets derive from it
        StyleRef nodeTextLeftStyleRef {};   // text-only property body, left-aligned   (left dot only)
        StyleRef nodeTextCenterStyleRef {}; // text-only property body, centred         (both dots)
        StyleRef nodeTextRightStyleRef {};  // text-only property body, right-aligned  (right dot only)
        StyleRef itemListStyleRef {};
        StyleRef itemStyleRef {};
        StyleRef mainWindowStyleRef{};
        StyleRef windowStyleRef {};
        StyleRef dockWindowStyleRef {}; // floating dock window — no internal padding
        StyleRef listAreaStyleRef {};
        StyleRef collectionAreaStyleRef {}; // card-like well behind the list & dictionary Field() helpers
        StyleRef iconButtonStyleRef {}; // compact flat icon button — list/dict delete (in-row)
        StyleRef iconButtonEndStyleRef {}; // same, end-aligned — the trailing add button
        StyleRef menuStyleRef {};
        StyleRef menuItemStyleRef{};
        StyleRef menuScrollButtonStyleRef{};
        StyleRef menuBarStyleRef {};        // horizontal strip at the top of the main window
        StyleRef menuBarTriggerStyleRef {}; // flat top-level trigger inside the menu bar
        StyleRef menuSeparatorStyleRef {};  // thin divider between items in a dropdown
        StyleRef menuItemRowStyleRef {};      // interactive row: icon slot + label + trailing shortcut/arrow
        StyleRef menuItemIconStyleRef {};     // fixed icon slot inside a menu item row
        StyleRef menuItemLabelStyleRef {};    // grow:1 label inside a menu item row
        StyleRef menuItemShortcutStyleRef {}; // dim trailing keyboard-shortcut text
        StyleRef menuItemArrowStyleRef {};    // dim trailing submenu arrow
        StyleRef tooltipStyleRef {};
        StyleRef tooltipTextStyleRef {};
        StyleRef groupLabelStyleRef {};
        StyleRef groupDragLabelStyleRef {};
        StyleRef collectionStyleRef {};
        StyleRef reorderableItemStyleRef{};
        StyleRef horzReorderableGroupStyleRef{};
        StyleRef vertReorderableGroupStyleRef{};
        StyleRef horzReorderableGroupWrapStyleRef{};
        StyleRef vertReorderableGroupWrapStyleRef{};
        StyleRef listViewStyleRef {};
        StyleRef listItemStyleRef {};
        StyleRef listItemRowStyleRef {};      // reorderable list item outer container
        StyleRef listItemDragKnobHorzStyleRef {}; // reorderable list item drag handle (horizontal list — drag along x)
        StyleRef listItemDragKnobVertStyleRef {}; // reorderable list item drag handle (vertical list — drag along y)
        StyleRef listItemBodyStyleRef {};     // reorderable list item text/icon body
        StyleRef treeViewStyleRef {};
        StyleRef gridViewStyleRef {};
        StyleRef gridViewItemStyleRef {};
        StyleRef gridViewScrollStyleRef {};   // virtualized grid scroll viewport (column)
        StyleRef gridViewRowStyleRef {};      // one row strip of cells in the virtual grid
        StyleRef gridViewListItemStyleRef {}; // list-mode cell: icon left, name right, one line
        StyleRef gridViewRenameItemStyleRef {};     // rename-mode cell: icon above inline editor
        StyleRef gridViewRenameListItemStyleRef {}; // rename-mode list row: icon left of editor
        StyleRef gridViewRenameIconStyleRef {};     // the icon child of a renaming cell
        StyleRef gridViewRenameEditStyleRef {};     // grid-mode inline editor (full cell width)
        StyleRef flexFillRowStyleRef {};      // horizontal flex bounded to its parent (flex-basis:0)
        StyleRef flexFillColStyleRef {};      // vertical flex bounded to its parent (flex-basis:0)
        StyleRef flatButtonStyleRef {};       // button that reads as plain left-aligned text (no chrome)
        StyleRef searchRowStyleRef {};         // full-width row hosting a top-of-panel SearchField (bottom spacing)
        // SearchField: the textbox chrome container, its icon, and the borderless inner field.
        StyleRef searchTextboxStyleRef {};
        StyleRef searchTextboxIconStyleRef {};
        StyleRef searchTextboxFieldStyleRef {};
        // Parsed .grid-view-item, for cloning with another iconSize (registering is deduped).
        Style    gridViewItemBaseStyle {};
        // Measured in ComputeRowMetrics so filenames never clip. gridViewListRowHeight is the list row.
        int32_t  gridViewItemLineHeight = 0;
        int32_t  gridViewListRowHeight  = 0;
        StyleRef tabBarStyleRef {};
        StyleRef tabStyleRef {};
        StyleRef tabContentStyleRef {};
        StyleRef tabCloseStyleRef {};

        // DockingContainer styles
        StyleRef dockContainerStyleRef {};
        StyleRef dockSplitHorzStyleRef {};   // horizontal flex row (split into left / right)
        StyleRef dockSplitVertStyleRef {};   // vertical flex column (split into top / bottom)
        StyleRef dockSizerHorzStyleRef {};   // vertical bar between left / right panes
        StyleRef dockSizerVertStyleRef {};   // horizontal bar between top / bottom panes
        StyleRef dockLeafStyleRef {};        // vertical flex: [tab bar][content]
        StyleRef dockContentStyleRef {};     // content area holding the selected panel's draw()

        StyleRef settingsPageSelectionRef {};
        StyleRef settingsPageStyleRef     {};   // settings page body — dock-content layout + left padding off the sizer

        // Arrow icons for tree expand/collapse
        IconID treeArrowRight    = IconID::Invalid;
        IconID treeArrowDown     = IconID::Invalid;

        // Sort-direction indicators for Table header cells.
        IconID sortArrowUpIcon = IconID::Invalid;
        IconID sortArrowDownIcon = IconID::Invalid;

        // Tab close button "X"
        IconID tabCloseIcon = IconID::Invalid;
        IconID menuCheckIcon = IconID::Invalid; // marks the current/checked item in a menu

        // Add / remove row buttons in the list & dictionary Field() helpers
        IconID addIcon    = IconID::Invalid;
        IconID removeIcon = IconID::Invalid;

        // Drag-handle grip (≡) for reorderable-list / tree-list thumbs
        IconID dragHandleIcon = IconID::Invalid;

        // Line color styles (CSS custom properties)
        interstice::VisualColorStyleIndex lineGrayStyle;
        interstice::VisualColorStyleIndex lineYellowStyle;
        interstice::VisualColorStyleIndex lineWhiteStyle;
        interstice::VisualColorStyleIndex lineDarkStyle;
        interstice::VisualColorStyleIndex gridLineStyle;      // node-view background grid
        interstice::VisualColorStyleIndex treeGuideLineStyle;
        interstice::VisualColorStyleIndex treeDropIndicatorStyle; // drop indicator lines + closed-branch outline

        interstice::VisualColorStyleIndex shadowColorStyle;   // SDFGradient: transparent → semi-opaque black, for drop shadows
        interstice::VisualColorStyleIndex marqueeFillColorStyle; // translucent accent for selection-marquee fill

        // Table row height in pixels, measured from real font metrics in ComputeRowMetrics.
        // Must match the rendered rows exactly, or the max scroll oscillates.
        int32_t tableRowHeight = 0;

        // Tree-table cell metrics, read from the stylesheet in ComputeRowMetrics (a theme that
        // omits them gets the measured text line height): pixels per indent level, the width of
        // the expander (arrow / leaf spacer) column, and the node icon / arrow glyph sizes.
        int32_t treeTableIndent        = 0;
        int32_t treeTableExpanderWidth = 0;
        int32_t treeTableIconSize      = 0;
        int32_t treeTableArrowGlyphSize = 0;

        // Node-view grid pitch: half a property row, measured in ComputeRowMetrics.
        int32_t nodeGridSize = 0;

    public:
        void InitializeDefaultStyles(StyleManager& styleManager, ITextSystem* textSystem);
        // Registers the theme's @font-face families with textSystem (when given)
        // before any style references them.
        // themePath: the palette file. styleFiles: the rule files, in order; relative
        // paths are next to the palette file.
        bool LoadTheme(StyleManager& styleManager, ITextSystem* textSystem, const std::string& themePath,
                       std::span<const std::string> styleFiles, float fontScale);

        // The style of a CSS class from the loaded style files (library or app);
        // invalid when no file defines it.
        StyleRef GetStyle(std::string_view cssClass) const;

        // A palette color by custom-property name ("--node-tint-input"), or `fallback`
        // when the loaded theme doesn't define it.
        color4f GetColor(std::string_view varName, color4f fallback = {}) const;

        // Measures the font-dependent metrics above. Call after every LoadTheme.
        void ComputeRowMetrics(StyleManager& styleManager, ITextSystem& textSystem);
    private:
        std::unordered_map<std::string, color4f> m_paletteColors;
        std::map<std::string, StyleRef, std::less<>> m_classStyles;  // every class in the style files
    };

} // namespace interstice
