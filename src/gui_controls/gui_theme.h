#pragma once

#include <gui_core/style/gui_style_ref.h>
#include <gui_core/style/gui_style.h>

#include <gui_core/gui_result.h>

#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

namespace tachyon
{
    /// @addtogroup styling
    /// @{

    class StyleManager;
    class ITextSystem;

    /// Default control styles. GUITheme::controls points at the active bundle;
    /// BeginToolbar/EndToolbar swap in the toolbar variants.
    struct ControlStyles
    {
        StyleRef button;        ///< .button / .toolbar-button
        StyleRef toggle;        ///< .toggle / .toolbar-toggle
        StyleRef textbox;       ///< .textbox / .toolbar-textbox
        StyleRef number;        ///< numeric fields: the textbox in the monospace font
        StyleRef menuTrigger;   ///< .menu-trigger / .toolbar-menu-trigger
    };

    /// The library's styles, icons and theme metrics, the defaults the controls draw with.
    /// LoadTheme registers every class of the style files; each xxxStyleRef below is the
    /// class of the same name in kebab-case (tableRowAltStyleRef -> .table-row-alt) unless
    /// its comment names another. They are reassigned on every theme (re)load.
    ///
    /// An app's own classes (from SetStyleFiles) are found by name; look them up in OnGUI,
    /// since a reload registers them again:
    ///
    ///     Text("My themed app", theme.GetStyle("banner"));
    ///     Button("Careful!", onClick, theme.GetStyle("warning-button"));
    class GUITheme
    {
    public:
        ControlStyles regularControls {};                   ///< .button, .toggle, .textbox, .number-field, .menu-trigger
        ControlStyles toolbarControls {};                   ///< the .toolbar-* variants of those
        const ControlStyles* controls = &regularControls;   ///< the bundle in use

        StyleRef emptyHorzStyleRef;     ///< horizontal flex, no wrap; groups elements without drawing anything
        StyleRef emptyHorzWrapStyleRef; ///< horizontal flex, wrap
        StyleRef emptyVertStyleRef;     ///< vertical flex, no wrap
        StyleRef emptyVertWrapStyleRef; ///< vertical flex, wrap
        /// Built in code: an undecorated, growing, wrapping flex row (a multi-value drag field).
        StyleRef fieldElementGroupStyleRef;

        // View panel toolbars: side bins (one reorderable group per side), the per-toolbar
        // item container and the central content placeholder.
        StyleRef toolbarSideHorzStyleRef     {}; ///< toolbar side bin, top / bottom: row + flex-wrap
        StyleRef toolbarSideVertStyleRef     {}; ///< toolbar side bin, left / right: column + flex-wrap

        StyleRef toolbarItemHorzStyleRef     {}; ///< toolbar on a horizontal side: row, thumb left of body
        StyleRef toolbarItemVertStyleRef     {}; ///< toolbar on a vertical side: column, thumb above body
        StyleRef toolbarThumbHorzStyleRef    {}; ///< a toolbar's drag thumb on a horizontal side
        StyleRef toolbarThumbVertStyleRef    {}; ///< a toolbar's drag thumb on a vertical side

        /// Zero-thickness top / bottom side bin for an empty side, used only while no drag
        /// is in flight (during a drag the full bin is used, so the side can be hit).
        StyleRef toolbarSideHorzEmptyStyleRef {};
        StyleRef toolbarSideVertEmptyStyleRef {}; ///< the same, for left / right
        StyleRef toolbarFloatingWindowStyleRef {}; ///< floating tear-off window content host (no padding)

        StyleRef viewContentStyleRef          {}; ///< the central content placeholder

        StyleRef panelDropVisualizationStyleRef{}; ///< overlay showing where a dragged panel or toolbar docks
        StyleRef ghostWindowStyleRef {};   ///< BeginGhostWindow's default: the window following a drag
        StyleRef ghostIconStyleRef {};     ///< the icon in a drag ghost
        StyleRef textStyleRef {};          ///< .text-style: rich text
        StyleRef staticTextStyleRef {};    ///< plain non-interactive text
        StyleRef imageStyleRef {};            ///< default box for the Image() element
        StyleRef labelStyleRef {};       ///< Label()'s default
        StyleRef iconLabelStyleRef {};   ///< inline icon + text, centered (e.g. command cells)
        StyleRef iconLabelNoIconStyleRef {}; ///< icon-label with no icon, left-padded to align under icon'd siblings
        StyleRef dragLabelStyleRef {};   ///< DragLabel()'s default

        /// @name Button and toggle groups
        /// Tight-packed button/toggle group: container + first/mid/last item styles.
        /// Used by BeginButtonGroup / BeginToggleGroup so adjacent items share
        /// borders and only the outer corners are rounded.
        ///@{
        StyleRef buttonGroupStyleRef {};
        StyleRef firstButtonStyleRef {};
        StyleRef midButtonStyleRef {};
        StyleRef lastButtonStyleRef {};
        StyleRef toggleGroupStyleRef {};
        StyleRef firstToggleStyleRef {};
        StyleRef midToggleStyleRef {};
        StyleRef lastToggleStyleRef {};
        ///@}

