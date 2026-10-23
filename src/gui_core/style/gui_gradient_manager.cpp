#include <gui_core/gui_hash.h>
#include <gui_core/style/gui_gradient_manager.h>

#include <gui_core/graphics/gui_image.h>
#include <gui_core/graphics/gui_graphics_context.h>

#include <algorithm>
#include <assert.h>
#include <cstring>
#include <bit>

namespace interstice
{
    namespace
    {
        constexpr uint32_t kInitialPageHeight = 64; // start small; we double as needed

        inline uint32_t FreeListBucket(uint16_t width, uint32_t pageWidth)
        {
            if (width == pageWidth) return 0;
            if (width >= 1 && width <= 4) return uint32_t(width); // buckets 1..4
            return 5; // catch-all bucket for "other" widths (unused for now)
        }

        inline color4b ToByte(const color4f& c)
        {
            auto pack = [](float v) -> byte
            {
                int iv = int(v * 255.0f + 0.5f);
                if (iv < 0) iv = 0; else if (iv > 255) iv = 255;
                return static_cast<byte>(iv);
            };
            return color4b{ pack(c.r), pack(c.g), pack(c.b), pack(c.a) };
        }

        // Refcounts saturate here instead of wrapping: a wrap would make the
        // slot read as unreferenced and the next release would free it while
        // live handles still point at its pixels. A pinned slot is leaked
        // deliberately — the count can no longer be trusted.
        constexpr uint16_t kPinnedRefcount = 0xFFFF;
    } // anonymous namespace

    uint64_t GradientManager::HashGradient(const Gradient& g)
    {
        return Hash::Calculate(g);
    }

    uint64_t GradientManager::HashSmall(uint16_t width, const color4b* colors)
    {
        // Distinguish small-bucket hashes from full-row gradient hashes by
        // salting with the width; otherwise a solid-white pixel and a full
        // row of white would both hash to the same dedup key.
        uint64_t h = Hash::Combine(uint64_t(0x5A4711C0DEULL), uint64_t(width));
        for (uint16_t i = 0; i < width; ++i)
            h = Hash::Combine(h, Hash::Calculate(colors[i]));
        return h;
    }

    GradientManager::GradientManager(size_t width)
        : m_width(width)
    {
        assert(width > 0 && width <= 0xFFFF);

        // Slot index 0 is reserved so that a zero-valued GradientHandle is
        // always invalid. Push a placeholder slot at index 0.
        m_slots.push_back(Slot{});
        m_slots[0].inUse = false;

        Page p;
        p.width = uint32_t(width);
        p.height = kInitialPageHeight;
        p.data.assign(size_t(p.width) * size_t(p.height), color4b{ 0, 0, 0, 0 });
        m_pages.push_back(std::move(p));
    }

    void GradientManager::ReleaseAll(GraphicsContext* graphicsContext)
    {
        for (auto& page : m_pages)
        {
            if (page.image != nullptr)
            {
                if (graphicsContext != nullptr)
                    graphicsContext->DestroyImageAtEnd(page.image, interstice::DeviceScope::Application);
                page.image = nullptr;
            }
        }
    }

    void GradientManager::ClearAll() noexcept
    {
        // Releases every live slot but keeps GPU images; the next frame reuploads
        // into them unless the caller calls ReleaseAll first.
        for (uint32_t i = 1; i < m_slots.size(); ++i)
        {
            Slot& s = m_slots[i];
            if (s.inUse && s.refcount > 0)
            {
                s.refcount = 0;
                ReleaseSlot(i);
            }
        }
        m_contentDedup.clear();
        for (auto& list : m_freeLists) list.clear();
        m_freeSlotStorage.clear();

        // Reset all pages' allocators (but keep page storage).
        for (auto& page : m_pages)
        {
            std::fill(page.data.begin(), page.data.end(), color4b{ 0, 0, 0, 0 });
            page.rowsUsed = 0;
            page.smallRow = -1;
            page.smallCursor = 0;
            page.modified = true;
        }

        // Keep slot 0 reserved.
        m_slots.clear();
        m_slots.push_back(Slot{});
    }

