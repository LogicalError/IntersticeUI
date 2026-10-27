#pragma once

#include <gui_core/text/gui_numeric_type.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace tachyon
{
    // Numbers as the text a field shows and edits.

    /// `value` as text; floats use up to 9 significant digits, NaN and infinities spelled out.
    std::string FormatNumber(NumericType type, const NumericValue& value);
    /// The whole text as a number of `type`: in range and, for floats, finite.
    bool ParseNumber(std::string_view text, NumericType type, NumericValue& outValue);
    /// Strips what a number of `type` can't hold (letters, a second dot, ...). Partial
    /// forms that can still be finished ("-", "1e-") stay. True if `text` changed.
    bool FilterNumberText(NumericType type, std::string& text);
    /// Whether `codepoint` can be typed into a number of `type`.
    bool IsNumberCharacter(NumericType type, uint32_t codepoint);
}
