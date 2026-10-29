# Graphite dark theme — spec

Reference mockups: `Main.dc.html` (NodeView + Profiler), `Inspector.dc.html` (Viewport + Inspector),
`Settings.dc.html` (Settings + Table), `Tokens.dc.html` (token sheet). They are HTML mockups with inline styles;
every value below is taken from them. Token names are suggestions — map them onto the existing `themes/dark.css` style names.

## Colors

### Surfaces (darkest → lightest)
| Token | Hex | Use |
|---|---|---|
| bg-app | #0B0C0F | Window background, gutters between panels, title/status bar |
| bg-input | #0D0F12 | Text fields, number fields, segmented-control wells, graph backgrounds |
| bg-canvas | #0E1013 | NodeView canvas (dot grid #252A33, 1px dots every 16px) |
| bg-panel | #121418 | Panel body |
| bg-header | #16191E | Panel header / tab strip, table header row |
| bg-raised | #181B20 | Nodes, keycaps (#1A1D23) |
| bg-control | #1E2128 | Buttons, node header |
| bg-active | #23272F | Active tab, active toggle button |
| bg-pressed | #2A2F39 | Selected segment in segmented control |
| row-stripe | #14161B | Odd rows in large tables (optional) |

### Lines
| Token | Hex | Use |
|---|---|---|
| panel-border | #1C1F25 | 1px outline around each panel |
| line-subtle | #23272F | Dividers, header bottom border, input border |
| line-node | #2A2F39 | Node border, node header divider |
| line-strong | #2E333D | Button border, scrollbar thumb |

### Text
| Token | Hex | Use |
|---|---|---|
| text-1 | #E8EAEF | Values, active labels, titles |
| text-2 | #AEB4C0 | Default UI text, property labels |
| text-3 | #8E96A7 | Captions, placeholders, timestamps, column headers (≥4.5:1 on every surface above) |
| text-link | #8FB3FF | Links (hover #B5CDFF) |

### Accent (single, user-tweakable)
| Token | Value | Use |
|---|---|---|
| accent | #6B9BFF | Active tab icon, toggles, slider fill, selected node border, focus border |
| accent-soft | accent @ 16% alpha | Selected row/item background, focus halo (3px) |
| on-accent | #0B0C0F | Text/icons on accent fills (dark text, not white) |

Alternate accents tested: #34D1BF, #A98BFF, #F59E4B.

### Semantic
| Token | Hex |
|---|---|
| ok | #4FC48A |
| warn | #E5B454 (also: NaN values, over-budget frames) |
| error | #EF6A6A (also: recording dot) |
| info | #6B9BFF |
| folder icon | #D9A443 |

### Data types / axes
| Token | Hex |
|---|---|
| pin-float | #34D1BF |
| pin-int | #F59E4B |
| pin-bool | #A98BFF |
| pin-color | #F472B6 |
| axis-x / y / z | #EF6A6A / #4FC48A / #6B9BFF |
| axis label bg | axis color @ 18% alpha, text X #F28B8B, Y #6FD3A1, Z #8FB3FF |

### Profiler flame chart fills (text #E8EAEF)
Frame #2B303B · Build/Layout #5A3B2E · Render/Record #3E3560 · OnGUI/EndFrame/BeginFrame #24505A · Window render #2E4A3A.
Frame graph bars #3E8F6B, over-budget #E5B454, current frame = accent; 16.7 ms budget line dashed #5A606C.

## Typography
- UI: Inter (fallback system-ui), 12px / 16px line height. Medium (500) for active tabs, 600 for titles and section headers.
- Captions: 11px, text-3.
- Section headers in Inspector: 11.5px / 600, text-1.
- Page title (Settings): 16px / 600.
- Numbers, timestamps, hex, keycaps: JetBrains Mono 11–11.5px. Right-align numeric table columns.

## Spacing scale (4px grid)
2 · 4 · 6 · 8 · 12 · 16 · 24 · 32

