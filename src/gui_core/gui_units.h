#pragma once

#include <compare>
#include <stdint.h>

namespace interstice
{
    // Units (#52). Sizes in the API are CSS px: the library scales them by the UI scale,
    // as it does the stylesheet's. Positions, and sizes the library measured or computed,
    // are whole device pixels; so is every int2 and IntRect (positions, rects, extents).
    //     LayoutOverride{}.Width(200)                 // 200 CSS px: 300 pixels at 150%
    //     LayoutOverride{}.Width(Pixels{ measured })  // as is
    struct CssPx
    {
        int32_t value = 0;
        constexpr CssPx() = default;
        constexpr CssPx(int32_t v) : value(v) {}   // a plain int size is CSS px
        constexpr auto operator<=>(const CssPx&) const = default;
    };

    struct Pixels
    {
        int32_t value = 0;
        constexpr Pixels() = default;
        constexpr explicit Pixels(int32_t v) : value(v) {}
        constexpr auto operator<=>(const Pixels&) const = default;
    };
}
