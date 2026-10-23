#pragma once

#include <cstddef>

namespace interstice
{
    // Memory functions a host gives the library. They receive the allocations that
    // can be routed: Vulkan objects, VMA, vk-bootstrap and SDL. They must outlive
    // the GUI; thread-safe, since drivers may call them from their own threads.
    // SDL can keep a few allocations past SDL_Quit (logged); it then keeps using
    // the hooks, so they should live as long as the process.
    struct AllocationHooks
    {
        // alignment is a power of two.
        void* (*allocate)(size_t size, size_t alignment, void* user) = nullptr;
        // ptr may be null (then it allocates); size is never 0. Keeps the alignment.
        void* (*reallocate)(void* ptr, size_t size, size_t alignment, void* user) = nullptr;
        // ptr may be null.
        void  (*free)(void* ptr, void* user) = nullptr;
        void* user = nullptr;

        bool IsSet() const { return allocate && reallocate && free; }
    };
}
