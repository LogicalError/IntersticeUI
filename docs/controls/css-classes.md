# Default CSS classes

The class each control uses when no style is passed. Parts of a control (a switch's
knob, a table's header) have their own classes, listed with the control above.

| Control | Class |
| --- | --- |
| `BeginWindow` | `window` |
| `BeginGhostWindow`, `AddGhostIcon` | `ghost-window`, `ghost-icon` |
| `BeginHorizontal`, `BeginVertical` and Wrap variants | none (plain flex box built in code) |
| `BeginArea`, `BeginCached` | `collection` |
| `BeginButtonGroup`, `BeginToggleGroup` | `button-group`, `toggle-group`; items `group-button`, `group-toggle` (`button`, `toggle` when alone) |
| `Text` | `static-text` |
| `Label`, `DragLabel` | `label`, `drag-label` (`group-label`, `group-drag-label` in a resizable label group) |
| `Image` | none (built in code) |
| `Button`, `CommandButton` | `button` |
| `Toggle`, `Field(bool&)` | `toggle` |
| `Switch` | `switch` |
| `Checkbox` | `checkbox` |
| `Field(std::string&)` | `textbox` |
| `SearchField` | `search-textbox` |
| `Dropdown` | `menu-trigger`, popup `menu` |
| `AutocompleteField` | `textbox`; list `command-search-list`, rows `menu-item-row`, highlighted row `command-search-current-row` |
| `Field` (numbers) | `number-field` |
| `Field` (vectors) | `vector-component`, `axis-label-x` / `-y` / `-z` / `-w` |
| `TypedField` | `typed-field` |
| `Field` (lists, dictionaries) | `collection-area` |
| `SliderField` | `slider-field-track` |
| `Slider` | `slider` |
| `ColorField` | `color-field` (picker: `color-picker`) |
| `GradientField` | `gradient-swatch` (editor: `gradient-editor`) |
| `BeginMenu` | `menu-trigger`, popup `menu` |
| `MenuItem` | `menu-item-row` |
| `MenuSeparator`, `MenuHeader` | `menu-separator`, `menu-header` |
| `BeginMenuBar`, `BeginTitleBar` | `menu-bar`, `title-bar` |
| `BeginContextMenu` | `menu` |
| `SetTooltip`, `BeginTooltip` | `tooltip` (text: `tooltip-text`) |
| `MessageDialog` | `message-box` |
| `BeginListView`, `ListItem` | `list-view`, `list-item` |
| `BeginReorderableGroup` | `horzReorderableGroup`, `vertReorderableGroup` (`...Wrap` when wrapped) |
| `BeginReorderableItem` | `reorderableItem` |
| `BeginTable`, `VirtualTable` | `table` (cells: `table-cell`) |
| `VirtualTreeView` | `tree-view` |
| `BeginGridView`, `GridViewItem` | `grid-view`, `grid-view-item` |
| `VirtualGridView` | `grid-view-scroll`, `grid-view-row` |
| `BeginTabBar`, `Tab` | `tab-bar`, `tab`, `tab-overflow` |
| `BeginDockingContainer` | `dock-container` |
| `BeginToolbar` | controls inside use `toolbar-*` variants |
| `Breadcrumb` | `breadcrumb` |
| `BeginStatusBar`, `StatusText` | `status-bar`, `status-text` |
| `CommandSearch` | `command-search` |
| `RenderSettingsPanel` | `settings-page` |
| `RenderNodeView` | `node-view` |
| Links in rich text (`theme.linkStyle`) | `link` (`color`, `:hover` `color`, `cursor`) |

The classes are defined in `workdirectory/themes/styles.css`, with colors from the
palette in `dark.css` or `light.css`. See [CSS and theming](../css.md) for how to change them or add
your own.

---

[← Custom drawing](custom-drawing.md) · [Controls](../controls.md)
