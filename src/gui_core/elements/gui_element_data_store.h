// Per-element typed state keyed by GUI::ID, persisted across frames (drag start,
// double-click timing, etc.).
//
// Lifetime: Get/GetOrCreate touch an entry; EndFrame evicts untouched entries.
// GUIStateManager touches IDs in a live interaction (capture, focus, drag, active)
// so their state survives frames without events.
//
// Storage: 64-byte inline buffer; larger types go to the heap.

#pragma once

#include <gui_core/gui_id.h>

#include <assert.h>
#include <stddef.h>
#include <new>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>

namespace GUI
{
    class ElementDataStore
    {
    public:
        ElementDataStore() = default;
        ElementDataStore(const ElementDataStore&) = delete;
        ElementDataStore& operator=(const ElementDataStore&) = delete;

        // Returns the existing T for id, or constructs one from args. Touches the entry.
        template<typename T, typename... Args>
        T& GetOrCreate(ID id, Args&&... args);

        // Existing T for id, or nullptr. Touches the entry if found.
        template<typename T>
        T* Get(ID id);

        // Keeps id alive through this frame's EndFrame eviction.
        void Touch(ID id);

        void Remove(ID id);

        void Clear();

        // Call at the start of each frame.
        void BeginFrame();

        // Evict entries not touched during the frame that just ended.
        void EndFrame();

        size_t EntryCount() const { return m_entries.size(); }

    private:
        // Fits all built-in handler states (largest: HorizontalSizerState, ~56 bytes).
        static constexpr size_t kInlineSize = 64;

        struct Entry
        {
            uint64_t        touchedFrame = 0;
            std::type_index typeIdx{ typeid(void) };

            alignas(std::max_align_t) std::byte inlineBuffer[kInlineSize];

            // inlineBuffer or a heap allocation; nullptr = empty.
            void* data           = nullptr;
            void (*destroyer)(void*) = nullptr;
            bool  heapAllocated  = false;

            Entry() = default;
            Entry(const Entry&)            = delete;
            Entry& operator=(const Entry&) = delete;
            // Non-movable: relocating the inline buffer needs T. unordered_map nodes never move.
            Entry(Entry&&)            = delete;
            Entry& operator=(Entry&&) = delete;

            ~Entry() { Reset(); }

            void Reset()
            {
                if (!data) return;
                destroyer(data); // also frees heap storage, with the type's own alignment
                data          = nullptr;
                destroyer     = nullptr;
                heapAllocated = false;
                typeIdx       = std::type_index(typeid(void));
            }
        };

        std::unordered_map<ID, Entry> m_entries;
        uint64_t m_currentFrame = 1;
    };

    template<typename T, typename... Args>
    T& ElementDataStore::GetOrCreate(ID id, Args&&... args)
    {
        // try_emplace constructs in place; Entry is non-movable.
        auto [it, _] = m_entries.try_emplace(id);
        Entry& entry = it->second;
        const std::type_index wanted{ typeid(T) };

        assert((!entry.data || entry.typeIdx == wanted) && "ID collision between different handler types");

        if (entry.data && entry.typeIdx == wanted)
        {
            entry.touchedFrame = m_currentFrame;
            return *static_cast<T*>(entry.data);
        }

        // Wrong-type entry (assertion violated in debug); rebuild for release.
        entry.Reset();

        void* mem;
        if constexpr (sizeof(T) <= kInlineSize && alignof(T) <= alignof(std::max_align_t))
        {
            mem = entry.inlineBuffer;
            entry.heapAllocated = false;
        }
        else
        {
            mem = ::operator new(sizeof(T), std::align_val_t{ alignof(T) });
            entry.heapAllocated = true;
        }

        T* ptr = ::new (mem) T(std::forward<Args>(args)...);
        entry.data         = ptr;
        if (entry.heapAllocated)
            entry.destroyer = +[](void* p)
            {
                static_cast<T*>(p)->~T();
                ::operator delete(p, std::align_val_t{ alignof(T) });
            };
        else
            entry.destroyer = +[](void* p) { static_cast<T*>(p)->~T(); };
        entry.typeIdx      = wanted;
        entry.touchedFrame = m_currentFrame;
        return *ptr;
    }

    template<typename T>
    T* ElementDataStore::Get(ID id)
    {
        auto it = m_entries.find(id);
        if (it == m_entries.end()) return nullptr;
        const std::type_index wanted{ typeid(T) };
        assert(it->second.typeIdx == wanted && "ID collision between different handler types");
        if (it->second.typeIdx != wanted) return nullptr;
        it->second.touchedFrame = m_currentFrame;
        return static_cast<T*>(it->second.data);
    }
}
