#pragma once

#include <stdint.h>

namespace GUI
{
    // ------------------------------------------------------------------------
    // StyleRef - References into style pools
    // ------------------------------------------------------------------------

    enum class LayoutStyleIndex     : uint32_t { Invalid = 0 };
    enum class VisualBoxStyleIndex  : uint16_t { Invalid = 0 };
    enum class VisualTextStyleIndex : uint16_t { Invalid = 0 };

    struct StyleRef
    {
        LayoutStyleIndex     layoutStyleIndex     = LayoutStyleIndex::Invalid;     // Index into layout style pool
        VisualBoxStyleIndex  visualBoxStyleIndex  = VisualBoxStyleIndex::Invalid;  // Index into visual box style pool
        VisualTextStyleIndex visualTextStyleIndex = VisualTextStyleIndex::Invalid; // Index into visual text style pool

        static constexpr StyleRef Invalid() { return StyleRef{ LayoutStyleIndex::Invalid, VisualBoxStyleIndex::Invalid, VisualTextStyleIndex::Invalid }; }

        inline bool operator==(const StyleRef& rhs) const { return layoutStyleIndex == rhs.layoutStyleIndex && visualBoxStyleIndex == rhs.visualBoxStyleIndex && visualTextStyleIndex == rhs.visualTextStyleIndex; }
        inline bool operator!=(const StyleRef& rhs) const { return !(*this == rhs); }
    };
}