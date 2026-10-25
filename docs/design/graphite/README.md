# Graphite theme

- `graphite-theme-spec.md` — the full spec: colors, metrics, per-control rules, documents/docking, states & motion, dark/light table.
- `tokens/graphite-tokens.json` — every color, metric and motion value for both themes (source of truth).
- `tokens/graphite-dark.css`, `tokens/graphite-light.css` — the same tokens as CSS custom properties.
- `graphite-dark.png`, `graphite-light.png` — overview sheets of everything (large; use the per-piece images below for detail).
- `images/` — one image per design piece and close-up crops of individual panels, for both themes (listed below).
- `mockups/` — source of every design board (`*.dc.html`). Styles are inline, so exact values can be read from them;
  they need the design canvas runtime to render, so use the images to look at them.

Token names are suggestions; map them onto the style names in your own `themes/*.css`.

## Images

### Dark

- `images/dark/Main.png` — **Main window — document tabs + panels**: Full main window: title bar, colored document tabs, Hierarchy, NodeView, Profiler, Files, Log, status bar
  - `images/dark/panels/Main--Hierarchy.png` — Main window — document tabs + panels › Hierarchy panel
  - `images/dark/panels/Main--NodeView.png` — Main window — document tabs + panels › NodeView panel
  - `images/dark/panels/Main--Profiler.png` — Main window — document tabs + panels › Profiler panel
  - `images/dark/panels/Main--Files.png` — Main window — document tabs + panels › Files panel
  - `images/dark/panels/Main--Log.png` — Main window — document tabs + panels › Log panel
- `images/dark/Inspector.png` — **Viewport + Inspector panels**: Viewport (floating toolbars, gizmo) + Inspector panel (all field types, color field with alpha, text selection)
  - `images/dark/panels/Inspector--Viewport.png` — Viewport + Inspector panels › Viewport panel
  - `images/dark/panels/Inspector--Inspector.png` — Viewport + Inspector panels › Inspector panel
- `images/dark/States.png` — **States & transitions**: State matrix for 12 controls x 7 states, transition timings, state-layer graph, Oklab vs sRGB
- `images/dark/Timeline.png` — **Timeline**: Timeline panel: transport, tracks, keys, loop range, audio clip, events, playhead
- `images/dark/CurveEditor.png` — **Curve editor**: Curve editor: channels, grid, bezier curves, selected key with tangent handles
- `images/dark/Tooltips.png` — **Tooltips**: Tooltip variants: simple + shortcut, rich help, truncation, validation, disabled reason, node pin
- `images/dark/Progress.png` — **Progress & loading**: Progress bars, multi-step, spinners, circular, skeletons, status bar progress, background tasks
- `images/dark/Notifications.png` — **Notifications & confirmations**: Toasts, destructive confirmation dialog, inline panel banners
- `images/dark/Settings.png` — **Settings + Table panels**: Settings panel (shortcuts with keycaps) + Table panel
  - `images/dark/panels/Settings--Settings.png` — Settings + Table panels › Settings panel
  - `images/dark/panels/Settings--Table.png` — Settings + Table panels › Table panel
- `images/dark/DocumentWindow.png` — **Second main window — dragged-out document**: Second main window holding a dragged-out document (Hero.prefab) with its own layout
  - `images/dark/panels/DocumentWindow--Prefab-hierarchy.png` — Second main window — dragged-out document › Prefab hierarchy panel
  - `images/dark/panels/DocumentWindow--Prefab-viewport.png` — Second main window — dragged-out document › Prefab viewport panel
  - `images/dark/panels/DocumentWindow--Animation.png` — Second main window — dragged-out document › Animation panel
  - `images/dark/panels/DocumentWindow--Inspector.png` — Second main window — dragged-out document › Inspector panel