## Metrics
| Element | Size |
|---|---|
| Title + menu bar (merged) | 36 high; menu buttons 26 high, 9px horizontal padding, radius 4 |
| Command search box | 380 × 26, radius 6 |
| Window buttons | 44 × 36 |
| Status bar | 26 high, 11px text-3, 12px side padding, 16px gap |
| Panel gutter | 4 (window padding 0 4px; 4px between panels) |
| Panel | radius 6, 1px panel-border, overflow clipped |
| Panel header | 34 high, padding 0 4 0 5, tabs gap 2 |
| Tab | 24 high, padding 0 10, radius 4, icon 14 + 6 gap; active = bg-active + text-1 + accent-colored icon |
| Tab overflow | show first N tabs, then "+N ▾" button instead of wrapping to a second row; the tab strip's context menu can put a group's tabs on several rows instead |
| Icon | 14px, stroke 2, round caps/joins (Lucide-style) |
| Icon button | 24 × 24, radius 4 |
| Control height | 24 (inputs, buttons, number fields), radius 4, padding 0 8 |
| Search field in panel | 26 high, 8px panel padding around it |
| Tree row | 22 high, indent 16 per level, chevron slot 16, icon 14, radius 4 on selection |
| Table row | 22 (profiler), 24 (data table), 27 (shortcuts list) |
| Table header | 26–28 high, bg-header, 11px / 500 text-3 |
| Log row | 24 high; columns 96 (time) / 16 (level dot 7px) / rest |
| Inspector row | 26 high, label column 116, 12px gap, 12px side padding, 4px row gap |
| Inspector section header | 30 high, collapsible chevron, 1px #1E2128 top divider |
| Segmented control | well bg-input + 1px line-subtle, 2px inset, segments 20–22 high |
| Toggle switch | 28 × 16, knob 12; on = accent with dark knob; off = #2E333D with #AEB4C0 knob |
| Slider | track 4 high #2A2F39, fill accent, thumb 14 (#E8EAEF with 3px accent border) |
| Focus | 1px accent border + 3px accent-soft halo |
| Scrollbar | 4px thumb, radius 2, #2E333D, no track |
| Keycap | min 20 × 20, 1px #2E333D border (2px bottom), bg #1A1D23, mono 10.5px |

## NodeView
- Node: radius 6, bg-raised, 1px line-node, shadow 0 8 20 rgba(0,0,0,.35).
- Header 30 high, bg-control: 8px category square (radius 2) + title 600 + category caption right-aligned.
  Categories: generator #E0716B, operator = accent, input #4FC48A, output #A98BFF.
- Body padding 6 top/bottom; pin rows 26, field rows 30, side padding 14.
- Pin 10px, centred on the node edge; connected = filled, unconnected = 2px ring; 2px bg-raised outline.
- Wires 2px, bezier, colored by source pin type.
- Selected node: 1px accent border + 3px accent-soft halo.
- Overlays (8–12px inset, bg-header, radius 6): breadcrumb top-left, zoom −/100%/+/Fit bottom-left, minimap 140 × 88 bottom-right.

## Color field (Inspector)
- Swatch button 56×24, radius 4, 1px line-strong border, drawn on the checkerboard.
- Left half: color fully opaque. Right half: color at its actual alpha over the checkerboard.
- 3px alpha bar along the bottom edge: track #0B0C0F (dark) / #464C57 (light), fill text-1 (dark) / #FFFFFF (light), width = alpha.
- Followed by hex field and an alpha field ("A" caption left, value right, e.g. 60%).

## Text selection
| Token | Dark | Light |
|---|---|---|
| selection (focused) | accent @ 35% (rgba(107,155,255,.35)) | accent @ 25% (rgba(47,111,235,.25)) |
| selection-inactive (unfocused field / window) | #3A3F4A | #DCDFE5 |
| caret | 2px accent, 14px tall | 2px accent, 14px tall |
- Selected text keeps text-1; never invert to white-on-accent (keeps mono digits and syntax colors readable).
- Multi-line: each fully selected line extends ~4px past its last glyph to show the selected newline.
- Read-only text (log details, labels that allow copy) uses the same focused selection color.
- Numeric fields select all on focus (see Dictionary "100").
- Selection color derives from the accent, so it follows accent changes.