        StyleRef dashedStyleRef{};          ///< a box with a dashed border
        StyleRef firstIconStyleRef {};      ///< .first-icon
        StyleRef iconLeftStyleRef {};   ///< icon left, text right
        StyleRef iconRightStyleRef {};  ///< text left, icon right
        StyleRef iconTopStyleRef {};    ///< icon above, text below
        StyleRef iconBottomStyleRef {}; ///< text above, icon below

        StyleRef knobStyleRef {};           ///< .knob
        StyleRef sliderFieldTrackStyleRef {}; ///< SliderField: the track holding the parts below
        StyleRef colorGradientSliderRowStyleRef {}; ///< GradientSlider: the row holding the bar and its field
        StyleRef sliderFieldBgStyleRef {};   ///< unfilled half painted by the slider's custom-draw
        StyleRef sliderFieldFillStyleRef {}; ///< filled half painted by the slider's custom-draw
        StyleRef sliderFieldTextStyleRef {}; ///< label text overlaid on the track
        StyleRef sliderFieldValueStyleRef {}; ///< value text overlaid on the track
        StyleRef sliderFieldEdgeStyleRef {}; ///< the line ending the fill
        StyleRef sliderStyleRef {};          ///< Slider: a groove and knob, no value text
        StyleRef sliderGrooveStyleRef {};    ///< Slider: groove line painted by the custom-draw
        StyleRef sliderFillStyleRef {};      ///< Slider: the groove's filled part, up to the knob
        StyleRef sliderKnobStyleRef {};      ///< Slider: knob painted by the custom-draw
        StyleRef switchStyleRef {};          ///< Switch: the control (track + label)
        StyleRef switchTrackStyleRef {};     ///< Switch: the track; its knob is painted by the custom-draw
        StyleRef switchKnobStyleRef {};      ///< Switch: the knob
        StyleRef switchLabelStyleRef {};     ///< Switch: the label after the track
        /// @name Scrollbars
        /// Per axis: the track element, and the thumb custom-drawn on top of it.
        /// Tracks: .scrollbar-vertical / .scrollbar-horizontal; thumbs: the same + -thumb.
        ///@{
        StyleRef scrollbarVerticalTrackStyleRef   {};
        StyleRef scrollbarVerticalThumbStyleRef   {};
        StyleRef scrollbarHorizontalTrackStyleRef {};
        StyleRef scrollbarHorizontalThumbStyleRef {};
        ///@}
        int      scrollbarThickness = 12;         ///< .scrollbar-vertical width (read from CSS, at the UI scale)
        StyleRef colorSwatchStyleRef {};      ///< ColorField: the swatch
        StyleRef colorFieldStyleRef {};       ///< ColorField: swatch, hex and alpha in a row
        StyleRef colorHexStyleRef {};         ///< ColorField: the hex field
        StyleRef colorAlphaStyleRef {};       ///< ColorField: the alpha box (value and %)
        StyleRef colorAlphaInputStyleRef {};  ///< ColorField: the alpha value in its box
        StyleRef colorAlphaUnitStyleRef {};   ///< ColorField: the % after it
        StyleRef colorAlphaCaptionStyleRef {}; ///< ColorField: the "A" before it
        StyleRef colorPickerStyleRef {};      ///< color picker: the window
        StyleRef dialogHeaderStyleRef {};               ///< dialog: the title row
        StyleRef dialogTitleStyleRef {};      ///< dialog: its title
        StyleRef dialogContextStyleRef {};    ///< dialog: what's being edited, after the title
        StyleRef colorPickerHdrValueStyleRef {}; ///< color picker: the intensity value
        StyleRef dialogCloseStyleRef {};                ///< dialog: the close button
        StyleRef dialogFooterStyleRef {};               ///< dialog: status and buttons
        StyleRef dialogStatusStyleRef {};               ///< dialog: the footer's status text
        StyleRef dialogStatusDotStyleRef {};            ///< dialog: the dot before it
        StyleRef dialogButtonStyleRef {};               ///< dialog: a secondary button (Cancel)
        StyleRef dialogPrimaryButtonStyleRef {};        ///< dialog: the primary button (OK); .dialog-button-primary
        StyleRef dialogDangerButtonStyleRef {};         ///< dialog: a destructive button (Quit, Delete); .dialog-button-danger
        StyleRef messageBoxStyleRef {};                 ///< MessageDialog: the window
        StyleRef messageBoxCloseStyleRef {};            ///< MessageDialog: the close button, top right
        StyleRef messageBoxBodyStyleRef {};             ///< MessageDialog: the icon and text row
        StyleRef messageBoxWarningStyleRef {};          ///< MessageDialog: severity circle, warning / quit
        StyleRef messageBoxQuestionStyleRef {};         ///< MessageDialog: severity circle, question / info
        StyleRef messageBoxErrorStyleRef {};            ///< MessageDialog: severity circle, error
        StyleRef messageBoxTextStyleRef {};             ///< MessageDialog: title, message and extras
        StyleRef messageBoxTitleStyleRef {};            ///< MessageDialog: the title
        StyleRef messageBoxMessageStyleRef {};          ///< MessageDialog: the message
        StyleRef messageBoxWellStyleRef {};             ///< MessageDialog: the affected items
        StyleRef messageBoxItemStyleRef {};             ///< MessageDialog: an affected item's row
        StyleRef messageBoxItemDotStyleRef {};          ///< MessageDialog: an affected item's dot
        StyleRef messageBoxItemNameStyleRef {};         ///< MessageDialog: an affected item's name
        StyleRef messageBoxItemMetaStyleRef {};         ///< MessageDialog: an affected item's extra info
        StyleRef messageBoxDetailsToggleStyleRef {};    ///< MessageDialog: "Details" with its chevron
        StyleRef messageBoxDetailsStyleRef {};          ///< MessageDialog: the details, monospace in a well
        StyleRef checkboxStyleRef {};                   ///< Checkbox: the control
        StyleRef checkboxBoxStyleRef {};                ///< Checkbox: the box
        StyleRef checkboxLabelStyleRef {};              ///< Checkbox: its label
        StyleRef colorPickerBodyStyleRef {};            ///< color picker: planes and controls
        StyleRef colorPickerPlanesStyleRef {};          ///< color picker: square and strips
        StyleRef colorPickerSquareStyleRef {};          ///< color picker: the saturation/value square
        StyleRef colorPickerStripStyleRef {};           ///< color picker: the hue and alpha strips
        StyleRef colorPickerControlsStyleRef {};        ///< color picker: the right column
        StyleRef colorPickerPreviewGroupStyleRef {};    ///< color picker: preview and captions
        StyleRef colorPickerCaptionsStyleRef {};        ///< color picker: New / Original
        StyleRef colorPickerCaptionStyleRef {};         ///< color picker: a small caption
        StyleRef colorPickerModeRowStyleRef {};         ///< color picker: space, Linear, range
        StyleRef colorPickerRangeStyleRef {};           ///< color picker: the 0-255 / 0-1 toggle
        StyleRef colorPickerChannelsStyleRef {};        ///< color picker: the channel rows
        StyleRef colorPickerChannelStyleRef {};         ///< color picker: one channel row
        StyleRef colorPickerChannelLabelStyleRef {};    ///< color picker: R, G, B, A ...
        StyleRef colorPickerTrackStyleRef {};           ///< color picker: a channel's track
        StyleRef colorPickerChannelValueStyleRef {};    ///< color picker: a channel's value
        StyleRef colorPickerFieldRowStyleRef {};        ///< color picker: Hex / HDR rows
        StyleRef colorPickerRowLabelStyleRef {};        ///< color picker: Hex, HDR
        StyleRef colorPickerHdrStyleRef {};             ///< color picker: the intensity box
        StyleRef colorPickerSwatchesStyleRef {};        ///< color picker: swatches and recent
        StyleRef colorPickerSwatchRowStyleRef {};       ///< color picker: one swatch row
        StyleRef colorPickerSectionLabelStyleRef {};    ///< color picker: Swatches, Recent
        StyleRef colorPickerSwatchStyleRef {};          ///< color picker: a swatch
        StyleRef colorPickerRecentStyleRef {};          ///< color picker: a recent color
        StyleRef colorPickerPreviewStyleRef {};         ///< color picker: the color preview
        StyleRef colorPickerNewStyleRef{};              ///< color picker: the add-swatch button
        StyleRef colorGradientSliderStyleRef {};        ///< the gradient bar of a color slider row
        StyleRef gradientSwatchStyleRef {};             ///< GradientField: the swatch
        StyleRef gradientEditorStyleRef {};             ///< gradient editor: the window
        StyleRef gradientEditorBarStyleRef {};          ///< gradient editor: the gradient bar with its stops
        StyleRef dialogSmallButtonStyleRef {};              ///< dialog: a short button (Save preset, Reverse)
        StyleRef dialogIconButtonStyleRef {};               ///< dialog: a small borderless icon button (previous / next)
        StyleRef dialogSquareButtonStyleRef {};             ///< dialog: a small bordered icon button (duplicate)
        StyleRef dialogDeleteButtonStyleRef {};             ///< dialog: the same, in the error color
        StyleRef gradientEditorPresetsStyleRef {};          ///< gradient editor: the presets row
        StyleRef gradientEditorSectionLabelStyleRef {};     ///< gradient editor: Presets
        StyleRef gradientEditorPresetStyleRef {};           ///< gradient editor: a preset chip
        StyleRef gradientEditorBadgeStyleRef {};            ///< gradient editor: the selected stop's location
        StyleRef gradientEditorColumnsStyleRef {};          ///< gradient editor: the two columns
        StyleRef gradientEditorColumnStyleRef {};           ///< gradient editor: the left column
        StyleRef gradientEditorRowStyleRef {};              ///< gradient editor: a labeled row
        StyleRef gradientEditorHeadingStyleRef {};          ///< gradient editor: Color stop
        StyleRef gradientEditorCountStyleRef {};            ///< gradient editor: 2 / 3
        StyleRef gradientEditorLabelStyleRef {};            ///< gradient editor: a row's label
        StyleRef gradientEditorLocationStyleRef {};         ///< gradient editor: the location box
        StyleRef gradientEditorLocationInputStyleRef {};    ///< gradient editor: its value
        StyleRef gradientEditorLocationUnitStyleRef {};     ///< gradient editor: its %
        StyleRef gradientEditorDividerStyleRef {};          ///< gradient editor: a divider line
        StyleRef gradientEditorActionsStyleRef {};          ///< gradient editor: Reverse, Distribute
        StyleRef gradientEditorTableStyleRef {};            ///< gradient editor: the stops table
        StyleRef gradientEditorTableHeadStyleRef {};        ///< gradient editor: its header row
        StyleRef gradientEditorTableRowStyleRef {};         ///< gradient editor: a stop's row
        StyleRef gradientEditorTypeCellStyleRef {};         ///< gradient editor: Type
        StyleRef gradientEditorSwatchCellStyleRef {};       ///< gradient editor: the stop's color
        StyleRef gradientEditorHeadCellStyleRef {};         ///< gradient editor: a header caption
        StyleRef gradientEditorHeadLocationStyleRef {};     ///< gradient editor: Location
        StyleRef gradientEditorValueCellStyleRef {};        ///< gradient editor: the stop's hex
        StyleRef gradientEditorLocationCellStyleRef {};     ///< gradient editor: the stop's location
        StyleRef tableStyleRef {};           ///< Table: the table
        /// Table bounded to its flex container (flex-basis:0): it doesn't grow, its rows scroll.
        StyleRef tableFillStyleRef {};
        /// Header grid, a sibling of the body so it doesn't scroll with the rows.
        StyleRef tableHeaderSectionStyleRef {};
        StyleRef tableBodiesRowStyleRef {};      ///< row holding the table's body sections, bounded to the table
        StyleRef tableScrollableBodyStyleRef {}; ///< a scrolling section body
        /// SectionSizing::FitContent section body.
        StyleRef tableFitContentBodyStyleRef {};
        StyleRef tableRowStyleRef {};     ///< row backdrop (odd rows)
        StyleRef tableRowAltStyleRef {};  ///< row backdrop (even rows)
        StyleRef tableRowBarStyleRef {};  ///< accent bar on a selected row's left
        StyleRef listRowStyleRef {};      ///< rounded row without stripes (tree views, TableState::rowStyle)
        /// Row-mode tree cell: transparent hover/selected, so the row backdrop shows them.
        StyleRef treeTableCellRowModeStyleRef {};
        /// The arrow's NoInput glyph, inside a full-row-height hit wrapper
        /// (treeTableArrowStyleRef) for forgiving clicks.
        StyleRef treeTableArrowGlyphStyleRef {};
        /// Inline-rename editor. Same box as treeTableTextStyleRef (both have a 1px border),
        /// so the text doesn't move when a rename starts.
        StyleRef treeTableTextEditStyleRef {};
        StyleRef tableHeaderStyleRef {};    ///< a column header
        /// Locked column headers: default cursor, since they can't be dragged.
        StyleRef tableHeaderLockedStyleRef {};
        StyleRef tableSizerStyleRef {};     ///< a column's resize handle
        StyleRef tableSeparatorStyleRef {}; ///< the divider between table sections
        StyleRef tableCellStyleRef {};      ///< a cell
        StyleRef tableCellNumberStyleRef {};  ///< right-aligned number cell (TableColumn::cellStyle)
        StyleRef tableCellCompactStyleRef {}; ///< tighter-padded cell for dense tables (opt-in via VirtualTable cellStyle)
        /// @name Tree table cells
        /// Tree column: [indent][arrow|spacer][icon?][text], guide lines custom-drawn.
        ///@{
        StyleRef treeTableCellStyleRef {};
        StyleRef treeTableIndentStyleRef {};
        StyleRef treeTableArrowStyleRef {};
        StyleRef treeTableIconStyleRef {};
        StyleRef treeTableTextStyleRef {};
        ///@}
        StyleRef nodeViewStyleRef {};       ///< NodeView: the canvas
        StyleRef viewBackgroundStyleRef {}; ///< solid backdrop (--bg-app) for the View panel's toolbar dock
        StyleRef nodeStyleRef {};       ///< single node container (vertical column with border + bg)
        StyleRef nodeHeaderStyleRef {}; ///< node title bar (distinct background, sits at top of node)
        StyleRef nodeCategoryStyleRef {}; ///< the category square in a node header
        StyleRef nodeTitleStyleRef {};    ///< a node header's title
        StyleRef nodeCaptionStyleRef {};  ///< a node header's category name, right-aligned
        StyleRef nodeViewOverlayStyleRef {};       ///< panel floating over the node view (breadcrumb, zoom bar)
        StyleRef nodeViewOverlayButtonStyleRef {}; ///< flat button in such a panel
        StyleRef nodeViewMinimapStyleRef {};       ///< the node view's minimap
        StyleRef breadcrumbStyleRef {};            ///< the breadcrumb path bar
        StyleRef breadcrumbSegmentStyleRef {};     ///< a clickable path segment
        StyleRef breadcrumbCurrentStyleRef {};     ///< the last segment
        StyleRef breadcrumbSeparatorStyleRef {};   ///< between two segments
        StyleRef axisLabelStyleRefs[4] {}; ///< vector fields: the label of each component (X, Y, Z, W)
        /// Width shared by the component labels: the widest name (X..W, R..A) with its label's
        /// padding and border, in pixels. Measured in ComputeRowMetrics.
        int32_t  axisLabelWidth = 0;
        StyleRef vectorComponentStyleRef {}; ///< a vector component's field
        StyleRef statusBarStyleRef {};       ///< BeginStatusBar's default
        StyleRef statusTextStyleRef {};      ///< text in the status bar
        /// @name Command search
        /// Command search: the box, its hint and keycap, the list of matches and its highlighted row.
        ///@{
        StyleRef commandSearchStyleRef {};
        StyleRef commandSearchHintStyleRef {};
        StyleRef commandSearchKeyStyleRef {};
        StyleRef commandSearchListStyleRef {};
        StyleRef commandSearchCurrentRowStyleRef {};
        ///@}
        StyleRef nodeRowStyleRef {};    ///< node property row (socket | body | socket)
        StyleRef nodeBodyCenterStyleRef {}; ///< centered body for full-body content (image previews)
        /// A socket's hit target; overflow:visible so the dot and its hover halo aren't clipped.
        StyleRef nodeSocketStyleRef {};
        /// .node-socket box size in pixels (read from CSS); the custom-drawn dot and the
        /// on-border insets derive from it.
        int      socketSize = 12;
        StyleRef nodeTextLeftStyleRef {};   ///< text-only property body, left-aligned (left dot only)
        StyleRef nodeTextCenterStyleRef {}; ///< text-only property body, centered (both dots)
        StyleRef nodeTextRightStyleRef {};  ///< text-only property body, right-aligned (right dot only)
        StyleRef itemListStyleRef {};       ///< a bordered scrolling list box
        StyleRef itemStyleRef {};           ///< an item in it
        StyleRef mainWindowStyleRef{};  ///< a window without padding, its content in a column
        StyleRef windowStyleRef {};     ///< BeginWindow's default
        StyleRef dockWindowStyleRef {}; ///< floating dock window - no internal padding
        StyleRef dockWindowLeafStyleRef {};     ///< a torn-off window's root leaf: the title bar over the panel
        StyleRef dockWindowPanelStyleRef {};    ///< the panel inset under a torn-off window's title bar
        StyleRef dockWindowBodyStyleRef {};     ///< a torn-off window's split layout, inset the same
        StyleRef titleBarTabBarStyleRef {};     ///< the tab strip in a torn-off window's title bar
        StyleRef titleBarTabStyleRef {};        ///< a tab in it
        StyleRef titleBarGripStyleRef {};       ///< the grip before it
        StyleRef titleBarTitleStyleRef {};      ///< the window's name when the tabs aren't in the bar
        StyleRef titleBarTextButtonStyleRef {}; ///< a text button in a title bar (Dock)
        StyleRef listAreaStyleRef {};       ///< undecorated scrolling column (BeginArea)
        StyleRef collectionAreaStyleRef {}; ///< card-like well behind the list & dictionary Field() helpers
        StyleRef typedFieldStyleRef {};                 ///< TypedField: the box
        StyleRef typedFieldInputStyleRef {};            ///< TypedField: the value
        StyleRef typedFieldTypeStyleRef {};             ///< TypedField: the type tag
        StyleRef collectionListStyleRef {};             ///< list field: the rows
        StyleRef collectionRowStyleRef {};              ///< list field: a row
        StyleRef collectionIndexStyleRef {};            ///< list field: a row's index
        StyleRef collectionRemoveStyleRef {};           ///< list / dictionary: remove
        StyleRef collectionAddStyleRef {};              ///< list field: Add item
        StyleRef collectionAddEntryStyleRef {};         ///< dictionary: Add entry
        StyleRef collectionEntryStyleRef {};            ///< dictionary: a row
        StyleRef collectionValueStyleRef {};            ///< dictionary: a value
        StyleRef collectionHeaderStyleRef {};           ///< dictionary: the header row
        StyleRef collectionHeaderCellStyleRef {};       ///< dictionary: Key / Value
        StyleRef collectionHeaderCaptionStyleRef {};    ///< dictionary: the caption
        StyleRef collectionHeaderTypeStyleRef {};       ///< dictionary: the type
        StyleRef groupLabelTopStyleRef {};              ///< Label with a caption: the container
        StyleRef groupLabelTitleStyleRef {};            ///< Label with a caption: the title
        StyleRef groupLabelCaptionStyleRef {};          ///< Label with a caption: the caption
        StyleRef iconButtonStyleRef {}; ///< compact flat icon button (a list / dictionary row's remove)
        StyleRef iconButtonEndStyleRef {}; ///< the same, end-aligned (the trailing add button)
        StyleRef menuStyleRef {};               ///< a menu's window
        StyleRef menuItemStyleRef{};            ///< a menu item
        StyleRef menuScrollButtonStyleRef{};    ///< the scroll up / down arrows of a menu taller than the screen
        StyleRef menuBarStyleRef {};        ///< horizontal strip at the top of the main window
        StyleRef menuBarTriggerStyleRef {}; ///< flat top-level trigger inside the menu bar
        StyleRef titleBarStyleRef {};            ///< a window's own title bar (BeginTitleBar)
        StyleRef titleBarButtonStyleRef {};      ///< its minimize and maximize buttons
        StyleRef titleBarCloseButtonStyleRef {}; ///< its close button
        StyleRef menuSeparatorStyleRef {};       ///< a divider between menu items
        StyleRef menuHeaderStyleRef {};     ///< a section's name in a menu
        StyleRef reorderDropSlotStyleRef {};   ///< a list row's drop slot while it's dragged
        StyleRef reorderLiftedStyleRef {};     ///< the dragged row itself
        StyleRef menuItemDestructiveLabelStyleRef {};   ///< a destructive item's label (.menu-item-label-destructive)
        StyleRef menuItemDestructiveIconStyleRef {};    ///< a destructive item's icon (.menu-item-icon-destructive)
        StyleRef menuItemRowStyleRef {};      ///< interactive row: icon slot + label + trailing shortcut/arrow
        StyleRef menuItemIconStyleRef {};     ///< fixed icon slot inside a menu item row
        StyleRef menuItemLabelStyleRef {};    ///< grow:1 label inside a menu item row
        StyleRef menuItemShortcutStyleRef {}; ///< dim trailing keyboard-shortcut text
        StyleRef menuItemArrowStyleRef {};    ///< dim trailing submenu arrow
        StyleRef tooltipStyleRef {};          ///< a tooltip's window
        StyleRef tooltipTextStyleRef {};      ///< its text
        StyleRef groupLabelStyleRef {};       ///< Label() inside a label group
        StyleRef groupDragLabelStyleRef {};   ///< DragLabel() inside a label group
        StyleRef collectionStyleRef {};       ///< BeginArea / BeginCached default
        /// @name Reorderable groups
        /// BeginReorderableItem's default and the reorderable group per direction, with and
        /// without wrapping. CSS classes in camelCase: .reorderableItem, .horzReorderableGroup ...
        ///@{
        StyleRef reorderableItemStyleRef{};
        StyleRef horzReorderableGroupStyleRef{};
        StyleRef vertReorderableGroupStyleRef{};
        StyleRef horzReorderableGroupWrapStyleRef{};
        StyleRef vertReorderableGroupWrapStyleRef{};
        ///@}
        StyleRef listViewStyleRef {};         ///< BeginListView's default
        StyleRef listItemStyleRef {};         ///< ListItem's default
        StyleRef listItemRowStyleRef {};      ///< reorderable list item outer container
        StyleRef listItemDragKnobHorzStyleRef {}; ///< reorderable list item drag handle (horizontal list - drag along x)
        StyleRef listItemDragKnobVertStyleRef {}; ///< reorderable list item drag handle (vertical list - drag along y)
        StyleRef listItemBodyStyleRef {};     ///< reorderable list item text/icon body
        StyleRef treeViewStyleRef {};         ///< a tree view
        StyleRef gridViewStyleRef {};         ///< BeginGridView's default
        StyleRef gridViewItemStyleRef {};     ///< a grid view cell, by default
        StyleRef gridViewScrollStyleRef {};   ///< virtualized grid scroll viewport (column)
        StyleRef gridViewRowStyleRef {};      ///< one row strip of cells in the virtual grid
        StyleRef gridViewListItemStyleRef {}; ///< list-mode cell: icon left, name right, one line
        StyleRef gridViewRenameItemStyleRef {};     ///< rename-mode cell: icon above inline editor
        StyleRef gridViewRenameListItemStyleRef {}; ///< rename-mode list row: icon left of editor
        StyleRef gridViewRenameIconStyleRef {};     ///< the icon child of a renaming cell
        StyleRef gridViewRenameEditStyleRef {};     ///< grid-mode inline editor (full cell width)
        StyleRef flexFillRowStyleRef {};      ///< horizontal flex bounded to its parent (flex-basis:0)
        StyleRef flexFillColStyleRef {};      ///< vertical flex bounded to its parent (flex-basis:0)
        StyleRef flatButtonStyleRef {};       ///< button that reads as plain left-aligned text (no chrome)
        StyleRef searchRowStyleRef {};         ///< full-width row hosting a top-of-panel SearchField (bottom spacing)
        /// @name Search field
        /// SearchField: the textbox chrome container, its icon, and the borderless inner field.
        ///@{
        StyleRef searchTextboxStyleRef {};
        StyleRef searchTextboxIconStyleRef {};
        StyleRef searchTextboxFieldStyleRef {};
        ///@}
        /// Parsed .grid-view-item, for cloning with another iconSize (registering is deduped).
        Style    gridViewItemBaseStyle {};
        /// Line height of a grid view cell's name in pixels, measured in ComputeRowMetrics so
        /// names never clip.
        int32_t  gridViewItemLineHeight = 0;
        int32_t  gridViewListRowHeight  = 0; ///< a list-mode grid view row, in pixels (ComputeRowMetrics)
        /// @name Tabs
        /// A tab bar, a tab, a tab's icon and text, and its close button.
        ///@{
        StyleRef tabBarStyleRef {};
        StyleRef tabStyleRef {};
        StyleRef tabOverflowStyleRef {};   ///< the "+N" button after the tabs that fit
        StyleRef tabContentStyleRef {};
        StyleRef tabCloseStyleRef {};
        ///@}