- `images/dark/Docking.png` — **Torn-off panel window**: Torn-off panel as its own OS window
- `images/dark/ColorPicker.png` — **Color picker**: Color picker dialog
- `images/dark/Tokens.png` — **Tokens & metrics**: Token sheet: colors, spacing scale, metrics, type
- `images/dark/GradientEditor.png` — **Gradient editor**: Gradient editor dialog
- `images/dark/InspectorLists.png` — **Lists, dictionary, menus**: Inspector lists (string/float with drag reorder), dictionary, dropdowns with an open menu
- `images/dark/MessageBox.png` — **Message boxes (yes / no / cancel)**: Message boxes: save/don't save/cancel, confirm quit, question with verb buttons, error with details

### Light

- `images/light/Main.png` — **Main window — document tabs + panels**: Full main window: title bar, colored document tabs, Hierarchy, NodeView, Profiler, Files, Log, status bar
  - `images/light/panels/Main--Hierarchy.png` — Main window — document tabs + panels › Hierarchy panel
  - `images/light/panels/Main--NodeView.png` — Main window — document tabs + panels › NodeView panel
  - `images/light/panels/Main--Profiler.png` — Main window — document tabs + panels › Profiler panel
  - `images/light/panels/Main--Files.png` — Main window — document tabs + panels › Files panel
  - `images/light/panels/Main--Log.png` — Main window — document tabs + panels › Log panel
- `images/light/Inspector.png` — **Viewport + Inspector panels**: Viewport (floating toolbars, gizmo) + Inspector panel (all field types, color field with alpha, text selection)
  - `images/light/panels/Inspector--Viewport.png` — Viewport + Inspector panels › Viewport panel
  - `images/light/panels/Inspector--Inspector.png` — Viewport + Inspector panels › Inspector panel
- `images/light/States.png` — **States & transitions**: State matrix for 12 controls x 7 states, transition timings, state-layer graph, Oklab vs sRGB
- `images/light/Timeline.png` — **Timeline**: Timeline panel: transport, tracks, keys, loop range, audio clip, events, playhead
- `images/light/CurveEditor.png` — **Curve editor**: Curve editor: channels, grid, bezier curves, selected key with tangent handles
- `images/light/Tooltips.png` — **Tooltips**: Tooltip variants: simple + shortcut, rich help, truncation, validation, disabled reason, node pin
- `images/light/Progress.png` — **Progress & loading**: Progress bars, multi-step, spinners, circular, skeletons, status bar progress, background tasks
- `images/light/Notifications.png` — **Notifications & confirmations**: Toasts, destructive confirmation dialog, inline panel banners
- `images/light/Settings.png` — **Settings + Table panels**: Settings panel (shortcuts with keycaps) + Table panel
  - `images/light/panels/Settings--Settings.png` — Settings + Table panels › Settings panel
  - `images/light/panels/Settings--Table.png` — Settings + Table panels › Table panel
- `images/light/DocumentWindow.png` — **Second main window — dragged-out document**: Second main window holding a dragged-out document (Hero.prefab) with its own layout
  - `images/light/panels/DocumentWindow--Prefab-hierarchy.png` — Second main window — dragged-out document › Prefab hierarchy panel
  - `images/light/panels/DocumentWindow--Prefab-viewport.png` — Second main window — dragged-out document › Prefab viewport panel
  - `images/light/panels/DocumentWindow--Animation.png` — Second main window — dragged-out document › Animation panel
  - `images/light/panels/DocumentWindow--Inspector.png` — Second main window — dragged-out document › Inspector panel
- `images/light/Docking.png` — **Torn-off panel window**: Torn-off panel as its own OS window
- `images/light/ColorPicker.png` — **Color picker**: Color picker dialog
- `images/light/Tokens.png` — **Tokens & metrics**: Token sheet: colors, spacing scale, metrics, type
- `images/light/GradientEditor.png` — **Gradient editor**: Gradient editor dialog
- `images/light/InspectorLists.png` — **Lists, dictionary, menus**: Inspector lists (string/float with drag reorder), dictionary, dropdowns with an open menu
- `images/light/MessageBox.png` — **Message boxes (yes / no / cancel)**: Message boxes: save/don't save/cancel, confirm quit, question with verb buttons, error with details