## Lists, dictionary, menus (Inspector)
Mockup: `InspectorLists.dc.html` (prop `theme`).
- Label column stays 116px but is **top-aligned** to the first row (padding-top 9), with a count caption under it (11px text-3: "3 items").
- List container: 4px padding, 2px row gap, 1px line-subtle border, radius 6, bg #101216 (dark) / #F6F7F9 (light).
- List row 26: grid 14 (grip, 6-dot, text-3) | 14 (index, mono 10.5 text-3, right-aligned) | field | 22 (remove ×, text-3).
  Hover row: bg #232529 / #E9E9EB (--text-1 at 8% over the panel), remove turns error color on an error @ 12% chip.
- Numeric list fields: mono value + type tag right (f32), same as the Numbers section.
- Add button: inside the container, aligned with the fields (margin 2 24 0 42), 24 high, 1px dashed line-strong, "+ Add item".
- Drag reorder: source row stays at 35% opacity; dragged copy floats with bg-control/white, drop shadow and 1px accent ring, grip in accent;
  drop target = a 26px slot opened between rows (1px dashed accent border on accent-soft, radius 4), the dragged copy hovering over it offset ~6px.
- Dictionary: same container with a 20px header row (Key / Value captions + mono type: string, i32), no grip/index,
  key field (UI font) | value field (mono) | remove; "+ Add entry" button spans the key+value columns.
- Dropdown button: 160 × 24, bg-control, 1px line-strong, label left, chevron right (text-3).
  Open: accent border + 3px accent-soft halo, chevron flips up.
- Menu popup: 264 wide, 4px padding, radius 8, bg #1A1D23 / #FFFFFF, 1px #2E333D / #D5D9E0, shadow 0 16 40 rgba(0,0,0,.55) / 0 12 32 rgba(16,24,40,.16).
  Opens 6px below the button. Optional filter field on top for big menus.
  Item 26: grid 16 (icon or check) | label | shortcut (mono 10.5 text-3) | 12 (submenu chevron). Hover = accent-soft.
  Section header 22 (11px/500 text-3, indented to label column). Separator 1px line-subtle with 4px margin.
  Checked item: accent check icon; unchecked: empty icon slot. Disabled: 45% opacity. Destructive: error color text + icon.

## Torn-off panel window
Mockup: `Docking.dc.html` (prop `theme`).
A tab dragged out of a dock becomes its own OS window, holding only that panel.
- Window: bg-app, 1px #2E333D / #C9CED6 border, radius 8 (where the OS allows), shadow 0 24 56 rgba(0,0,0,.55) dark / 0 20 48 rgba(16,24,40,.22) light.
- Title bar 36 (custom chrome, no menu bar): 6-dot grip (text-3) + the panel's tab (26 high, active-tab style; light theme uses bg-panel so the tab joins the panel)
  + spacer (drag area) + "Dock" text button (returns the panel to its last dock position) + 44×36 minimize / maximize / close.
- The panel sits inset 4px (left, right, bottom) inside the window, same panel styling as when docked (bg-panel, panel-border, radius 6).
- Resize grip (two diagonal strokes, text-3) in the bottom-right corner.
- The tab strip in the title bar accepts more dragged tabs, so a torn-off window can host several panels.

## Tooltips
Mockup: `Tooltips.dc.html`.
- Simple: 12px text, padding 5×8, radius 6, 8px arrow (rotated square) toward the anchor, 6–10px gap from the anchor.
  Dark: bg #2A2F39, 1px #363C48, text-1. Light: inverted — bg #1A1D23, white text (no border).
  Shortcut chip inside: mono 10.5, bg white @ 8% (dark) / 12% (light), muted text.
- Rich (property help): popup surface (bg-popup, popup border, radius 8, popup shadow), 268 wide, padding 10×12:
  title 600 · description text-2 · divider · mono 10.5 text-3 footer (API path · type). Labels with rich help get a dotted underline.
- Truncated text: an expansion tooltip drawn exactly over the truncated text, same font, so it reads as the text growing.
- Validation: popup surface with a 1px error border (arrow too); the field keeps its error border + 3px error @ 16% halo and an error icon.
- Disabled controls get a tooltip that says why they are disabled (second line, muted).
- Node pins: name · mono type · live value, second line = connection source.
- Timing: show after 500 ms hover; instant when moving between tooltipped items; hide after 100 ms; never on touch-drag.

