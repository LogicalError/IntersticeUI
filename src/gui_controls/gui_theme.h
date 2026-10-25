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

        StyleRef emptyHorzStyleRef;     // horizontal flex, no wrap - used for grouping elements w/o drawing anything
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

        // Demo: 4 icon sides x 3 block vertical alignments, used by the
        // panel_misc vertical-align demo grid. [side][valign] where
        //   side  = 0:Left, 1:Right, 2:Top, 3:Bottom
        //   valign = 0:Top,  1:Middle, 2:Bottom
        StyleRef knobStyleRef {};
        StyleRef sliderFieldTrackStyleRef {};
        StyleRef colorGradientSliderRowStyleRef {}; // outer row of EmitColorGradientSlider (bar + numeric field)
        StyleRef sliderFieldBgStyleRef {};   // unfilled half painted by the slider's custom-draw
        StyleRef sliderFieldFillStyleRef {}; // filled half painted by the slider's custom-draw
        StyleRef sliderFieldTextStyleRef {}; // label text overlaid on the track
        StyleRef sliderFieldValueStyleRef {};// value text overlaid on the track
        StyleRef sliderFieldEdgeStyleRef {}; // the line ending the fill
        StyleRef sliderStyleRef {};          // Slider: thin line+knob element (no value text)
        StyleRef sliderGrooveStyleRef {};    // Slider: groove line painted by the custom-draw
        StyleRef sliderFillStyleRef {};      // Slider: the groove's filled part, up to the knob
        StyleRef sliderKnobStyleRef {};      // Slider: knob painted by the custom-draw
        StyleRef switchStyleRef {};          // Switch: the control (track + label)
        StyleRef switchTrackStyleRef {};     // Switch: the track; its knob is painted by the custom-draw
        StyleRef switchKnobStyleRef {};      // Switch: the knob
        StyleRef switchLabelStyleRef {};     // Switch: the label after the track
        // Per axis: the track element, and the thumb custom-drawn on top of it.
        StyleRef scrollbarVerticalTrackStyleRef   {};
        StyleRef scrollbarVerticalThumbStyleRef   {};
        StyleRef scrollbarHorizontalTrackStyleRef {};
        StyleRef scrollbarHorizontalThumbStyleRef {};
        int      scrollbarThickness = 12;         // .scrollbar-vertical width (read from CSS, at the UI scale)
        StyleRef colorSwatchStyleRef {};
        StyleRef colorFieldStyleRef {};       // ColorField: swatch, hex and alpha in a row
        StyleRef colorHexStyleRef {};         // ColorField: the hex field
        StyleRef colorAlphaStyleRef {};       // ColorField: the alpha box (value and %)
        StyleRef colorAlphaInputStyleRef {};  // ColorField: the alpha value in its box
        StyleRef colorAlphaUnitStyleRef {};   // ColorField: the % after it
        StyleRef colorAlphaCaptionStyleRef {};// ColorField: the "A" before it
        StyleRef colorPickerStyleRef {};
        StyleRef dialogHeaderStyleRef {};               // dialog: the title row
        StyleRef dialogTitleStyleRef {};
        StyleRef dialogContextStyleRef {};    // dialog: what's being edited, after the title
        StyleRef colorPickerHdrValueStyleRef {}; // color picker: the intensity value                // dialog: its title
        StyleRef dialogCloseStyleRef {};                // dialog: the close button
        StyleRef dialogFooterStyleRef {};               // dialog: status and buttons
        StyleRef dialogStatusStyleRef {};               // dialog: the footer's status text
        StyleRef dialogStatusDotStyleRef {};            // dialog: the dot before it
        StyleRef dialogButtonStyleRef {};               // dialog: a secondary button (Cancel)
        StyleRef dialogPrimaryButtonStyleRef {};        // dialog: the primary button (OK)
        StyleRef checkboxStyleRef {};                   // Checkbox: the control
        StyleRef checkboxBoxStyleRef {};                // Checkbox: the box
        StyleRef checkboxLabelStyleRef {};              // Checkbox: its label
        StyleRef colorPickerBodyStyleRef {};            // color picker: planes and controls
        StyleRef colorPickerPlanesStyleRef {};          // color picker: square and strips
        StyleRef colorPickerSquareStyleRef {};          // color picker: the saturation/value square
        StyleRef colorPickerStripStyleRef {};           // color picker: the hue and alpha strips
        StyleRef colorPickerControlsStyleRef {};        // color picker: the right column
        StyleRef colorPickerPreviewGroupStyleRef {};    // color picker: preview and captions
        StyleRef colorPickerCaptionsStyleRef {};        // color picker: New / Original
        StyleRef colorPickerCaptionStyleRef {};         // color picker: a small caption
        StyleRef colorPickerModeRowStyleRef {};         // color picker: space, Linear, range
        StyleRef colorPickerRangeStyleRef {};           // color picker: the 0-255 / 0-1 toggle
        StyleRef colorPickerChannelsStyleRef {};        // color picker: the channel rows
        StyleRef colorPickerChannelStyleRef {};         // color picker: one channel row
        StyleRef colorPickerChannelLabelStyleRef {};    // color picker: R, G, B, A ...
        StyleRef colorPickerTrackStyleRef {};           // color picker: a channel's track
        StyleRef colorPickerChannelValueStyleRef {};    // color picker: a channel's value
        StyleRef colorPickerFieldRowStyleRef {};        // color picker: Hex / HDR rows
        StyleRef colorPickerRowLabelStyleRef {};        // color picker: Hex, HDR
        StyleRef colorPickerHdrStyleRef {};             // color picker: the intensity box
        StyleRef colorPickerSwatchesStyleRef {};        // color picker: swatches and recent
        StyleRef colorPickerSwatchRowStyleRef {};       // color picker: one swatch row
        StyleRef colorPickerSectionLabelStyleRef {};    // color picker: Swatches, Recent
        StyleRef colorPickerSwatchStyleRef {};          // color picker: a swatch
        StyleRef colorPickerRecentStyleRef {};          // color picker: a recent color
        StyleRef colorPickerPreviewStyleRef {};
        StyleRef colorPickerNewStyleRef{};
        StyleRef colorGradientSliderStyleRef {};
        StyleRef gradientSwatchStyleRef {};
        StyleRef gradientEditorStyleRef {};
        StyleRef gradientEditorBarStyleRef {};
        StyleRef dialogSmallButtonStyleRef {};              // dialog: a 24 high button (Save preset, Reverse)
        StyleRef dialogIconButtonStyleRef {};               // dialog: a borderless 24 icon button (previous / next)
        StyleRef dialogSquareButtonStyleRef {};             // dialog: a bordered 24 icon button (duplicate)
        StyleRef dialogDeleteButtonStyleRef {};             // dialog: the same, in the error color
        StyleRef gradientEditorPresetsStyleRef {};          // gradient editor: the presets row
        StyleRef gradientEditorSectionLabelStyleRef {};     // gradient editor: Presets
        StyleRef gradientEditorPresetStyleRef {};           // gradient editor: a preset chip
        StyleRef gradientEditorBadgeStyleRef {};            // gradient editor: the selected stop's location
        StyleRef gradientEditorColumnsStyleRef {};          // gradient editor: the two columns
        StyleRef gradientEditorColumnStyleRef {};           // gradient editor: the left column
        StyleRef gradientEditorRowStyleRef {};              // gradient editor: a labeled row
        StyleRef gradientEditorHeadingStyleRef {};          // gradient editor: Color stop
        StyleRef gradientEditorCountStyleRef {};            // gradient editor: 2 / 3
        StyleRef gradientEditorLabelStyleRef {};            // gradient editor: a row's label
        StyleRef gradientEditorLocationStyleRef {};         // gradient editor: the location box
        StyleRef gradientEditorLocationInputStyleRef {};    // gradient editor: its value
        StyleRef gradientEditorLocationUnitStyleRef {};     // gradient editor: its %
        StyleRef gradientEditorDividerStyleRef {};          // gradient editor: a divider line
        StyleRef gradientEditorActionsStyleRef {};          // gradient editor: Reverse, Distribute
        StyleRef gradientEditorTableStyleRef {};            // gradient editor: the stops table
        StyleRef gradientEditorTableHeadStyleRef {};        // gradient editor: its header row
        StyleRef gradientEditorTableRowStyleRef {};         // gradient editor: a stop's row
        StyleRef gradientEditorTypeCellStyleRef {};         // gradient editor: Type
        StyleRef gradientEditorSwatchCellStyleRef {};       // gradient editor: the stop's color
        StyleRef gradientEditorHeadCellStyleRef {};         // gradient editor: a header caption
        StyleRef gradientEditorHeadLocationStyleRef {};     // gradient editor: Location
        StyleRef gradientEditorValueCellStyleRef {};        // gradient editor: the stop's hex
        StyleRef gradientEditorLocationCellStyleRef {};     // gradient editor: the stop's location
        StyleRef tableStyleRef {};
        StyleRef tableFillStyleRef {};       // table bounded to its flex container (flex-basis:0) - overflows scroll, table doesn't grow
        // Header grid, a sibling of the body so it doesn't scroll with the rows.
        StyleRef tableHeaderSectionStyleRef {};
        StyleRef tableBodiesRowStyleRef {};
        StyleRef tableScrollableBodyStyleRef {};
        // SectionSizing::FitContent section body.
        StyleRef tableFitContentBodyStyleRef {};
        StyleRef tableRowStyleRef {};     // row backdrop (odd rows)
        StyleRef tableRowAltStyleRef {};
        StyleRef tableRowBarStyleRef {};  // accent bar on a selected row's left
        StyleRef listRowStyleRef {};      // rounded row without stripes (tree views, TableState::rowStyle)  // row backdrop (even rows, zebra stripe)
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
        StyleRef tableCellNumberStyleRef {};  // right-aligned number cell (TableColumn::cellStyle)
        StyleRef tableCellCompactStyleRef {}; // tighter-padded cell for dense tables (opt-in via VirtualTable cellStyle)
        // Tree column: [indent][arrow|spacer][icon?][text], guide lines custom-drawn.
        StyleRef treeTableCellStyleRef {};
        StyleRef treeTableIndentStyleRef {};
        StyleRef treeTableArrowStyleRef {};
        StyleRef treeTableIconStyleRef {};
        StyleRef treeTableTextStyleRef {};
        StyleRef nodeViewStyleRef {};
        StyleRef viewBackgroundStyleRef {}; // solid backdrop (--bg-app) for the View panel's toolbar dock
        StyleRef nodeStyleRef {};       // single node container (vertical column with border + bg)
        StyleRef nodeHeaderStyleRef {}; // node title bar (distinct background, sits at top of node)
        StyleRef nodeCategoryStyleRef {}; // the category square in a node header
        StyleRef nodeTitleStyleRef {};    // a node header's title
        StyleRef nodeCaptionStyleRef {};  // a node header's category name, right-aligned
        StyleRef nodeViewOverlayStyleRef {};       // panel floating over the node view (breadcrumb, zoom bar)
        StyleRef nodeViewOverlayButtonStyleRef {}; // flat button in such a panel
        StyleRef nodeViewMinimapStyleRef {};
        StyleRef breadcrumbStyleRef {};
        StyleRef breadcrumbSegmentStyleRef {};     // a clickable path segment
        StyleRef breadcrumbCurrentStyleRef {};     // the last segment
        StyleRef breadcrumbSeparatorStyleRef {};
        // Vector fields: the X/Y/Z/W label attached to each component, and the component's field.
        StyleRef axisLabelStyleRefs[4] {};
        StyleRef vectorComponentStyleRef {};
        StyleRef statusBarStyleRef {};
        StyleRef statusTextStyleRef {};
        // Command search: the box, its hint and keycap, the list of matches and its highlighted row.
        StyleRef commandSearchStyleRef {};
        StyleRef commandSearchHintStyleRef {};
        StyleRef commandSearchKeyStyleRef {};
        StyleRef commandSearchListStyleRef {};
        StyleRef commandSearchCurrentRowStyleRef {};
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
        StyleRef dockWindowStyleRef {}; // floating dock window - no internal padding
        StyleRef dockWindowLeafStyleRef {};     // a torn-off window's root leaf: the title bar over the panel
        StyleRef dockWindowPanelStyleRef {};    // the panel inset under a torn-off window's title bar
        StyleRef dockWindowBodyStyleRef {};     // a torn-off window's split layout, inset the same
        StyleRef titleBarTabBarStyleRef {};     // the tab strip in a torn-off window's title bar
        StyleRef titleBarGripStyleRef {};       // the grip before it
        StyleRef titleBarTitleStyleRef {};      // the window's name when the tabs aren't in the bar
        StyleRef titleBarTextButtonStyleRef {}; // a text button in a title bar (Dock)
        StyleRef listAreaStyleRef {};
        StyleRef collectionAreaStyleRef {}; // card-like well behind the list & dictionary Field() helpers
        StyleRef typedFieldStyleRef {};                 // TypedField: the box
        StyleRef typedFieldInputStyleRef {};            // TypedField: the value
        StyleRef typedFieldTypeStyleRef {};             // TypedField: the type tag
        StyleRef collectionListStyleRef {};             // list field: the rows
        StyleRef collectionRowStyleRef {};              // list field: a row
        StyleRef collectionIndexStyleRef {};            // list field: a row's index
        StyleRef collectionRemoveStyleRef {};           // list / dictionary: remove
        StyleRef collectionAddStyleRef {};              // list field: Add item
        StyleRef collectionAddEntryStyleRef {};         // dictionary: Add entry
        StyleRef collectionEntryStyleRef {};            // dictionary: a row
        StyleRef collectionValueStyleRef {};            // dictionary: a value
        StyleRef collectionHeaderStyleRef {};           // dictionary: the header row
        StyleRef collectionHeaderCellStyleRef {};       // dictionary: Key / Value
        StyleRef collectionHeaderCaptionStyleRef {};    // dictionary: the caption
        StyleRef collectionHeaderTypeStyleRef {};       // dictionary: the type
        StyleRef groupLabelTopStyleRef {};              // Label with a caption: the container
        StyleRef groupLabelTitleStyleRef {};            // Label with a caption: the title
        StyleRef groupLabelCaptionStyleRef {};          // Label with a caption: the caption
        StyleRef iconButtonStyleRef {}; // compact flat icon button - list/dict delete (in-row)
        StyleRef iconButtonEndStyleRef {}; // same, end-aligned - the trailing add button
        StyleRef menuStyleRef {};
        StyleRef menuItemStyleRef{};
        StyleRef menuScrollButtonStyleRef{};
        StyleRef menuBarStyleRef {};        // horizontal strip at the top of the main window
        StyleRef menuBarTriggerStyleRef {}; // flat top-level trigger inside the menu bar
        StyleRef titleBarStyleRef {};            // a window's own title bar (BeginTitleBar)
        StyleRef titleBarButtonStyleRef {};      // its minimize and maximize buttons
        StyleRef titleBarCloseButtonStyleRef {};
        StyleRef menuSeparatorStyleRef {};
        StyleRef menuHeaderStyleRef {};     // a section's name in a menu  // thin divider between items in a dropdown
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
        StyleRef listItemDragKnobHorzStyleRef {}; // reorderable list item drag handle (horizontal list - drag along x)
        StyleRef listItemDragKnobVertStyleRef {}; // reorderable list item drag handle (vertical list - drag along y)
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
        StyleRef dockContainerMainStyleRef {};   // a main window's docking area, with a side gutter
        StyleRef dockSplitHorzStyleRef {};   // horizontal flex row (split into left / right)
        StyleRef dockSplitVertStyleRef {};   // vertical flex column (split into top / bottom)
        StyleRef dockSizerHorzStyleRef {};   // vertical bar between left / right panes
        StyleRef dockSizerVertStyleRef {};   // horizontal bar between top / bottom panes
        StyleRef dockLeafStyleRef {};        // vertical flex: [tab bar][content]
        StyleRef dockContentStyleRef {};     // content area holding the selected panel's draw()

        StyleRef settingsPageSelectionRef {};
        StyleRef settingsPageStyleRef     {};   // settings page body - dock-content layout + left padding off the sizer

        // Arrow icons for tree expand/collapse
        IconID treeArrowRight    = IconID::Invalid;
        IconID treeArrowDown     = IconID::Invalid;

        // Sort-direction indicators for Table header cells.
        IconID sortArrowUpIcon = IconID::Invalid;
        IconID sortArrowDownIcon = IconID::Invalid;

        // Tab close button "X"
        IconID tabCloseIcon = IconID::Invalid;
        IconID menuCheckIcon = IconID::Invalid; // marks the current/checked item in a menu
        IconID dropdownArrowIcon = IconID::Invalid; // after a dropdown's text (menu triggers outside a menu bar)
        IconID dropdownArrowUpIcon = IconID::Invalid; // the same while its menu is open
        IconID chevronLeftIcon   = IconID::Invalid; // previous
        IconID duplicateIcon     = IconID::Invalid;
        IconID deleteIcon        = IconID::Invalid;
        IconID reverseIcon       = IconID::Invalid;
        IconID distributeIcon    = IconID::Invalid;

        // Add / remove row buttons in the list & dictionary Field() helpers
        IconID addIcon    = IconID::Invalid;
        IconID removeIcon = IconID::Invalid;

        // Drag-handle grip (three-bar) for reorderable-list / tree-list thumbs
        IconID dragHandleIcon = IconID::Invalid;
        IconID dockIcon = IconID::Invalid;        // returns a torn-off panel to its dock
        IconID searchIcon     = IconID::Invalid;
        IconID windowMinimizeIcon = IconID::Invalid;
        IconID windowMaximizeIcon = IconID::Invalid;
        IconID windowRestoreIcon  = IconID::Invalid;

        // Line color styles (CSS custom properties)
        interstice::VisualColorStyleIndex lineGrayStyle;
        interstice::VisualColorStyleIndex lineYellowStyle;
        interstice::VisualColorStyleIndex lineWhiteStyle;
        interstice::VisualColorStyleIndex lineDarkStyle;
        interstice::VisualColorStyleIndex treeGuideLineStyle;
        interstice::VisualColorStyleIndex dividerHoverLineStyle; // a hovered divider drawn as a line (label sizer)
        interstice::VisualColorStyleIndex nanTextColor;          // a number field's NaN
        interstice::VisualColorStyleIndex checkerAColor, checkerBColor;     // transparency checkerboard
        interstice::VisualColorStyleIndex alphaTrackColor, alphaFillColor;  // a color's alpha bar
        interstice::VisualColorStyleIndex accentColor, panelColor;          // a selected swatch's ring and gap
        interstice::VisualColorStyleIndex treeDropIndicatorStyle; // drop indicator lines + closed-branch outline

        interstice::VisualColorStyleIndex shadowColorStyle;   // SDFGradient: transparent -> semi-opaque black, for drop shadows
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

        // A palette color by custom-property name ("--node-input"), or `fallback`
        // when the loaded theme doesn't define it.
        color4f GetColor(std::string_view varName, color4f fallback = {}) const;

        // Measures the font-dependent metrics above. Call after every LoadTheme.
        void ComputeRowMetrics(StyleManager& styleManager, ITextSystem& textSystem);
    private:
        std::unordered_map<std::string, color4f> m_paletteColors;
        std::map<std::string, StyleRef, std::less<>> m_classStyles;  // every class in the style files
    };

} // namespace interstice