        StyleRef dockContainerStyleRef {};       ///< a docking container
        StyleRef dockContainerMainStyleRef {};   ///< a main window's docking area, with a side gutter
        StyleRef dockSplitHorzStyleRef {};   ///< horizontal flex row (split into left / right)
        StyleRef dockSplitVertStyleRef {};   ///< vertical flex column (split into top / bottom)
        StyleRef dockSizerHorzStyleRef {};   ///< vertical bar between left / right panes
        StyleRef dockSizerVertStyleRef {};   ///< horizontal bar between top / bottom panes
        StyleRef dockLeafStyleRef {};        ///< vertical flex: [tab bar][content]
        StyleRef dockContentStyleRef {};     ///< content area holding the selected panel's draw()

        StyleRef settingsPageSelectionRef {};   ///< the settings page list (.settings-page-selection)
        StyleRef settingsPageStyleRef     {};   ///< settings page body - dock-content layout + left padding off the sizer

        /// @name Built-in icons
        /// Registered by InitializeDefaultStyles.
        ///@{

        IconID treeArrowRight    = IconID::Invalid; ///< tree expander: collapsed
        IconID treeArrowDown     = IconID::Invalid; ///< tree expander: expanded

        IconID sortArrowUpIcon = IconID::Invalid;   ///< table header: sorted descending
        IconID sortArrowDownIcon = IconID::Invalid; ///< table header: sorted ascending

