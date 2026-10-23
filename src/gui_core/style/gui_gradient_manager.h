#pragma once

#include <gui_core/gui_base_types.h>
#include <gui_core/style/gui_style.h>

#include <stdint.h>
#include <vector>
#include <span>
#include <unordered_map>

namespace interstice
{
    class Image;
    class GraphicsContext;

    // Opaque handle to a reference-counted atlas slot.
    //
    // Encoding:  value == 0    → invalid
    //            high 24 bits  → 1-based slot index (1..2^24-1)
    //            low 8 bits    → generation (wraps)
    //
    // Each Acquire* (a new slot at refcount 1, or the existing handle on dedup)
    // pairs with exactly one Release. At refcount 0 the slot goes to a per-width
    // free list and its pixels may be reused. AddRef shares a handle.
    struct GradientHandle
    {
        uint32_t value = 0;

        constexpr GradientHandle() = default;
        constexpr explicit GradientHandle(uint32_t v) : value(v) {}

        constexpr bool IsValid() const { return value != 0; }
        constexpr uint32_t SlotIndex() const { return value >> 8; }        // 1-based; 0 == invalid
        constexpr uint8_t  Generation() const { return value & 0xFF; }

        constexpr bool operator==(const GradientHandle&) const noexcept = default;

        static constexpr GradientHandle Invalid() { return GradientHandle(0); }
        static constexpr GradientHandle Make(uint32_t slotIndex1Based, uint8_t generation)
        {
            return GradientHandle((slotIndex1Based << 8) | uint32_t(generation));
        }
    };

    // Where a handle lives in the atlas. Width is always 1..Page::width; Column
    // is always within the row. Page is the logical page index (0..PageCount).
    struct AtlasLocation
    {
        uint16_t col   = 0; // starting column within the row (0..page.width-1)
        uint16_t row   = 0; // row index within the page (0..page.height-1)
        uint16_t width = 0; // number of columns (1..page.width)
        uint8_t  page  = 0; // logical page index
    };

    // Manages a 1D gradient atlas texture built out of one or more pages (each
    // page is an independent 2D texture). Acquired handles are reference
    // counted; slots are reclaimed when refcount hits zero and may be reused
    // by later Acquire calls.
    //
    // Atlas growth policy:
    //   - Each page grows its height by doubling up to kMaxPageHeight.
    //   - When the last page is exhausted, a new page is allocated.
    //   - When a page's CPU buffer grows, its old GPU image is queued for
    //     deletion via GraphicsContext::DestroyImageAtEnd(..., DeviceScope::Frame)
    //     so no GPU resources leak.
    //
    // Packing:
    //   - Gradients take one full row (width = page.width).
    //   - Solid / 2-color / 3-color / 4-color blocks are packed into shared
    //     "small" rows with per-width free lists. Slot records its (col, width)
    //     explicitly so future partial-row gradient packing can reuse the same
    //     free-list machinery without public API changes.
    class GradientManager
    {
    public:
        // Hard cap per-page height. Once a page reaches this size a new page
        // is allocated instead of growing further. Tuned to stay well under
        // typical maxImageDimension2D (usually 16384 on desktop GPUs).
        static constexpr uint32_t kMaxPageHeight = 8192;

        // Sentinel for "no page currently registered with the renderer".
        static constexpr int32_t kUnregisteredPage = -1;

    public:
        // Create an atlas with a given page width. All pages share the same width.
        GradientManager(size_t width = 256);

        // Destroy all GPU images owned by the atlas (deferred via GraphicsContext).
        // Safe to call multiple times.
        void ReleaseAll(GraphicsContext* graphicsContext);

        // Acquire / release refcounted atlas slots. Identical content dedupes
        // to the same handle with bumped refcount. Each successful Acquire must
        // eventually be matched by a Release (or AddRef'd ownership transfers).
        GradientHandle AcquireSolidColor(const color4b& color);
        GradientHandle AcquireSolidColor(const color4f& color);

        GradientHandle AcquireColors(const color4b& c0, const color4b& c1);
        GradientHandle AcquireColors(const color4f& c0, const color4f& c1);

        GradientHandle AcquireColors(const color4b& c0, const color4b& c1, const color4b& c2);
        GradientHandle AcquireColors(const color4f& c0, const color4f& c1, const color4f& c2);

        GradientHandle AcquireColors(const color4b& c0, const color4b& c1, const color4b& c2, const color4b& c3);
        GradientHandle AcquireColors(const color4f& c0, const color4f& c1, const color4f& c2, const color4f& c3);

        GradientHandle AcquireGradient(const Gradient& g);

        // Bump/decrement the refcount of an existing handle. Release returns true
        // if the refcount reached zero and the slot was reclaimed.
        void AddRef(GradientHandle handle);
        bool Release(GradientHandle handle);

        // Resolve a handle to its atlas position. Returns an all-zero AtlasLocation
        // for invalid / stale handles.
        AtlasLocation GetLocation(GradientHandle handle) const;

