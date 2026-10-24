# Graphite theme

- `graphite-theme-spec.md` — the full spec: colors, metrics, per-control rules, dark/light table.
- `tokens/graphite-tokens.json` — every color and metric for both themes (source of truth).
- `tokens/graphite-dark.css`, `tokens/graphite-light.css` — the same tokens as CSS custom properties.
- `mockups/` — source of every design board (`*.dc.html`). Styles are inline, so exact values can be read
  from them; they need the design canvas runtime to render, so open the canvas to view them.

Token names are suggestions; map them onto the style names in your own `themes/*.css`.
