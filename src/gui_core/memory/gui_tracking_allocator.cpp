#include <gui_core/memory/gui_tracking_allocator.h>

#include <gui_core/diagnostics/gui_logging.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>

#ifdef _WIN32
#include <malloc.h>
#endif

static constexpr GUI::Diagnostics::LogCategory kLogMemory = GUI::Diagnostics::MakeLogCategory("memory");

namespace GUI
{
    struct TrackingAllocator::State
    {
        mutable std::mutex                 mutex;
        std::unordered_map<void*, size_t>  sizes;
        size_t                             liveBytes = 0;
        size_t                             total     = 0;
    };

    namespace
    {
        void* AlignedAllocate(size_t size, size_t alignment)
        {
            alignment = std::max(alignment, alignof(std::max_align_t));
#ifdef _WIN32
            return _aligned_malloc(size, alignment);
#else
            return std::aligned_alloc(alignment, (size + alignment - 1) / alignment * alignment);
#endif
        }

        void AlignedFree(void* ptr)
        {
#ifdef _WIN32
            _aligned_free(ptr);
#else
            std::free(ptr);
#endif
        }
    }

    TrackingAllocator::TrackingAllocator() : m_state(new State) {}

    TrackingAllocator::~TrackingAllocator()
    {
        ReportLeaks();
        delete m_state;
    }

    AllocationHooks TrackingAllocator::Hooks()
    {
        AllocationHooks hooks;
        hooks.user = m_state;
        hooks.allocate = [](size_t size, size_t alignment, void* user) -> void*
        {
            auto& state = *static_cast<State*>(user);
            void* ptr = AlignedAllocate(size, alignment);
            if (ptr == nullptr) return nullptr;
            std::lock_guard lock(state.mutex);
            state.sizes[ptr] = size;
            state.liveBytes += size;
            ++state.total;
            return ptr;
        };
        hooks.reallocate = [](void* ptr, size_t size, size_t alignment, void* user) -> void*
        {
            auto& state = *static_cast<State*>(user);
            size_t oldSize = 0;
            if (ptr != nullptr)
            {
                std::lock_guard lock(state.mutex);
                oldSize = state.sizes.at(ptr);
            }
            // Allocate, copy and free, so the alignment is kept on every platform.
            void* moved = AlignedAllocate(size, alignment);
            if (moved == nullptr) return nullptr;
            if (ptr != nullptr)
            {
                std::memcpy(moved, ptr, std::min(oldSize, size));
                AlignedFree(ptr);
            }
            std::lock_guard lock(state.mutex);
            if (ptr != nullptr)
            {
                state.sizes.erase(ptr);
                state.liveBytes -= oldSize;
            }
            else
                ++state.total;
            state.sizes[moved] = size;
            state.liveBytes += size;
            return moved;
        };
        hooks.free = [](void* ptr, void* user)
        {
            if (ptr == nullptr) return;
            auto& state = *static_cast<State*>(user);
            {
                std::lock_guard lock(state.mutex);
                const auto it = state.sizes.find(ptr);
                if (it != state.sizes.end())
                {
                    state.liveBytes -= it->second;
                    state.sizes.erase(it);
                }
            }
            AlignedFree(ptr);
        };
        return hooks;
    }

    size_t TrackingAllocator::LiveAllocations() const
    {
        std::lock_guard lock(m_state->mutex);
        return m_state->sizes.size();
    }

    size_t TrackingAllocator::LiveBytes() const
    {
        std::lock_guard lock(m_state->mutex);
        return m_state->liveBytes;
    }

    size_t TrackingAllocator::TotalAllocations() const
    {
        std::lock_guard lock(m_state->mutex);
        return m_state->total;
    }

    size_t TrackingAllocator::ReportLeaks() const
    {
        std::lock_guard lock(m_state->mutex);
        for (const auto& [ptr, size] : m_state->sizes)
            LOG_WARNING(kLogMemory, "leaked {} bytes at address {}", size, reinterpret_cast<uintptr_t>(ptr));
        return m_state->sizes.size();
    }
}