        IconID tabCloseIcon = IconID::Invalid; ///< a tab's close button
        IconID menuCheckIcon = IconID::Invalid; ///< marks the current/checked item in a menu
        IconID dropdownArrowIcon = IconID::Invalid; ///< after a dropdown's text (menu triggers outside a menu bar)
        IconID dropdownArrowUpIcon = IconID::Invalid; ///< the same while its menu is open
        IconID chevronLeftIcon   = IconID::Invalid; ///< previous
        IconID duplicateIcon     = IconID::Invalid; ///< duplicate
        IconID deleteIcon        = IconID::Invalid; ///< delete (trash can)
        IconID reverseIcon       = IconID::Invalid; ///< gradient editor: Reverse
        IconID distributeIcon    = IconID::Invalid; ///< gradient editor: Distribute evenly

        IconID addIcon    = IconID::Invalid; ///< list / dictionary Field(): add a row
        IconID removeIcon = IconID::Invalid; ///< list / dictionary Field(): remove a row

        IconID dragHandleIcon = IconID::Invalid;  ///< three-bar grip of a reorderable list / tree row
        IconID dockIcon = IconID::Invalid;        ///< returns a torn-off panel to its dock
        IconID eyedropperIcon = IconID::Invalid;  ///< picks a color from the screen
        IconID alertIcon      = IconID::Invalid;  ///< message severity: warning
        IconID powerIcon      = IconID::Invalid;  ///< message severity: quitting
        IconID questionIcon   = IconID::Invalid;  ///< message severity: question
        IconID infoIcon       = IconID::Invalid;  ///< message severity: information
        IconID errorIcon      = IconID::Invalid;  ///< message severity: error
        IconID chevronRightIcon = IconID::Invalid; ///< next; a collapsed section
        IconID searchIcon     = IconID::Invalid;  ///< SearchField
        IconID windowMinimizeIcon = IconID::Invalid; ///< title bar: minimize
        IconID windowMaximizeIcon = IconID::Invalid; ///< title bar: maximize
        IconID windowRestoreIcon  = IconID::Invalid; ///< title bar: restore
        ///@}

