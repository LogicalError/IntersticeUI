#pragma once

#include <gui_core/gui_base_types.h>
#include <gui_core/style/gui_style.h>

#include <stdint.h>
#include <vector>
#include <span>
#include <unordered_map>

namespace tachyon
{
    class Image;
    class GraphicsContext;

    /// Handle to a reference-counted GradientManager atlas slot. 0 is invalid; otherwise
    /// the high 24 bits are the 1-based slot index and the low 8 bits its generation.
    ///
    /// Each Acquire* (a new slot at refcount 1, or the existing handle on dedup)
    /// pairs with exactly one Release, as does each AddRef. At refcount 0 the slot
    /// is freed and its pixels may be reused.
    struct GradientHandle
    {
        uint32_t value = 0; ///< encoded slot index and generation

        constexpr GradientHandle() = default;
        constexpr explicit GradientHandle(uint32_t v) : value(v) {}

        /// Not the invalid handle; GradientManager::IsValid also checks it's still live.
        constexpr bool IsValid() const { return value != 0; }
        constexpr uint32_t SlotIndex() const { return value >> 8; } ///< 1-based; 0 = invalid
        constexpr uint8_t  Generation() const { return value & 0xFF; } ///< wraps around

        constexpr bool operator==(const GradientHandle&) const noexcept = default;

        static constexpr GradientHandle Invalid() { return GradientHandle(0); } ///< the invalid handle
        /// Encodes a slot index (1-based) and generation.
        static constexpr GradientHandle Make(uint32_t slotIndex1Based, uint8_t generation)
        {
            return GradientHandle((slotIndex1Based << 8) | uint32_t(generation));
        }
    };

    /// Where a handle's texels are in the atlas.
    struct AtlasLocation
    {
        uint16_t col   = 0; ///< starting column within the row (0..page.width-1)
        uint16_t row   = 0; ///< row index within the page (0..page.height-1)
        uint16_t width = 0; ///< number of columns (1..page.width)
        uint8_t  page  = 0; ///< logical page index (0..PageCount()-1)
    };

    /// The atlas that the colors of color styles are baked into: pages of 2D textures,
    /// one row per gradient, with solid and 2..4-color blocks packed into shared rows.
    /// Slots are reference counted and deduplicated by content; a slot is reclaimed at
    /// refcount 0 and may be reused by a later Acquire.
    ///
    /// A page doubles its height up to kMaxPageHeight; then a new page is added. A grown
    /// page's old GPU image is destroyed at the end of the frame.
    class GradientManager
    {
    public:
        /// Page height cap; a full page gets a new page instead of growing. Stays well
        /// under the usual maxImageDimension2D (16384 on desktop GPUs).
        static constexpr uint32_t kMaxPageHeight = 8192;

        /// GetPagePhysicalImageIndex of a page the renderer hasn't registered.
        static constexpr int32_t kUnregisteredPage = -1;

    public:
        /// An empty atlas whose pages are `width` texels wide.
        GradientManager(size_t width = 256);

        /// Destroys every GPU image of the atlas (deferred through `graphicsContext`).
        /// Safe to call more than once.
        void ReleaseAll(GraphicsContext* graphicsContext);

        /// @name Acquire
        /// A slot holding the colors (solid, 2..4-color block or full gradient row).
        /// Identical content returns the same handle with its refcount raised; each call
        /// pairs with one Release.
        ///@{
        GradientHandle AcquireSolidColor(const color4b& color);
        GradientHandle AcquireSolidColor(const color4f& color);

        GradientHandle AcquireColors(const color4b& c0, const color4b& c1);
        GradientHandle AcquireColors(const color4f& c0, const color4f& c1);

        GradientHandle AcquireColors(const color4b& c0, const color4b& c1, const color4b& c2);
        GradientHandle AcquireColors(const color4f& c0, const color4f& c1, const color4f& c2);

        GradientHandle AcquireColors(const color4b& c0, const color4b& c1, const color4b& c2, const color4b& c3);
        GradientHandle AcquireColors(const color4f& c0, const color4f& c1, const color4f& c2, const color4f& c3);

        GradientHandle AcquireGradient(const Gradient& g);
        ///@}

        /// Raises the refcount of a live handle; pair with one Release.
        void AddRef(GradientHandle handle);
        /// Lowers the refcount; true when it reached zero and the slot was reclaimed.
        bool Release(GradientHandle handle);