## Timeline
Mockup: `Timeline.dc.html` (1240 panel).
- Toolbar 40: transport (start, prev key, play = accent fill, next key, end, loop = pressed toggle), time display in a well
  (mono 13/500 current, text-3 total, frame chip), fps dropdown, Snap toggle, zoom slider right-aligned.
- Track list 240: header 28 (Tracks caption, filter, +). Rows 28 with 1px row lines (#181B20 / #EEF0F3).
  Group rows: chevron, 8px category chip, 600 name, eye + lock. Property rows indented to 34 with an add-key diamond on the right.
  Audio rows: M / S chips (solo uses warn @ 18%). Selected track = accent-soft on both the list row and its lane.
- Lanes are inset 12px left and right so keys at the range ends are fully visible.
- Ruler 28 (bg-header): major tick every second (10px, text-3) with mono 10.5 labels, minor ticks every quarter second (5px).
  Loop range = accent-soft band with 2px accent edges on the ruler; outside the range the lanes are dimmed (black @ 28% / navy @ 5%).
- Keys: 10px diamonds, fill #C9CED8 / #5F6673, 1px panel-colored edge. Selected: 11px accent with light edge.
  Group summary keys: 8px, #6B7280 / #9AA1AD. Box selection: 1px dashed accent on accent-soft.
- Audio clip: teal @ 10% fill, teal @ 45–55% border, radius 4, waveform bars teal @ 65–70%.
- Event markers: 2px orange line + small label chip (orange @ 16%).
- Playhead: 1px accent line through ruler and lanes, mono label pill on the ruler (accent fill, on-accent text).

## Curve editor
Mockup: `CurveEditor.dc.html`.
- Toolbar: Interpolation segmented with curve glyphs (Constant | Linear | Bezier), Tangents segmented (Auto | Aligned | Free | Flat),
  Frame all (A), and numeric Frame / Value fields for the selected key on the right.
- Channel list 238: tree with 10px channel color squares, the value at the playhead (mono), eye / eye-off; hidden channels at 55% opacity.
- Plot bg #0E1013 / #FFFFFF. Grid: minor every 10 frames / 0.25 (#15181D / #F1F2F5), major every 30 frames / 0.5 (#1F232A / #E1E4E9),
  zero line #343A45 / #C9CED6. Value labels mono 10 text-3 at the left edge; frame ruler 24 on top.
- Channel colors = axis colors (X red, Y green, Z blue, scalar = warn). Active channel stroke 2px at full opacity; others 1.5px at 55%.
  Pre/post infinity drawn dashed (3 4).
- Keys 8px rounded squares in channel color with a plot-colored edge. Selected key 11px white with 2px accent border;
  tangent handles: 1.25px accent lines to 8px hollow accent circles.
- Key readout tooltip next to the selected key: "X · f 120 · 0.400" (mono 10.5, tooltip colors).
- Playhead identical to the timeline (frame number in the pill).

## Progress & loading
Mockup: `Progress.dc.html`.
- Bar: 6px (4px inside lists and status bar), radius 3, track #23272F / #E3E6EB, fill accent. Label row above (name + mono %),
  detail line below (11px text-3: size, ETA). Cancel × on the label row.
- Indeterminate: a 26% wide segment with soft (transparent) ends sliding left→right, ~1.2 s loop.
- Failed: fill in error color, error icon in the label, reason in error text below, Retry button. Done: green check + duration.
- Multi-step: 16px status glyph column (done = filled ok circle with check; active = spinner; pending = dashed ring) with a 4px bar under the active step.
- Spinners: ring in track color + 90° accent arc, rotate 0.8 s linear. Sizes 12 / 16 / 24. Circular progress 40px, 3px stroke, mono % inside.
- Busy button: keeps its width, spinner replaces the icon, label in -ing form ("Saving…"), aria-busy.
- Skeletons: 10px rounded bars in #1C1F25 / #E9EBEF matching the real layout; a soft shine sweeps across (1.5 s).
- Status bar: 12px spinner + label + 120px bar + mono count; a "3 tasks" pill opens the Background tasks popover
  (popup surface, rows with 4px bars, mono %/state, cancel ×; paused tasks in warn color).

## Notifications & confirmations
Mockup: `Notifications.dc.html`.
- Toasts: bottom-right of the main window, 16px from the edges, 8px apart, newest closest to the corner; older ones collapse into a "+N earlier" pill.
  380 wide, popup surface, radius 8, padding 12. Grid: 24px severity circle (severity @ 14–16% with a 2.4-stroke glyph) | content | 22px dismiss.
  Title 600 + relative time (11px text-3), body text-2 (mono 11 for code/file locations), optional 4px progress bar, text-button actions
  (primary in link color, secondary text-2). Errors use role=alert and stay until dismissed; others auto-hide after ~6 s (paused on hover).
  No colored side stripes — severity lives in the icon only.
- Confirmation dialog (destructive): 32px error-soft circle with icon, title 14/600 as a question, consequence sentence,
  list of affected items in a well (mono 11, max 3 + "and N more…"), optional "Don't ask again" checkbox.
  Footer: Cancel (default focus, shown with focus ring) + destructive button in danger fill — #EF6A6A with dark text (dark) /
  #C93C3C with white text (light); the button label repeats the action and count ("Delete 12 assets").
- Inline banners at the top of a panel: full-width, 8×12 padding, severity @ 10–14% fill with a 1px severity @ 30–35% bottom line,
  icon + message + actions right-aligned (primary accent button, secondary button, or a link).

## Documents — top-level docking
Mockups: `Main.dc.html` (document bar), `DocumentWindow.dc.html` (second main window).
Two levels of docking:
1. **Documents** (levels, prefabs/assets, material graphs, layouts, animations). Each document owns a complete panel layout;
   switching the document tab swaps the entire workspace below the document bar.
2. **Panels** inside a document (Hierarchy, Viewport, Inspector, …), docked as before.

Document bar — "colored tabs"
- Its own row directly under the title bar (the title bar keeps menus, search and window buttons): 36 high, bg-titlebar
  (#08090B / #DADDE2), tabs aligned to the bottom, 3px apart, 8px side padding.
- The bar's bottom edge is a 2px line in the active document's solid color, so the workspace below is visibly "owned" by it.
- Tab: 30 high, min-width 150, radius 6 6 0 0, padding 0 8 0 12, gap 8. Type icon 14px, name + extension ("Forest" + ".level"),
  dirty dot (6px) or close ×.
- Every tab carries its document-type color:
  inactive = type color @ 14% (dark) / 12% (light) fill, text-1 name, type-colored icon, text-3 extension;
  active = solid type color, name 600 and icon in on-solid (#0B0C0F dark / #FFFFFF light), extension on-solid @ 62–78%.
- Hover / pressed on inactive tabs: tint @ 22% / 30% (dark), 18% / 24% (light). Keyboard focus: inset 1px accent outline.
- Right side: "N open ▾" overflow list. "+" after the last tab creates a document.
- Document-type colors — tint (all themes use the normal palette) / solid in light theme (darkened so white text passes 4.5:1):
  level #6B9BFF / #2F6FEB · material/graph #34D1BF / #0B7A6F · layout #F59E4B / #B35F0E · prefab #A98BFF / #7C5CE0 · animation #F472B6 / #B8306F.
  In the dark theme the solid color equals the tint color (dark text on it).

How they differ from panel tabs (on purpose)
| | Panel tab | Document tab |
|---|---|---|
| Height | 24 | 30 |
| Shape | floating pill (radius 4), neutral | top-rounded tab (radius 6 6 0 0) sitting on a colored 2px baseline |
| Color | neutral grey, accent icon when active | always tinted in the document-type color; active is solid |
| Name | label | name + muted extension, dirty dot |
| Location | panel header | own bar under the title bar |

Behaviour
- Dragging a document tab out of the bar creates a new main window (full chrome: title bar, menus, document bar, status bar)
  holding that document and its own panel layout. Dropping a document tab on another window's document bar moves it there.
- Document tabs only drop on document bars; panel tabs only drop on panel docks. The two drop-target sets never mix.
- Panels inside a document can still be torn off into panel windows (see Torn-off panel window); those belong to the document's main window.
- The window title / status bar names the active document.

## States & motion
Mockup: `States.dc.html` (matrix of 12 controls × rest, hover, pressed, focus, selected/on, disabled, invalid).

State model — every visual state is computed, not hand-picked:
- **Base**: the control's rest colors.
- **State layer**: the control's own content color over the base, at **8% hover**, **14% pressed**.
  Dark secondary buttons therefore lighten; a primary button (dark text on accent) darkens; light theme darkens.
  Exception: menu items and list rows highlight with accent-soft (they are keyboard-navigated).
- **Selection / on**: accent-soft fill (16% dark / 12% light) or the control's own "on" visual (toggle track, check fill).
- **Focus** (keyboard only, never on mouse click): 1px accent border + 3px accent @ 30%/25% halo for fields and controls;
  inset 1px accent outline for tabs, rows and menu items (they sit flush with neighbours).
- **Disabled**: 40% opacity on the whole control (menus 45%), no state layer, no focus.
- **Invalid**: error border + 3px error @ 16–18% halo; stacks with focus and hover.

Transitions
| Change | Duration | Easing |
|---|---|---|
| Hover in | 70 ms | ease-out |
| Hover out | 140 ms | ease-out |
| Press | 0 ms | instant |
| Release | 120 ms | ease-out |
| Focus ring | 100 ms | ease-out (halo grows 0 → 3px) |
| Selection | 0 ms | instant — highlight follows input exactly |
| Toggle, check, radio | 120 ms | ease-in-out (knob + fill together) |
| Tooltip | 100 ms in / 80 ms out | ease-out (after 500 ms delay) |
| Menu, popup | 120 ms in / 80 ms out | ease-out, fade + 4px slide from the anchor |
| Toast | 180 ms in / 140 ms out | ease-out, slide from the right edge |
| Dock preview | 100 ms | ease-out |
| Invalid | 0 ms (halo 150 ms) | instant border |
| Theme switch | 200 ms | ease-in-out, every token at once |
ease-out = cubic-bezier(0.2, 0, 0, 1); ease-in-out = cubic-bezier(0.4, 0, 0.2, 1).

Color interpolation
- Same-hue changes (state layers, selection fades): lerp in sRGB — the delta is small and it's cheapest.
- Hue changes (ok → warn → error, accent → error on invalid, theme switch): lerp in **Oklab**; sRGB sags darker/greyer mid-way.
  Convert each token's endpoints to Oklab once at theme load, not per frame.

Implementation notes
- Per widget keep one scalar per animated channel (hover, press, focus; 8 bits each is enough) in a compact array indexed by widget id;
  colors are a lerp between precomputed endpoint colors, so no per-frame color math beyond one lerp per channel.
- Only widgets with an unfinished transition are ticked; when none are active, don't redraw.
- Retarget from the current value when a state flips mid-transition (never restart from 0).
- Reduced-motion setting: all durations 0, except progress indicators and spinners.

## Dialog surface (elevation)
Modal dialogs, message boxes and pickers must stand out from the window, so they get their own surface tier instead of bg-panel:
| Token | Dark | Light |
|---|---|---|
| dialog-bg | #1B1E25 | #FFFFFF |
| dialog-header (title bar + footer) | #20242C | #F6F7F9 |
| dialog-border | #3A404C | #C3C9D2 |
| dialog-divider | #2A2F39 | #E6E8EC |
| dialog-well (lists, details) | #13151A | #F6F7F9 |
| dialog-input | #0F1115 | #FFFFFF |
| dialog-control (buttons) | #272B34 | #FFFFFF |
| dialog-control-border | #3A404C | #C9CED6 |
| dialog-shadow | 0 0 0 1px rgba(0,0,0,.5), 0 24px 64px rgba(0,0,0,.6), inset 0 1px 0 rgba(255,255,255,.05) | 0 0 0 1px rgba(16,24,40,.06), 0 24px 64px rgba(16,24,40,.28) |
| scrim (over the owning window) | rgba(0,0,0,0.55) | rgba(16,24,40,0.30) |
- Dark: the dialog is a step *lighter* than panels (closer = lighter), with a 1px top highlight. Light: pure white over a dimmed window.
- Modal dialogs always dim their owning window with the scrim (fade 120 ms); non-modal pickers skip the scrim but keep the surface + shadow.
- Elevation order: panel < popup/menu (#1A1D23 / #FFFFFF) < dialog < tooltip. Toasts use the popup tier.

## Message boxes (yes / no / cancel)
Mockup: `MessageBox.dc.html`.
- 456 wide, bg-panel, 1px border, radius 8, dialog shadow; no title bar — a 24px close × in the top-right corner (same as Cancel / Esc).
- Body padding 20 (40 on the right to clear the ×): 32px severity circle (severity @ 12–16% with a 2.2-stroke glyph) |
  title as a question, 14/600 · one or two sentences saying what happens · optional extra (affected-items list in a well,
  "Don't ask again" checkbox, collapsible mono Details).
- Footer 52, bg-header, top divider. Buttons 28 high. Order: [destructive-alternative left] … [Cancel] [primary] right-aligned.
  Unsaved changes: "Don't save" alone on the left, Cancel, then "Save all" (primary).
- Button labels are verbs that say what happens ("Reload", "Keep mine", "Quit") — never bare Yes/No.
- Default button (Enter) has the focus ring. Safe default: the primary action when it's harmless (Save, Reload, OK);
  Cancel when the action destroys or stops work (Quit with running tasks, Delete). Destructive buttons use the danger fill.
- Esc = Cancel (or the close ×). Every button has an Alt mnemonic, shown underlined on its first letter.
- Severity icon: warning for data loss, power/warning for quitting, question for neutral choices, error for failures.
  The error box offers "Copy details" next to OK.

## Viewport
- Floating toolbars over the view (bg rgba(22,25,30,.92), 1px line-subtle, radius 6, 30 high, 3px inset): file/undo group, shading-mode group, camera dropdown.
- Vertical tool strip at left (select/move/rotate/scale); active tool = accent fill with on-accent icon.
- Axis gizmo top-right 64px; FPS badge bottom-left (mono).

## Dialogs (shared)
Mockups: `ColorPicker.dc.html`, `GradientEditor.dc.html` (prop `theme` = dark | light; the `*Light.dc.html` files just set it).
- Frame: bg-panel, 1px panel-border, radius 8, shadow 0 24 64 rgba(0,0,0,.55) dark / rgba(16,24,40,.18) light.
- Header 44: title 13/600 text-1 + context path text-3 + 28px close button.
- Footer 52, bg-header, top divider: status text left; Cancel (secondary) + OK (accent fill, on-accent, 600) right.
- Transparency checkerboard: 8px squares, #2A2F39/#1E2128 dark, #FFFFFF/#E3E6EB light.
- Selected swatch/preset: 2px panel-colored gap + 2px accent ring.

### Color picker (560 wide)
- SV square 240×240, radius 6; cursor 14px ring (2px white + 1px dark outline).
- Hue strip and alpha strip 16×240, 10px apart; handle 22×6 white-outlined bar.
- Right column: New/Original split preview 40 high on checkerboard; RGB | HSV | HSL segmented + range toggle (0–255 / 0–1).
- Channel rows 26: label 14 / track 8 high showing the channel's actual gradient / numeric field 52. Thumb 12px white.
- Hex field (focused state shown) + eyedropper; HDR intensity field + Linear checkbox.
- Swatches row (22px squares, dashed add button) and Recent row (22px circles).
  Both use the color-field split: left half opaque, right half at actual alpha over the checkerboard.
  Swatch squares also get a 2px alpha bar along the bottom (circles omit it). Unselected outline 1px line-node.

### Gradient editor (720 wide)
- Preset chips 64×22.
- Gradient bar 44 high on checkerboard, 24px side inset so end stops fit.
- Alpha stops above the bar (14px square, fill = grey at alpha level, pointer down); color stops below (18px square with stop color, pointer up).
  Unselected edge text-1 (dark) / white (light); selected edge = accent + 3px accent-soft halo, with a location badge under it.
- Left column: selected stop (n / total, prev/next, color swatch + hex, location %, duplicate, delete in error color),
  Interpolation (Linear | Smooth | Constant), Blend space (sRGB | Linear | Oklab), Reverse, Distribute evenly.
- Right column: stops table (Type, swatch, value, location) sorted by location; selected row = accent-soft + 2px accent left bar.

## Light theme (themes/light.css)
Same token names, metrics and structure. Mockups: `MainLight.dc.html`, `InspectorLight.dc.html`, `SettingsLight.dc.html`, `TokensLight.dc.html`.

| Token | Dark | Light |
|---|---|---|
| bg-app | #0B0C0F | #E6E8EC |
| bg-input | #0D0F12 | #FFFFFF |
| bg-canvas | #0E1013 (dots #252A33) | #F4F5F7 (dots #CDD2DA) |
| bg-panel | #121418 | #FBFBFC |
| bg-header | #16191E | #F3F4F6 |
| bg-raised | #181B20 | #FFFFFF |
| bg-control (buttons) | #1E2128 | #FFFFFF + 1px #CDD2DA, shadow 0 1 2 rgba(16,24,40,.06) |
| bg-active (active tab) | #23272F | #E4E7EC |
| segment well / pressed | #0D0F12 / #2A2F39 | #EEF0F3 / #FFFFFF chip + shadow 0 1 2 rgba(16,24,40,.12) |
| sidebar nav, details pane | #101216 / #0F1114 | #F6F7F9 |
| row-stripe | #14161B | #F6F7F9 (rows on #FFFFFF) |
| panel-border | #1C1F25 | #DADDE3 |
| line-subtle | #23272F | #E3E6EB |
| line-node / input border | #2A2F39 | #D5D9E0 |
| line-strong / scrollbar / toggle-off | #2E333D | #C9CED6 |
| section divider | #1E2128 | #E9EBEF |
| text-1 | #E8EAEF | #1A1D23 |
| text-2 | #AEB4C0 | #464C57 |
| text-3 | #8E96A7 | #5F6673 |
| text-link | #8FB3FF | #2459C9 (hover #1A4296) |
| accent | #6B9BFF | #2F6FEB |
| accent-soft | accent @ 16% | accent @ 12% |
| on-accent | #0B0C0F | #FFFFFF |
| ok / warn / error | #4FC48A / #E5B454 / #EF6A6A | #1E9E62 / #D99A2B (text #8E5D11) / #D64545 |
| pin float / int / bool / color | #34D1BF / #F59E4B / #A98BFF / #F472B6 | #0FA596 / #D9771A / #7C5CE0 / #D63F8C |
| axis x / y / z | #EF6A6A / #4FC48A / #6B9BFF | #D64545 / #1E9E62 / #2F6FEB |
| axis label text | #F28B8B / #6FD3A1 / #8FB3FF | #B83232 / #157A4A / #2459C9 (bg axis @ 12%) |
| node category gen/in/out | #E0716B / #4FC48A / #A98BFF | #D9534F / #1E9E62 / #7C5CE0 |
| folder icon | #D9A443 | #E3AE4C (tree icon #C98F2A) |
| frame bars ok / over | #3E8F6B / #E5B454 | #59B88A / #E0A23A |
| budget line | #5A606C | #9AA1AD |
| node shadow | 0 8 20 rgba(0,0,0,.35) | 0 4 14 rgba(16,24,40,.10) |
| floating toolbar bg | rgba(22,25,30,.92) | rgba(255,255,255,.92) + shadow 0 1 3 rgba(16,24,40,.08) |
| slider thumb | #E8EAEF, 3px accent border | #FFFFFF, 3px accent border |

Flame chart fills (light, text #1A1D23): Frame #E3E6EB · Build/Layout #F4DCCF · Render/Record #E2DCF5 · OnGUI/EndFrame #D3ECEF · Window render #D7EBDD.
Alternate light accents (all ≥4.5:1 with white): #0E8277, #6D4FD6, #B85C10.

## Behavioral notes
- Selection = accent-soft background + text-1 (never a saturated solid fill behind text).
- Large tables: selected row also gets a 2px accent inset bar on the left.
- Bottom dock can collapse to a 30px tab strip (Files, Log with count badge).