        // Line colors, from the palette's custom properties.
        tachyon::VisualColorStyleIndex lineGrayStyle;          ///< --text-3
        tachyon::VisualColorStyleIndex lineYellowStyle;        ///< --warn
        tachyon::VisualColorStyleIndex lineWhiteStyle;         ///< --text-1
        tachyon::VisualColorStyleIndex lineDarkStyle;          ///< --bg-app
        tachyon::VisualColorStyleIndex treeGuideLineStyle;     ///< --line-node: tree guide lines
        tachyon::VisualColorStyleIndex dividerHoverLineStyle; ///< a hovered divider drawn as a line (label sizer)
        tachyon::VisualColorStyleIndex nanTextColor;          ///< a number field's NaN
        tachyon::color4b               scrim {};              ///< dims a window under its modal dialog (fading in and out)
        tachyon::VisualColorStyleIndex checkerAColor, checkerBColor;     ///< transparency checkerboard
        tachyon::VisualColorStyleIndex alphaTrackColor, alphaFillColor;  ///< a color's alpha bar
        tachyon::VisualColorStyleIndex accentColor, panelColor;          ///< a selected swatch's ring and gap
        tachyon::VisualColorStyleIndex treeDropIndicatorStyle; ///< drop indicator lines + closed-branch outline