    // --------------------------------------------------------------------
    // Slot storage helpers
    // --------------------------------------------------------------------

    uint32_t GradientManager::AllocateSlotStorage()
    {
        if (!m_freeSlotStorage.empty())
        {
            uint32_t idx = m_freeSlotStorage.back();
            m_freeSlotStorage.pop_back();
            return idx;
        }

        uint32_t idx = static_cast<uint32_t>(m_slots.size());
        m_slots.push_back(Slot{});
        return idx;
    }

    void GradientManager::FreeSlotStorage(uint32_t slotIdx)
    {
        assert(slotIdx != 0);
        Slot& s = m_slots[slotIdx];
        s.inUse = false;
        s.refcount = 0;
        s.width = 0;
        m_freeSlotStorage.push_back(slotIdx);
    }

    // --------------------------------------------------------------------
    // Page growth
    // --------------------------------------------------------------------

    bool GradientManager::GrowOrAddPage(uint32_t& outPageIdx, uint32_t& outRow)
    {
        // Try to allocate a new row on the last page; grow or add as needed.
        uint32_t pageIdx = static_cast<uint32_t>(m_pages.size() - 1);
        Page* page = &m_pages[pageIdx];

        if (page->rowsUsed >= page->height)
        {
            // Try to grow
            if (page->height < kMaxPageHeight)
            {
                uint32_t newHeight = std::min(kMaxPageHeight, page->height * 2);
                if (newHeight <= page->height) newHeight = page->height + 1;
                // Append transparent rows
                page->data.resize(size_t(page->width) * size_t(newHeight), color4b{ 0, 0, 0, 0 });
                page->height = newHeight;
                page->modified = true;
            }
            else
            {
                // Need a new page
                Page p;
                p.width = uint32_t(m_width);
                p.height = kInitialPageHeight;
                p.data.assign(size_t(p.width) * size_t(p.height), color4b{ 0, 0, 0, 0 });
                m_pages.push_back(std::move(p));
                m_pageTableDirty = true;
                pageIdx = static_cast<uint32_t>(m_pages.size() - 1);
                page = &m_pages[pageIdx];
            }
        }

        outPageIdx = pageIdx;
        outRow = page->rowsUsed++;
        page->modified = true;
        return true;
    }

    bool GradientManager::EnsureSmallRowAvailable(uint32_t& outPageIdx, uint16_t width)
    {
        // Return a page whose smallRow has room for `width` columns. Opens
        // a new small row on the last page (possibly growing/adding pages)
        // if none is open or the current one is full.
        uint32_t pageIdx = static_cast<uint32_t>(m_pages.size() - 1);
        Page* page = &m_pages[pageIdx];

        if (page->smallRow < 0 || page->smallCursor + width > int32_t(page->width))
        {
            uint32_t row;
            if (!GrowOrAddPage(pageIdx, row)) return false;
            page = &m_pages[pageIdx];
            page->smallRow = int32_t(row);
            page->smallCursor = 0;
        }

        outPageIdx = pageIdx;
        return true;
    }

    // --------------------------------------------------------------------
    // Slot allocation
    // --------------------------------------------------------------------

    int32_t GradientManager::AllocateSmallSlot(uint16_t width)
    {
        // First: try the width-specific free list
        uint32_t bucket = FreeListBucket(width, uint32_t(m_width));
        assert(bucket != 0); // full-row allocs go through AllocateRowSlot
        if (!m_freeLists[bucket].empty())
        {
            uint32_t slotIdx = m_freeLists[bucket].back();
            m_freeLists[bucket].pop_back();
            Slot& s = m_slots[slotIdx];
            s.inUse = true;
            s.refcount = 0;
            // row/col/page/width are retained from prior use — content will be overwritten
            return int32_t(slotIdx);
        }

        // Otherwise: pack into a shared small row on some page
        uint32_t pageIdx;
        if (!EnsureSmallRowAvailable(pageIdx, width))
            return -1;

        Page& page = m_pages[pageIdx];
        uint16_t col = uint16_t(page.smallCursor);
        uint16_t row = uint16_t(page.smallRow);
        page.smallCursor += width;

        uint32_t slotIdx = AllocateSlotStorage();
        Slot& s = m_slots[slotIdx];
        s.row = row;
        s.col = col;
        s.width = width;
        s.page = uint8_t(pageIdx);
        s.inUse = true;
        s.refcount = 0;
        // generation is left as-is (carried across reuse)
        return int32_t(slotIdx);
    }

