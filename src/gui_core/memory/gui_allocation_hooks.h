#pragma once

#include <cstddef>

namespace interstice
{
    /// Memory functions a host gives the library, for the allocations that can be routed:
    /// Vulkan objects, VMA, vk-bootstrap and SDL. They must be thread-safe (drivers call
    /// them from their own threads) and stay valid for the rest of the process: SDL can keep
    /// a few allocations past SDL_Quit (logged) and keeps using the hooks.
    struct AllocationHooks
    {
        /// Returns `size` bytes aligned to `alignment` (a power of two).
        void* (*allocate)(size_t size, size_t alignment, void* user) = nullptr;
        /// Resizes `ptr`, keeping its alignment; `ptr` may be null (then it allocates). `size`
        /// is never 0.
        void* (*reallocate)(void* ptr, size_t size, size_t alignment, void* user) = nullptr;
        /// Frees `ptr`, which may be null.
        void  (*free)(void* ptr, void* user) = nullptr;
        void* user = nullptr;   ///< passed to every call

        /// The hooks are used only when all three functions are set.
        bool IsSet() const { return allocate && reallocate && free; }
    };
}
