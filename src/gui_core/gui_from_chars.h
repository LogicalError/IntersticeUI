#pragma once

#include <charconv>
#include <locale>
#include <sstream>
#include <string>
#include <system_error>
#include <version>

namespace interstice
{
    /// std::from_chars for a decimal number, through a classic-locale stream. For standard
    /// libraries without floating-point from_chars. The number is scanned first, as
    /// from_chars does ([-]digits[.digits][e[+-]digits], no '+' or whitespace before it):
    /// libc++'s stream would also take the letters of a hex float, so "0.25px" fails there.
    template <typename T>
    std::from_chars_result FromCharsStream(const char* first, const char* last, T& value)
    {
        auto digits = [last](const char* p) {
            while (p != last && *p >= '0' && *p <= '9') ++p;
            return p;
        };
        const char* p = first;
        if (p != last && *p == '-') ++p;
        const char* intEnd = digits(p);
        const char* end    = intEnd;
        if (end != last && *end == '.')
            end = digits(end + 1);
        if (intEnd == p && end - intEnd <= 1)
            return { first, std::errc::invalid_argument };   // no digits
        if (end != last && (*end == 'e' || *end == 'E'))
        {
            const char* exp = end + 1;
            if (exp != last && (*exp == '+' || *exp == '-')) ++exp;
            if (const char* expEnd = digits(exp); expEnd != exp)
                end = expEnd;   // otherwise the 'e' isn't part of the number
        }

        std::istringstream in(std::string(first, end));
        in.imbue(std::locale::classic());
        T parsed{};
        in >> parsed;
        if (in.fail())
            return { first, std::errc::result_out_of_range };
        value = parsed;
        return { end, std::errc{} };
    }

    /// std::from_chars, falling back to FromCharsStream on standard libraries without the
    /// floating-point overloads (libc++ before LLVM 20, Apple's included).
    template <typename T>
    std::from_chars_result FromChars(const char* first, const char* last, T& value)
    {
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
        return std::from_chars(first, last, value);
#else
        return FromCharsStream(first, last, value);
#endif
    }
}