    int32_t GradientManager::AllocateRowSlot()
    {
        // Free list first
        if (!m_freeLists[0].empty())
        {
            uint32_t slotIdx = m_freeLists[0].back();
            m_freeLists[0].pop_back();
            Slot& s = m_slots[slotIdx];
            s.inUse = true;
            s.refcount = 0;
            // row/col/page/width retained; pixels about to be rewritten
            return int32_t(slotIdx);
        }

        uint32_t pageIdx;
        uint32_t row;
        if (!GrowOrAddPage(pageIdx, row))
            return -1;

        uint32_t slotIdx = AllocateSlotStorage();
        Slot& s = m_slots[slotIdx];
        s.row = uint16_t(row);
        s.col = 0;
        s.width = uint16_t(m_width);
        s.page = uint8_t(pageIdx);
        s.inUse = true;
        s.refcount = 0;
        return int32_t(slotIdx);
    }

    // --------------------------------------------------------------------
    // Public acquire helpers
    // --------------------------------------------------------------------

    GradientHandle GradientManager::AcquireSmall(uint16_t width, uint64_t contentHash, const color4b* colors)
    {
        // Dedup
        auto it = m_contentDedup.find(contentHash);
        if (it != m_contentDedup.end())
        {
            uint32_t slotIdx = it->second;
            Slot& s = m_slots[slotIdx];
            if (s.inUse && s.width == width)
            {
                if (s.refcount < kPinnedRefcount) s.refcount++;
                return GradientHandle::Make(slotIdx, s.generation);
            }
        }

        int32_t slotIdx32 = AllocateSmallSlot(width);
        if (slotIdx32 < 0) return GradientHandle::Invalid();
        uint32_t slotIdx = uint32_t(slotIdx32);

        Slot& s = m_slots[slotIdx];
        s.refcount = 1;
        s.contentHash = contentHash;

        // Write pixels
        Page& page = m_pages[s.page];
        color4b* dst = &page.data[size_t(s.row) * size_t(page.width) + size_t(s.col)];
        for (uint16_t i = 0; i < width; ++i) dst[i] = colors[i];
        page.modified = true;

        m_contentDedup[contentHash] = slotIdx;
        return GradientHandle::Make(slotIdx, s.generation);
    }

    GradientHandle GradientManager::AcquireRow(uint64_t contentHash, std::span<const color4b> rowPixels)
    {
        assert(rowPixels.size() == m_width);

        auto it = m_contentDedup.find(contentHash);
        if (it != m_contentDedup.end())
        {
            uint32_t slotIdx = it->second;
            Slot& s = m_slots[slotIdx];
            if (s.inUse && s.width == m_width)
            {
                if (s.refcount < kPinnedRefcount) s.refcount++;
                return GradientHandle::Make(slotIdx, s.generation);
            }
        }

        int32_t slotIdx32 = AllocateRowSlot();
        if (slotIdx32 < 0) return GradientHandle::Invalid();
        uint32_t slotIdx = uint32_t(slotIdx32);

        Slot& s = m_slots[slotIdx];
        s.refcount = 1;
        s.contentHash = contentHash;

        Page& page = m_pages[s.page];
        color4b* dst = &page.data[size_t(s.row) * size_t(page.width)];
        std::memcpy(dst, rowPixels.data(), sizeof(color4b) * m_width);
        page.modified = true;

        m_contentDedup[contentHash] = slotIdx;
        return GradientHandle::Make(slotIdx, s.generation);
    }

    GradientHandle GradientManager::AcquireSolidColor(const color4b& c)
    {
        uint64_t h = HashSmall(1, &c);
        return AcquireSmall(1, h, &c);
    }

