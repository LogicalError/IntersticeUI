#include <gui_core/gui_base_types.h>
#include <gui_core/gui_hash.h>
#include <gui_core/style/gui_style_text.h>

namespace tachyon
{
    FontID FontIDFromName(std::string_view familyName)
    {
        if (familyName.empty())
            return FontID::Invalid;
        uint64_t hash = Hash::Basis;
        for (char c : familyName)
        {
            hash ^= static_cast<uint64_t>((c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c);
            hash *= Hash::Prime;
        }
        const uint32_t folded = static_cast<uint32_t>(hash ^ (hash >> 32));
        return static_cast<FontID>(folded != 0 ? folded : 1);
    }

    uint64_t TextLayoutStyle::GetHashCode() const noexcept
    {
        return Hash::Combine(Hash::Calculate(textDirection),
                             Hash::Calculate(textWrapping),
                             Hash::Calculate(horizontalAlignment),
                             Hash::Calculate(verticalAlignment),
                             Hash::Calculate(baseline),
                             Hash::Calculate(verticalTrim),
                             Hash::Calculate(textOverflow),
                             Hash::Calculate(wrapMode));
    }

    uint64_t TextDecoration::GetHashCode() const noexcept
    {
        if (thickness <= 0)
        {
            static uint64_t defaultEmptyHash = Hash::Combine(Hash::Calculate(DecorationStyle::Solid),
                                                                Hash::Calculate(0),
                                                                Hash::Calculate(0),
                                                                Hash::Calculate(tachyon::color4b { 0,0,0,0 }));
            return defaultEmptyHash;
        }
        return Hash::Combine(Hash::Calculate(style),
                                Hash::Calculate(thickness),
                                Hash::Calculate(offset),
                                Hash::Calculate(color));
    }

    uint64_t TextStyle::GetHashCode() const noexcept
    {
        return Hash::Combine(Hash::Calculate(family),
                                Hash::Calculate(color),
                                Hash::Calculate(fontSize),
                                Hash::Calculate(weight),
                                Hash::Calculate(style),
                                Hash::Calculate(stretch),
                                Hash::Calculate(lineHeightType),
                                Hash::Calculate(lineHeight),
                                Hash::Calculate(letterSpacing),
                                Hash::Calculate(wordSpacing),
                                Hash::Calculate(fontId),
                                Hash::Calculate(language.view()),
                                throughLine.GetHashCode(),
                                bottomLine.GetHashCode(),
                                underLine.GetHashCode(),
                                overLine.GetHashCode());
    }

}//namespace tachyon