        /// The atlas position of a handle; all zero for an invalid or stale handle.
        AtlasLocation GetLocation(GradientHandle handle) const;

        /// True if the handle is live (slot occupied and generation matches).
        bool IsValid(GradientHandle handle) const;

        /// Refcount of a live handle; 0 for an invalid or stale handle.
        uint16_t GetRefcount(GradientHandle handle) const;

        /// Page width in texels, the same for every page.
        inline size_t Width() const noexcept { return m_width; }

        /// Number of pages allocated.
        inline uint32_t PageCount() const noexcept { return static_cast<uint32_t>(m_pages.size()); }

        /// Allocated height of a page in rows; the rows in use are at most this.
        size_t PageHeight(uint32_t pageIndex) const noexcept;

        /// CPU texels of a page, PageHeight x Width.
        std::span<const color4b> PageTextureData(uint32_t pageIndex) const noexcept;

        /// Writable CPU texels of a page; call MarkPageModified after writing.
        std::span<color4b> PageTextureData(uint32_t pageIndex) noexcept;
        /// Makes the next GetGradientImage re-upload the page.
        void MarkPageModified(uint32_t pageIndex) noexcept;

        /// @name Renderer page table
        /// The renderer registers each page in its bindless image set and records the
        /// 0-based shader slot here; color style uploads map logical pages through it.
        /// HasPageTableChanged is set when the mapping changes, so the color styles must be
        /// re-uploaded.
        ///@{
        int32_t GetPagePhysicalImageIndex(uint32_t pageIndex) const noexcept;
        void SetPagePhysicalImageIndex(uint32_t pageIndex, int32_t shaderSlotZeroBased) noexcept;
        bool HasPageTableChanged() const noexcept { return m_pageTableDirty; }
        void ClearPageTableChanged() noexcept { m_pageTableDirty = false; }
        ///@}

        /// Releases every slot; every outstanding GradientHandle becomes stale, so holders
        /// (StyleManager, the text system's caret) re-register. For a theme reload.
        void ClearAll() noexcept;

        /// The GPU image of a page, created or re-uploaded as needed; nullptr when
        /// `pageIndex` is out of range.
        tachyon::Image* GetGradientImage(GraphicsContext* graphicsContext, uint32_t pageIndex = 0);

    private:
        struct Slot
        {
            uint64_t contentHash = 0; ///< content hash key used in m_contentDedup (must be 64-bit to match map key)
            uint16_t refcount = 0;    ///< 0 = free
            uint16_t row = 0;
            uint16_t col = 0;
            uint16_t width = 0;       ///< 0 when free
            uint8_t  page = 0;
            uint8_t  generation = 0;  ///< incremented when slot is reclaimed
            bool     inUse = false;
        };

        struct Page
        {
            uint32_t width = 0;
            uint32_t height = 0;              ///< allocated rows in CPU buffer
            uint32_t rowsUsed = 0;            ///< rows occupied by allocations
            std::vector<color4b> data;         ///< width * height color4b
            Image* image = nullptr;
            bool modified = true;

            /// Cursor for packing small (<width) allocations into a shared row.
            /// -1 means "no open small row; next small alloc must open a new row".
            int32_t smallRow = -1;
            int32_t smallCursor = 0;           ///< column after last small alloc in smallRow

            int32_t physicalImageIndex = kUnregisteredPage; ///< 0-based shader slot, set by renderer
        };

        // --- helpers ---
        GradientHandle AcquireSmall(uint16_t width, uint64_t contentHash,
                                    const color4b* colors);
        GradientHandle AcquireRow(uint64_t contentHash,
                                  std::span<const color4b> rowPixels);

        /// Returns slot index (not handle) or -1 on allocation failure.
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
        std::vector<uint32_t>       m_freeSlotStorage; ///< reusable m_slots entries

        /// Free lists of (allocated-but-refcount==0) slots bucketed by width.
        /// kind 0 = full-row (width == m_width), kind 1..4 = width 1..4.
        /// Anything else would need its own bucket in the future.
        static constexpr uint32_t kFreeListCount = 6;
        std::vector<uint32_t>       m_freeLists[kFreeListCount]; ///< slot indices

        /// Dedup: content hash -> slot index.
        std::unordered_map<uint64_t, uint32_t> m_contentDedup;

        bool m_pageTableDirty = true; ///< true whenever page count / mapping changes
    };
}
