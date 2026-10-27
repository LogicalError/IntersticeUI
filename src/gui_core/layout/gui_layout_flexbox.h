#pragma once

#include <stdint.h>

#include <span>

namespace tachyon
{
    /// @addtogroup layout
    /// @{

    struct IntRect;
	struct ElementRect;

    struct LayoutFlexbox
    {
        /// Returns the content width needed to contain the children.
        static int32_t HorizontalSizing(const ElementRect&           parentRect,
                                        std::span<ElementRect> childRects);
        
        /// Returns the content height needed to contain the children.
        static int32_t VerticalSizing(const ElementRect&           parentRect,
                                      std::span<ElementRect> childRects);
        
        static void HorizontalPositioning(const ElementRect&     parentRect,
                                          std::span<ElementRect> childRects);
        
        /// Returns the content bounds.
        static IntRect VerticalPositioning(const ElementRect&     parentRect,
                                           const IntRect&         clipRect,
                                           std::span<ElementRect> childRects);
    };
    /// @}
} // namespace tachyon