        // True if the handle is live (slot occupied and generation matches).
        bool IsValid(GradientHandle handle) const;

        // Current refcount for a live handle. Returns 0 for invalid/stale handles.
        uint16_t GetRefcount(GradientHandle handle) const;

        // Atlas width (shared across all pages).
        inline size_t Width() const noexcept { return m_width; }

        // Number of pages currently allocated.
        inline uint32_t PageCount() const noexcept { return static_cast<uint32_t>(m_pages.size()); }

        // Current allocated height of a page (rows actually used is <= this).
        size_t PageHeight(uint32_t pageIndex) const noexcept;

        // Full CPU texture data view for a page (rows * width color4b entries).
        std::span<const color4b> PageTextureData(uint32_t pageIndex) const noexcept;

        // Mutable CPU view of a page (caller must call MarkPageModified after writes).
        std::span<color4b> PageTextureData(uint32_t pageIndex) noexcept;
        void MarkPageModified(uint32_t pageIndex) noexcept;

        // ---- Renderer page-index bookkeeping ------------------------------
        // The renderer registers each page in its bindless image set and records
        // the 0-based shader slot here; uploads map logical pages to it.
        // HasPageTableChanged signals the mapping changed and the VCS buffer needs re-uploading.
        int32_t GetPagePhysicalImageIndex(uint32_t pageIndex) const noexcept;
        void SetPagePhysicalImageIndex(uint32_t pageIndex, int32_t shaderSlotZeroBased) noexcept;
        bool HasPageTableChanged() const noexcept { return m_pageTableDirty; }
        void ClearPageTableChanged() noexcept { m_pageTableDirty = false; }

        // Force-release every slot. All outstanding GradientHandles become
        // stale. Intended for theme/atlas hot-reload: external holders (style
        // manager, skribidi caret, etc.) must re-register afterwards.
        void ClearAll() noexcept;

        // Returns the GPU image for a page, allocating / re-uploading as needed.
        // Nullptr if pageIndex is out of range.
        interstice::Image* GetGradientImage(GraphicsContext* graphicsContext, uint32_t pageIndex = 0);

    private:
        struct Slot
        {
            uint64_t contentHash = 0; // content hash key used in m_contentDedup (must be 64-bit to match map key)
            uint16_t refcount = 0;    // 0 = free
            uint16_t row = 0;
            uint16_t col = 0;
            uint16_t width = 0;       // 0 when free
            uint8_t  page = 0;
            uint8_t  generation = 0;  // incremented when slot is reclaimed
            bool     inUse = false;
        };

        struct Page
        {
            uint32_t width = 0;
            uint32_t height = 0;              // allocated rows in CPU buffer
            uint32_t rowsUsed = 0;            // rows occupied by allocations
            std::vector<color4b> data;         // width * height color4b
            Image* image = nullptr;
            bool modified = true;

            // Cursor for packing small (<width) allocations into a shared row.
            // -1 means "no open small row; next small alloc must open a new row".
            int32_t smallRow = -1;
            int32_t smallCursor = 0;           // column after last small alloc in smallRow

            int32_t physicalImageIndex = kUnregisteredPage; // 0-based shader slot, set by renderer
        };

        // --- helpers ---
        GradientHandle AcquireSmall(uint16_t width, uint64_t contentHash,
                                    const color4b* colors);
        GradientHandle AcquireRow(uint64_t contentHash,
                                  std::span<const color4b> rowPixels);

        // Returns slot index (not handle) or -1 on allocation failure.
        int32_t AllocateSmallSlot(uint16_t width);
        int32_t AllocateRowSlot();

        uint32_t AllocateSlotStorage();    // returns slot index (into m_slots)
        void     FreeSlotStorage(uint32_t slotIdx);

        bool GrowOrAddPage(uint32_t& outPageIdx, uint32_t& outRow);   // for full-row allocs
        bool EnsureSmallRowAvailable(uint32_t& outPageIdx, uint16_t width);

        Slot* GetSlot(GradientHandle handle);
        const Slot* GetSlot(GradientHandle handle) const;

        void ReleaseSlot(uint32_t slotIdx);

        static uint64_t HashGradient(const Gradient& g);
        static uint64_t HashSmall(uint16_t width, const color4b* colors);

        size_t                      m_width;
        std::vector<Page>           m_pages;
        std::vector<Slot>           m_slots;
        std::vector<uint32_t>       m_freeSlotStorage; // reusable m_slots entries

        // Free lists of (allocated-but-refcount==0) slots bucketed by width.
        // kind 0 = full-row (width == m_width), kind 1..4 = width 1..4.
        // Anything else would need its own bucket in the future.
        static constexpr uint32_t kFreeListCount = 6;
        std::vector<uint32_t>       m_freeLists[kFreeListCount]; // slot indices

        // Dedup: content hash → slot index.
        std::unordered_map<uint64_t, uint32_t> m_contentDedup;

        bool m_pageTableDirty = true; // true whenever page count / mapping changes
    };
}
