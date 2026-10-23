# Graphite dark theme — spec

Reference mockups: `MainDark.dc.html` (NodeView + Profiler), `InspectorDark.dc.html` (Viewport + Inspector),
`SettingsDark.dc.html` (Settings + Table), `TokensDark.dc.html` (token sheet). They are HTML mockups with inline styles;
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
| text-3 | #8A92A2 | Captions, placeholders, timestamps, column headers (≥4.5:1 on every surface above) |
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
| Tab overflow | show first N tabs, then "+N ▾" button instead of wrapping to a second row |
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

## Viewport
- Floating toolbars over the view (bg rgba(22,25,30,.92), 1px line-subtle, radius 6, 30 high, 3px inset): file/undo group, shading-mode group, camera dropdown.
- Vertical tool strip at left (select/move/rotate/scale); active tool = accent fill with on-accent icon.
- Axis gizmo top-right 64px; FPS badge bottom-left (mono).

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
| text-3 | #8A92A2 | #5F6673 |
| text-link | #8FB3FF | #2459C9 (hover #1A4296) |
| accent | #6B9BFF | #2F6FEB |
| accent-soft | accent @ 16% | accent @ 12% |
| on-accent | #0B0C0F | #FFFFFF |
| ok / warn / error | #4FC48A / #E5B454 / #EF6A6A | #1E9E62 / #D99A2B (text #9A6512) / #D64545 |
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