    GradientHandle GradientManager::AcquireSolidColor(const color4f& c)
    {
        auto cb = ToByte(c);
        return AcquireSolidColor(cb);
    }

    GradientHandle GradientManager::AcquireColors(const color4b& c0, const color4b& c1)
    {
        color4b arr[2] = { c0, c1 };
        uint64_t h = HashSmall(2, arr);
        return AcquireSmall(2, h, arr);
    }

    GradientHandle GradientManager::AcquireColors(const color4f& c0, const color4f& c1)
    {
        return AcquireColors(ToByte(c0), ToByte(c1));
    }

    GradientHandle GradientManager::AcquireColors(const color4b& c0, const color4b& c1, const color4b& c2)
    {
        color4b arr[3] = { c0, c1, c2 };
        uint64_t h = HashSmall(3, arr);
        return AcquireSmall(3, h, arr);
    }

    GradientHandle GradientManager::AcquireColors(const color4f& c0, const color4f& c1, const color4f& c2)
    {
        return AcquireColors(ToByte(c0), ToByte(c1), ToByte(c2));
    }

    GradientHandle GradientManager::AcquireColors(const color4b& c0, const color4b& c1, const color4b& c2, const color4b& c3)
    {
        color4b arr[4] = { c0, c1, c2, c3 };
        uint64_t h = HashSmall(4, arr);
        return AcquireSmall(4, h, arr);
    }

    GradientHandle GradientManager::AcquireColors(const color4f& c0, const color4f& c1, const color4f& c2, const color4f& c3)
    {
        return AcquireColors(ToByte(c0), ToByte(c1), ToByte(c2), ToByte(c3));
    }

    GradientHandle GradientManager::AcquireGradient(const Gradient& g)
    {
        if (g.type == GradientType::None || g.stopCount == 0)
            return GradientHandle::Invalid();

        uint64_t h = HashGradient(g);

        // Dedup via content map before rasterizing.
        auto it = m_contentDedup.find(h);
        if (it != m_contentDedup.end())
        {
            uint32_t slotIdx = it->second;
            Slot& s = m_slots[slotIdx];
            if (s.inUse && s.width == m_width)
            {
                if (s.refcount < kPinnedRefcount) s.refcount++;
                return GradientHandle::Make(slotIdx, s.generation);
            }
        }

        // Rasterize into a temporary row-sized buffer, then hand off to AcquireRow.
        std::vector<color4b> tmp(m_width);
        g.WriteTo(std::span<color4b>(tmp.data(), tmp.size()));
        return AcquireRow(h, tmp);
    }

    // --------------------------------------------------------------------
    // Refcount / release
    // --------------------------------------------------------------------

    void GradientManager::AddRef(GradientHandle handle)
    {
        Slot* s = GetSlot(handle);
        if (s == nullptr) return;
        if (s->refcount < kPinnedRefcount) s->refcount++;
    }

    bool GradientManager::Release(GradientHandle handle)
    {
        Slot* s = GetSlot(handle);
        if (s == nullptr) return false;
        assert(s->refcount > 0);
        if (s->refcount == kPinnedRefcount) return false; // pinned — count no longer trustworthy
        if (--s->refcount == 0)
        {
            ReleaseSlot(handle.SlotIndex());
            return true;
        }
        return false;
    }

    void GradientManager::ReleaseSlot(uint32_t slotIdx)
    {
        Slot& s = m_slots[slotIdx];
        assert(s.inUse);

        // Remove from dedup map if we are the entry there.
        auto it = m_contentDedup.find(s.contentHash);
        if (it != m_contentDedup.end() && it->second == slotIdx)
            m_contentDedup.erase(it);

        // Bump generation so outstanding handles become stale.
        s.generation++;
        s.contentHash = 0;
        s.refcount = 0;

        // Push to appropriate free list (retaining page/row/col/width so the
        // next allocator can reuse the same pixels without rewriting them
        // unless the content differs).
        uint32_t bucket = FreeListBucket(s.width, uint32_t(m_width));
        m_freeLists[bucket].push_back(slotIdx);
        s.inUse = false;
    }

