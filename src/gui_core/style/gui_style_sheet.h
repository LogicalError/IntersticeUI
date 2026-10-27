#pragma once

#include <gui_core/style/gui_style.h>

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace tachyon 
{
    /// @addtogroup styling
    /// @{

    // ============================================================================
    // StyleSheet - result of parsing a CSS-inspired style file
    // ============================================================================

    /// A problem found while parsing a style sheet; the parser skips the bad part and goes on.
    struct ParseError
    {
        std::string message;    ///< what is wrong
        int         line   = 0; ///< 1-based line of the problem in the parsed source
        int         column = 0; ///< column of the problem on that line
    };

    /// A resolved CSS custom property (`:root { --name: value; }`): a color, 1..4 numbers
    /// or a box-shadow.
    struct CustomProperty
    {
        color4b    color {};         ///< the value as a solid color
        ColorStyle colorStyle {};    ///< the full value, which may be a gradient
        bool       hasColor = false; ///< color / colorStyle are set

        /// 1..4 numeric values (`--border-first: 2px 2px 2px 2px;`), for shorthands such
        /// as border-width and border-radius.
        uint16_t   dims[4] {};
        uint8_t    dimCount = 0; ///< number of values in dims, 0 = not numeric
        float      number    = 0.0f; ///< == dims[0] when dimCount >= 1
        bool       hasNumber = false; ///< dimCount >= 1

        /// A box-shadow value (`--shadow: 0 8px 20px rgba(0,0,0,0.35);`, or none).
        BoxShadow  shadows[kMaxBoxShadows] {}; ///< see hasShadow
        bool       hasShadow = false; ///< shadows is set (all-empty for `none`)
    };

    /// One `@font-face` rule. Faces sharing a family name form one family.
    struct FontFace
    {
        std::string family; ///< font-family name
        std::string src; ///< as written; relative paths are relative to the stylesheet
    };

    /// A parsed style sheet: named styles, custom properties and font faces.
    /// Register a style with StyleManager::Register to use it.
    struct StyleSheet
    {
        std::unordered_map<std::string, Style>          styles; ///< keyed by class name, without the '.'
        std::unordered_map<std::string, CustomProperty> customProperties; ///< keyed by name including the leading "--"
        std::vector<FontFace>                           fontFaces; ///< in source order
        Style                                           baseStyle; ///< what `*` rules set; styles start from it
        std::vector<ParseError>                         errors; ///< in the order found

        /// True when parsing reported any error.
        bool                  HasErrors()                            const { return !errors.empty(); }
        /// The style of a class name (without the '.'); nullptr when not defined.
        const Style*          GetStyle(std::string_view name)          const;
        /// A custom property by name (with the leading "--"); nullptr when not defined.
        const CustomProperty* GetCustomProperty(std::string_view name) const;
    };

    /// Parses a theme's CSS (the subset TachyonUI supports, see docs/css.md) into a
    /// StyleSheet. Errors don't stop it: a bad declaration or rule is skipped and
    /// reported in StyleSheet::errors, and the rest is parsed.
    StyleSheet ParseStyleSheet(std::string_view source);

    /// @}
} // namespace tachyon
