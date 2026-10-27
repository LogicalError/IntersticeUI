#pragma once

#include <gui_core/text/gui_numeric_text.h>

#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

namespace tachyon
{
    /// What an editable field takes as text, asked while it is edited. An instance given to
    /// the text system must outlive the field (a static or a member, not a temporary).
    class ITextValidator
    {
    public:
        virtual ~ITextValidator() = default;

        /// Whether typing `codepoint` can be part of the text.
        virtual bool AcceptsCharacter(uint32_t /*codepoint*/) const { return true; }
        /// Strips what the text can't hold (pasted or dropped text, a typed character);
        /// true if `text` changed.
        virtual bool Filter(std::string& /*text*/) const { return false; }
        /// Whether an IME may compose text into the field.
        virtual bool AcceptsComposition() const { return true; }
    };

    /// Turns a field's value into the text it shows and edits, and edited text back into a
    /// value. Specialize it for a type to edit that type in a field, or pass any object with
    /// the same members (and ITextValidator as its base) to RegisterEditableValue:
    ///
    ///   std::string Format(const T& value, bool editing) const;   // editing: the focus just arrived
    ///   bool        Parse(std::string_view text, T& value) const; // false: not a T, value untouched
    template<typename T>
    struct TextConverter;

    template<typename T>
    concept NumericFieldType = GetNumericType(T{}) != NumericType::None;

    template<NumericFieldType T>
    struct TextConverter<T> : ITextValidator
    {
        std::string Format(const T& value, bool editing) const
        {
            // NaN and infinities aren't text to start editing from: start from 0.
            if constexpr (std::is_floating_point_v<T>)
                if (editing && !std::isfinite(value))
                    return "0";
            NumericValue number{};
            const NumericType type = CopyNumericTypeTo(value, number);
            return FormatNumber(type, number);
        }

        bool Parse(std::string_view text, T& value) const
        {
            NumericValue number{};
            const NumericType type = GetNumericType(value);
            if (!ParseNumber(text, type, number))
                return false;
            return CopyNumericTypeTo(type, number, value);
        }

        bool AcceptsCharacter(uint32_t codepoint) const override { return IsNumberCharacter(GetNumericType(T{}), codepoint); }
        bool Filter(std::string& text) const override { return FilterNumberText(GetNumericType(T{}), text); }
        bool AcceptsComposition() const override { return false; }   // digits and signs only
    };

    /// The converter a field uses for T when it is given none.
    template<typename T>
    const TextConverter<T>& DefaultTextConverter()
    {
        static const TextConverter<T> converter;
        return converter;
    }
}