    GradientManager::Slot* GradientManager::GetSlot(GradientHandle handle)
    {
        if (!handle.IsValid()) return nullptr;
        uint32_t idx = handle.SlotIndex();
        if (idx == 0 || idx >= m_slots.size()) return nullptr;
        Slot& s = m_slots[idx];
        if (!s.inUse || s.generation != handle.Generation()) return nullptr;
        return &s;
    }

    const GradientManager::Slot* GradientManager::GetSlot(GradientHandle handle) const
    {
        if (!handle.IsValid()) return nullptr;
        uint32_t idx = handle.SlotIndex();
        if (idx == 0 || idx >= m_slots.size()) return nullptr;
        const Slot& s = m_slots[idx];
        if (!s.inUse || s.generation != handle.Generation()) return nullptr;
        return &s;
    }

    AtlasLocation GradientManager::GetLocation(GradientHandle handle) const
    {
        const Slot* s = GetSlot(handle);
        if (s == nullptr) return AtlasLocation{};
        return AtlasLocation
        {
            .col   = s->col,
            .row   = s->row,
            .width = s->width,
            .page  = s->page
        };
    }

    bool GradientManager::IsValid(GradientHandle handle) const
    {
        return GetSlot(handle) != nullptr;
    }

    uint16_t GradientManager::GetRefcount(GradientHandle handle) const
    {
        const Slot* s = GetSlot(handle);
        return s ? s->refcount : uint16_t(0);
    }

    // --------------------------------------------------------------------
    // Page data / image access
    // --------------------------------------------------------------------

    size_t GradientManager::PageHeight(uint32_t pageIndex) const noexcept
    {
        if (pageIndex >= m_pages.size()) return 0;
        return m_pages[pageIndex].height;
    }

    std::span<const color4b> GradientManager::PageTextureData(uint32_t pageIndex) const noexcept
    {
        if (pageIndex >= m_pages.size()) return {};
        const auto& p = m_pages[pageIndex];
        return std::span<const color4b>(p.data.data(), p.data.size());
    }

    std::span<color4b> GradientManager::PageTextureData(uint32_t pageIndex) noexcept
    {
        if (pageIndex >= m_pages.size()) return {};
        auto& p = m_pages[pageIndex];
        return std::span<color4b>(p.data.data(), p.data.size());
    }

    void GradientManager::MarkPageModified(uint32_t pageIndex) noexcept
    {
        if (pageIndex >= m_pages.size()) return;
        m_pages[pageIndex].modified = true;
    }

    int32_t GradientManager::GetPagePhysicalImageIndex(uint32_t pageIndex) const noexcept
    {
        if (pageIndex >= m_pages.size()) return kUnregisteredPage;
        return m_pages[pageIndex].physicalImageIndex;
    }

    void GradientManager::SetPagePhysicalImageIndex(uint32_t pageIndex, int32_t shaderSlot) noexcept
    {
        if (pageIndex >= m_pages.size()) return;
        if (m_pages[pageIndex].physicalImageIndex != shaderSlot)
        {
            m_pages[pageIndex].physicalImageIndex = shaderSlot;
            m_pageTableDirty = true;
        }
    }

    Image* GradientManager::GetGradientImage(GraphicsContext* graphicsContext, uint32_t pageIndex)
    {
        if (pageIndex >= m_pages.size()) return nullptr;
        Page& page = m_pages[pageIndex];

        // Re-upload / allocate on first use or if the CPU side changed.
        if (page.modified || page.image == nullptr)
        {
            if (page.image != nullptr && graphicsContext != nullptr)
            {
                graphicsContext->DestroyImageAtEnd(page.image, interstice::DeviceScope::Frame);
                page.image = nullptr;
            }
            if (graphicsContext != nullptr)
            {
                page.image = graphicsContext->CreateImage(
                    page.data.data(),
                    uint2{ page.width, page.height },
                    interstice::ImageFormat::R8G8B8A8,
                    interstice::Sampler::NearestRepeat);
            }
            page.modified = false;
        }
        return page.image;
    }

} // namespace interstice
