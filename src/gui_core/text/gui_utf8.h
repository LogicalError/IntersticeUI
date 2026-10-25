#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace interstice
{
    inline constexpr uint32_t kInvalidCodepoint = 0xFFFFFFFFu;

    // The codepoint starting at text[i], with i moved past it. kInvalidCodepoint for a
    // malformed or cut-off sequence (i moves one byte).
    inline uint32_t NextUtf8Codepoint(std::string_view text, size_t& i)
    {
        const auto lead = static_cast<unsigned char>(text[i]);
        const int length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : (lead >> 3) == 0x1E ? 4 : 0;
        if (length == 0 || i + size_t(length) > text.size())
        {
            i++;
            return kInvalidCodepoint;
        }
        uint32_t codepoint = length == 1 ? lead : lead & (0x7Fu >> length);
        for (int k = 1; k < length; ++k)
        {
            const auto next = static_cast<unsigned char>(text[i + size_t(k)]);
            if ((next & 0xC0u) != 0x80u)
            {
                i++;
                return kInvalidCodepoint;
            }
            codepoint = (codepoint << 6) | (next & 0x3Fu);
        }
        i += size_t(length);
        return codepoint;
    }

    inline void AppendUtf8(std::string& out, uint32_t codepoint)
    {
        if      (codepoint < 0x80)    { out += char(codepoint); }
        else if (codepoint < 0x800)   { out += char(0xC0 | (codepoint >> 6));  out += char(0x80 | (codepoint & 0x3F)); }
        else if (codepoint < 0x10000) { out += char(0xE0 | (codepoint >> 12)); out += char(0x80 | ((codepoint >> 6) & 0x3F)); out += char(0x80 | (codepoint & 0x3F)); }
        else                          { out += char(0xF0 | (codepoint >> 18)); out += char(0x80 | ((codepoint >> 12) & 0x3F)); out += char(0x80 | ((codepoint >> 6) & 0x3F)); out += char(0x80 | (codepoint & 0x3F)); }
    }

    // `text` with each malformed byte replaced by U+FFFD (the replacement character).
    inline std::string ToValidUtf8(std::string_view text)
    {
        std::string valid;
        valid.reserve(text.size());
        for (size_t i = 0; i < text.size(); )
        {
            const size_t start = i;
            const uint32_t codepoint = NextUtf8Codepoint(text, i);
            if (codepoint == kInvalidCodepoint)
                AppendUtf8(valid, 0xFFFD);
            else
                valid.append(text.substr(start, i - start));
        }
        return valid;
    }

    // The byte length of text's last codepoint (0 when empty).
    inline size_t LastUtf8CodepointSize(std::string_view text)
    {
        size_t size = 0;
        while (size < text.size())
        {
            size++;
            if ((static_cast<unsigned char>(text[text.size() - size]) & 0xC0u) != 0x80u)
                break;
        }
        return size;
    }
}
