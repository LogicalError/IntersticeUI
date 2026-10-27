#pragma once

#include <stdint.h>

namespace tachyon
{
    /// @addtogroup styling
    /// @{

    // ------------------------------------------------------------------------
    // StyleRef - References into style pools
    // ------------------------------------------------------------------------

    /// Index into StyleManager's layout style pool; Invalid (0) = none.
    enum class LayoutStyleIndex     : uint32_t { Invalid = 0 };
    /// Index into StyleManager's visual box style pool; Invalid (0) = none.
    enum class VisualBoxStyleIndex  : uint16_t { Invalid = 0 };
    /// Index into StyleManager's visual text style pool; Invalid (0) = none.
    enum class VisualTextStyleIndex : uint16_t { Invalid = 0 };

    /// A registered style, as StyleManager::Register returns it: indices into the
    /// shared style pools, cheap to copy. A theme reload (StyleManager::ClearAll)
    /// invalidates it.
    struct StyleRef
    {
        LayoutStyleIndex     layoutStyleIndex     = LayoutStyleIndex::Invalid;     ///< Index into layout style pool
        VisualBoxStyleIndex  visualBoxStyleIndex  = VisualBoxStyleIndex::Invalid;  ///< Index into visual box style pool
        VisualTextStyleIndex visualTextStyleIndex = VisualTextStyleIndex::Invalid; ///< Index into visual text style pool

        /// A reference to no style (all indices Invalid).
        static constexpr StyleRef Invalid() { return StyleRef{ LayoutStyleIndex::Invalid, VisualBoxStyleIndex::Invalid, VisualTextStyleIndex::Invalid }; }

        inline bool operator==(const StyleRef& rhs) const { return layoutStyleIndex == rhs.layoutStyleIndex && visualBoxStyleIndex == rhs.visualBoxStyleIndex && visualTextStyleIndex == rhs.visualTextStyleIndex; }
        inline bool operator!=(const StyleRef& rhs) const { return !(*this == rhs); }
    };
    /// @}
}