        tachyon::VisualColorStyleIndex marqueeFillColorStyle; ///< selection-marquee fill (fixed translucent blue)

        /// Table row height in pixels, measured from real font metrics in ComputeRowMetrics.
        /// Must match the rendered rows exactly, or the max scroll oscillates.
        int32_t tableRowHeight = 0;

        /// @name Tree table metrics
        /// Pixels, read from the stylesheet in ComputeRowMetrics; a theme that omits one gets
        /// the measured text line height.
        ///@{
        int32_t treeTableIndent        = 0; ///< per indent level
        int32_t treeTableExpanderWidth = 0; ///< the expander column (arrow or leaf spacer)
        int32_t treeTableIconSize      = 0; ///< a node's icon
        int32_t treeTableArrowGlyphSize = 0; ///< the expander arrow
        ///@}

        /// Node-view grid pitch: half a property row, measured in ComputeRowMetrics.
        int32_t nodeGridSize = 0;

    public:
        /// Registers the styles built in code (the fallback for an invalid style index,
        /// fieldElementGroupStyleRef, the empty*StyleRefs) and the built-in icons; textSystem
        /// must be valid. Call before LoadTheme, and again after StyleManager::ClearAll.
        void InitializeDefaultStyles(StyleManager& styleManager, ITextSystem* textSystem);
        /// Parses the palette file `themePath` followed by `styleFiles` (in order; relative
        /// paths are next to the palette file) and registers every class, the palette colors
        /// and, when `textSystem` is given, the `@font-face` families. `fontScale` scales every
        /// font size. Parse errors are logged and skipped; false when a file can't be opened.
        bool LoadTheme(StyleManager& styleManager, ITextSystem* textSystem, const std::string& themePath,
                       std::span<const std::string> styleFiles, float fontScale);

        /// The style of a CSS class from the loaded style files (library or app); an invalid
        /// style, with a warning once per class, when no file defines it.
        StyleRef         GetStyle(std::string_view cssClass) const;
        /// As GetStyle, but a class no file defines is a NotFound error.
        Result<StyleRef> FindStyle(std::string_view cssClass) const;

        /// A palette color by custom-property name ("--node-input"), or `fallback`
        /// when the loaded theme doesn't define it.
        color4f GetColor(std::string_view varName, color4f fallback = {}) const;

        /// Measures the font-dependent metrics above. Call after every LoadTheme.
        void ComputeRowMetrics(StyleManager& styleManager, ITextSystem& textSystem);
    private:
        std::unordered_map<std::string, color4f> m_paletteColors;
        std::map<std::string, StyleRef, std::less<>> m_classStyles;  ///< every class in the style files
        mutable std::set<std::string, std::less<>>   m_missingWarned;
    };

    /// @}
} // namespace tachyon
