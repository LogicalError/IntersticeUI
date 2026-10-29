#pragma once

#include <gui_core/gui_id.h>

#include <assert.h>
#include <stddef.h>
#include <new>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>

namespace tachyon
{
    /// @addtogroup elements
    /// @{

    /// Per-element typed state keyed by ID, kept across frames (drag start, double-click
    /// timing). Get, GetOrCreate and Touch keep an entry alive; EndFrame evicts the rest.
    /// GUIStateManager touches the IDs in a live interaction (capture, focus, drag, active),
    /// so their state survives frames without events. Types up to 64 bytes are stored inline.
    class ElementDataStore
    {
    public:
        ElementDataStore() = default;
        ElementDataStore(const ElementDataStore&) = delete;
        ElementDataStore& operator=(const ElementDataStore&) = delete;

        /// The T stored for `id`, constructed from `args` when absent. Touches the entry. One
        /// `id` holds one type: asserts on a type mismatch (release builds replace the entry).
        template<typename T, typename... Args>
        T& GetOrCreate(ID id, Args&&... args);

        /// The T stored for `id`, or nullptr (also on a type mismatch). Touches the entry if found.
        template<typename T>
        T* Get(ID id);

        /// Keeps `id`'s entry alive through this frame's EndFrame.
        void Touch(ID id);

        void Remove(ID id);

        void Clear();

        /// Starts a frame; call before any Get, GetOrCreate or Touch of the frame.
        void BeginFrame();

        /// Destroys the entries not touched since BeginFrame.
        void EndFrame();

        size_t EntryCount() const { return m_entries.size(); }

    private:
        /// Fits all built-in handler states (largest: SizerState, ~72 bytes).
        static constexpr size_t kInlineSize = 80;

        struct Entry
        {
            uint64_t        touchedFrame = 0;
            std::type_index typeIdx{ typeid(void) };

            alignas(std::max_align_t) std::byte inlineBuffer[kInlineSize];

            /// inlineBuffer or a heap allocation; nullptr = empty.
            void* data           = nullptr;
            void (*destroyer)(void*) = nullptr;
            bool  heapAllocated  = false;

            Entry() = default;
            Entry(const Entry&)            = delete;
            Entry& operator=(const Entry&) = delete;
            /// Non-movable: relocating the inline buffer needs T. unordered_map nodes never move.
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
    /// @}
}
