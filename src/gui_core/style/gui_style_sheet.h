#pragma once

#include <gui_core/style/gui_style.h>

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace GUI 
{
    // ============================================================================
    // StyleSheet — result of parsing a CSS-inspired style file
    // ============================================================================

    struct ParseError
    {
        std::string message;
        int         line   = 0;
        int         column = 0;
    };

    // Resolved CSS custom property (from :root { --name: <value>; })
    struct CustomProperty
    {
        // Color value (set when property is a color)
        color4b    color {};
        ColorStyle colorStyle {};
        bool       hasColor = false;

        // 1..4 numeric values (e.g. `--border-first: 2px 2px 2px 2px;`).
        // hasNumber and `number` are aliases for the single-value case (dimCount == 1).
        // Multi-value vars are consumed by shorthand parsers like border-width / border-radius.
        uint16_t   dims[4] {};
        uint8_t    dimCount = 0;
        float      number    = 0.0f; // == dims[0] when dimCount >= 1
        bool       hasNumber = false; // dimCount >= 1
    };

    // One @font-face rule. Faces sharing a family name form one family.
    struct FontFace
    {
        std::string family;
        std::string src; // as written; relative paths are relative to the stylesheet
    };

    struct StyleSheet
    {
        // Styles keyed by selector name (without leading '.')
        std::unordered_map<std::string, Style>          styles;
        std::unordered_map<std::string, CustomProperty> customProperties;
        std::vector<FontFace>                           fontFaces; // in source order
        Style                                           baseStyle; // what `*` rules set; styles start from it
        std::vector<ParseError>                         errors;

        bool                  HasErrors()                            const { return !errors.empty(); }
        const Style*          GetStyle(std::string_view name)          const;
        const CustomProperty* GetCustomProperty(std::string_view name) const;
    };

    // ============================================================================
    // ParseStyleSheet
    // ============================================================================
    //
    // Parses a CSS-inspired text into a StyleSheet. Errors are non-fatal: parsing
    // continues after a bad declaration and all errors are collected in the result.
    //
    // --- Syntax overview --------------------------------------------------------
    //
    //   /* Comments (block only) */
    //
    //   :root {
    //       --variable-name: <color-value>;
    //   }
    //
    //   .styleName {
    //       property: value;
    //       property: var(--variable-name);
    //       ...
    //   }
    //
    //   /* `*` sets a base every style starts from; class rules override it.
    //      Put it before the rules it should be the base of. */
    //   * { font-family: "Inter"; }
    //
    //   /* Comma-separated selectors share one block */
    //   .foo, .bar { ... }
    //
    //   /* Font faces; weight and style come from the font file */
    //   @font-face { font-family: "Inter"; src: url("../fonts/Inter-Regular.otf"); }
    //   @font-face { font-family: "Inter"; src: url("../fonts/Inter-Bold.otf"); }
    //
    //   /* Pseudo-state suffix restricts which state slot is written */
    //   .button:hover   { background-color: #4a4a4a; }
    //   .button:active  { background-color: #2a2a2a; }
    //   .button:focus   { border-color: #0078d7; }
    //   .button:selected{ background-color: #005a9e; }
    //   .button:disabled{ color: #888888; }
    //
    // --- Supported properties ---------------------------------------------------
    //
    //   Box model (layout, not state-specific):
    //     margin, margin-top/right/bottom/left
    //     padding, padding-top/right/bottom/left
    //     border-width, border-top/right/bottom/left-width
    //     border-radius, border-top-left/top-right/bottom-right/bottom-left-radius
    //
    //   Display & layout:
    //     display           : flex | grid | none
    //     overflow          : auto | hidden | visible
    //     visibility        : visible | collapsed
    //     flex-direction    : row | row-reverse | column | column-reverse
    //     flex-wrap         : nowrap | wrap | wrap-reverse
    //     align-items       : flex-start | flex-end | center | stretch  (flex + grid)
    //     align-self        : auto | flex-start | flex-end | center | stretch (flex + grid)
    //     justify-content   : flex-start | flex-end | center | space-between |
    //                         space-around | space-evenly               (flex + grid)
    //     justify-items     : stretch (default) | justify-content values (grid only)
    //     column-gap        : <px>                                      (grid)
    //     row-gap           : <px>                                      (grid)
    //     grid-column       : span <N> | <start> | <start> / span <N>
    //     grid-row          : span <N> | <start> | <start> / span <N>
    //     gap               : <px>                                      (flex)
    //     flex-grow         : <number>
    //     flex-shrink       : <number>
    //     flex-basis        : <px> | auto
    //     cursor            : default | text | wait | crosshair | pointer |
    //                         col-resize | row-resize | nwse-resize | nesw-resize |
    //                         move | not-allowed | none
    //     -interstice-icon-side       : left | right | top | bottom
    //     -interstice-icon-text-gap   : <px>
    //     break-after             : auto | always | avoid | left | right
    //     -interstice-clamp-scroll    : true | false
    //     -interstice-border-dash-size   : <px>
    //     -interstice-border-dash-offset : <px>
    //     -interstice-grid-column-track  : auto | <Npx> | <Nfr>
    //     -interstice-grid-row-track     : auto | <Npx> | <Nfr>
    //     dominant-baseline           : auto | alphabetic | ideographic | central | hanging |
    //                                  mathematical | middle | text-top | text-bottom
    //     vertical-align             : (same values as dominant-baseline)
    //     -interstice-text-baseline      : (same values as dominant-baseline, vendor-prefixed alias)
    //
    //   Text layout (not state-specific):
    //     text-align                     : start | center | end | left | right | top | bottom
    //     -interstice-vertical-text-align    : start | top | center | middle | end | bottom
    //                                    Vertical position of the text (+ icon) block in its rect;
    //                                    `vertical-align` sets the glyph baseline instead.
    //     text-wrap                      : none | wrap | word-char | break
    //     text-overflow                  : clip | ellipsis
    //     direction                      : auto | ltr | rtl
    //
    //   Visual / per-state:
    //     background-color  : <color> | linear-gradient(...) | radial-gradient(...)
    //     border-color      : <color>
    //     color             : <color>
    //     font-size         : <px>
    //     font-weight       : normal | thin | extra-light | light | medium |
    //                         demi-bold | semi-bold | bold | extra-bold | black |
    //                         heavy | extra-black  (or numeric 100-950)
    //     font-style        : normal | italic | oblique
    //     font-family       : [<family-name> | <generic>]#  — first family name is the font,
    //                         first generic (default | emoji | sans-serif | serif |
    //                         monospace | math) the fallback while it isn't registered
    //     font-stretch      : normal | condensed | expanded | ...
    //     line-height       : normal | <number> | <number>px | <number>em
    //     letter-spacing    : <number>
    //     word-spacing      : <number>
    //
    //   Text decorations (per-state, via text-decoration shorthand or long-hand):
    //     text-underline-color/thickness/offset/style
    //     text-overline-color/thickness/offset/style
    //     text-throughline-color/thickness/offset/style
    //     text-bottomline-color/thickness/offset/style
    //
    // --- Color values -----------------------------------------------------------
    //
    //   #rgb  #rgba  #rrggbb  #rrggbbaa
    //   rgb(r, g, b)                         r/g/b in 0-255
    //   rgba(r, g, b, a)                     a in 0-255
    //   transparent  white  black  red  green  blue  yellow  cyan  magenta
    //   gray / grey  silver  orange  purple  navy  teal  maroon  olive
    //   var(--variable-name)                  references a :root custom property
    //   linear-gradient(<angle>deg, <color> [<pct>%], ...)
    //   radial-gradient(<color> [<pct>%], ...)
    //
    // --- Dimension values -------------------------------------------------------
    //
    //   <number>      treated as pixels, stored as uint16_t
    //   <number>px    same
    //
    StyleSheet ParseStyleSheet(std::string_view source);

} // namespace GUI
