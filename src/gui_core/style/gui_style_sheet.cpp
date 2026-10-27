#include <gui_core/style/gui_style_sheet.h>
#include <gui_core/style/gui_style_sheet_internal.h>

#include <string>

// Top-level stylesheet API. The CSS lexer/parser live in
// gui_style_sheet_internal.h; per-property parsing in gui_style_sheet_properties.cpp.

namespace tachyon {

// ============================================================================
// StyleSheet::GetStyle
// ============================================================================

const Style* StyleSheet::GetStyle(std::string_view name) const
{
    auto it = styles.find(std::string(name));
    return (it != styles.end()) ? &it->second : nullptr;
}

// ============================================================================
// StyleSheet::GetCustomProperty
// ============================================================================

const CustomProperty* StyleSheet::GetCustomProperty(std::string_view name) const
{
    auto it = customProperties.find(std::string(name));
    return (it != customProperties.end()) ? &it->second : nullptr;
}

// ============================================================================
// ParseStyleSheet
// ============================================================================

StyleSheet ParseStyleSheet(std::string_view source)
{
    Lexer  lex(source);
    Parser parser(lex);
    return parser.Parse();
}

} // namespace tachyon
