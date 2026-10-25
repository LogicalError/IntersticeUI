#pragma once

#include <gui_core/style/gui_style.h>
#include <gui_core/style/gui_gradient_manager.h>

#include <stdint.h>
#include <deque>
#include <vector>
#include <unordered_map>

namespace interstice
{
	// requires gui/style/gui_style_ref.h
    struct StyleRef;
    enum class LayoutStyleIndex : uint32_t;
    enum class VisualBoxStyleIndex : uint16_t;
    enum class VisualTextStyleIndex : uint16_t;


    using VisualBoxStyleStates = VisualBoxStyle[Style::kStateCount];
    using VisualTextStyleStates = VisualTextStyle[Style::kStateCount];

    // Per-element text overrides for StyleManager::WithTextOverride.
    struct TextOverride
    {
        FontID      font = FontID::Invalid; // Invalid = keep the base font
        LanguageTag language;               // empty = keep the base language
    };

    // Units of a style's metrics: CSS pixels (scaled to the UI) or final pixels.
    enum class StyleUnits : uint8_t { Css, Pixels };

    // Bakes serializable `Style`s into deduplicated runtime layout/visual styles;
    // a StyleRef holds indices into the (possibly shared) pools.
    // Each VisualColorStyle owns exactly one GradientHandle; Register / Create /
    // Update / Destroy handle Acquire / Release, so callers never manage them.
    class StyleManager
    {
    public:
        explicit StyleManager(GradientManager& gm) noexcept;
        ~StyleManager();

        // Styles are given in CSS pixels and stored at the UI scale (SetUIScale);
        // StyleUnits::Pixels registers a style that is already at it (see ToPixels).
        StyleRef Register(const Style &s, StyleUnits units = StyleUnits::Css) noexcept;
        // A CSS-pixel style at the UI scale, as Register would store it.
        Style     ToPixels(const Style &s) const noexcept;
        TextStyle ToPixels(const TextStyle &t) const noexcept;

        // UI scale applied to every style as it's registered (see Register). Changing
        // it affects later registrations only; re-register (theme reload) after.
        void  SetUIScale(float scale) noexcept { m_uiScale = scale > 0.0f ? scale : 1.0f; }
        float GetUIScale() const noexcept { return m_uiScale; }
        // A size given in code (CSS pixels) at the UI scale: whole pixels, and a
        // non-zero size stays at least 1.
        int32_t Px(int32_t cssPx) const noexcept { return int32_t(Px(float(cssPx))); }
        float   Px(float cssPx) const noexcept;

        // A variant of `base` whose text uses another font and/or language, for one
        // element's text (e.g. a text box). Unset fields keep the base style's values;
        // layout and box styles are shared. Cheap to call every frame; a theme reload
        // (ClearAll) drops the variants, so derive from the current base each frame.
        StyleRef WithTextOverride(StyleRef base, const TextOverride& textOverride) noexcept;

        // Getters by index. Return nullptr if index is out of range.
        const LayoutStyle*           GetLayoutStyle(LayoutStyleIndex index) const noexcept;
        const VisualColorStyle*      GetVisualColorStyle(VisualColorStyleIndex index) const noexcept;
        const VisualBoxStyleStates*  GetVisualBoxStyles(VisualBoxStyleIndex index) const noexcept;
        const VisualBoxStyle*        GetVisualBoxStyle(VisualBoxStyleIndex index, interstice::ElementState state) const noexcept;
        const VisualBoxStyle*        GetSelectionVisualBoxStyle(VisualBoxStyleIndex index, interstice::ElementState state = interstice::ElementState::None) const noexcept;
        const VisualTextStyle*       GetSelectionVisualTextStyle(VisualTextStyleIndex index, interstice::ElementState state = interstice::ElementState::None) const noexcept;
        const VisualTextStyleStates* GetVisualTextStyles(VisualTextStyleIndex index) const noexcept;
        const VisualTextStyle*       GetVisualTextStyle(VisualTextStyleIndex index, interstice::ElementState state) const noexcept;

        inline size_t LayoutStyleCount() const noexcept { return m_layoutStyles.size(); }

        inline size_t VisualBoxStyleCount() const noexcept { return m_visualBoxStyles.size() / Style::kStateCount; }

        inline size_t VisualTextStyleCount() const noexcept { return m_visualTextStyles.size() / Style::kStateCount; }

        inline size_t VisualColorStyleCount() const noexcept { return m_visualColorStyles.size(); }


        inline int32_t GetBorderBoxWidth(LayoutStyleIndex layoutStyleIndex) const noexcept
        {
            const interstice::LayoutStyle* ls = GetLayoutStyle(layoutStyleIndex);
            return ls == nullptr ? 0 : ls->GetBorderBoxWidth();
        }

    public:
        // Modified since the last upload.
        bool AreVisualColorStylesModified() const noexcept { return visualColorStylesModified; }

        void ResetVisualColorStylesModified() noexcept { visualColorStylesModified = false; }


        bool AreVisualBoxStylesModified() const noexcept { return visualBoxStylesModified; }

        void ResetVisualBoxStylesModified() noexcept { visualBoxStylesModified = false; }


        std::span<const interstice::VisualBoxStyle> GetVisualBoxStylesSpan() noexcept { return m_visualBoxStyles; }

        std::span<const interstice::VisualColorStyle> GetVisualColorStylesSpan() noexcept { return m_visualColorStyles; }

        // Takes ownership of `incomingHandle`: released on a dedup hit, otherwise stored with the slot.
        VisualColorStyleIndex RegisterVisualColorStyle(const VisualColorStyle& s,
                                                       GradientHandle incomingHandle) noexcept;

        // Bake a ColorStyle and register it - the preferred wrapper for most
        // call sites. Handles dedup + handle ownership in one call.
        VisualColorStyleIndex RegisterColorStyle(const ColorStyle& s,
                                                 ImageIndex textureIndex = ImageIndex::Invalid) noexcept;

        // ---- On-demand (runtime-mutable) color style management ----

        // Allocates a stable slot for a runtime-owned color style. Bypasses deduplication.
        // The returned index remains valid until DestroyColorStyle is called.
        VisualColorStyleIndex CreateColorStyle(const ColorStyle& s) noexcept;

        // Overwrites the color style at the given index in-place. Releases the
        // previous atlas handle and acquires a new one. Marks GPU buffer dirty
        // only if the contents actually changed.
        void UpdateColorStyle(VisualColorStyleIndex idx, const ColorStyle& s) noexcept;

        // Returns the slot to the free list for future reuse. Releases the
        // underlying atlas handle.
        void DestroyColorStyle(VisualColorStyleIndex idx) noexcept;

        // Releases every handle and clears every style. Intended for theme
        // hot-reload - external holders (e.g. skribidi caret) must re-register
        // afterwards. Does NOT destroy the gradient atlas' GPU images.
        void ClearAll() noexcept;

        // Bumped by ClearAll. Anything that caches style indices across frames
        // compares against it and re-registers when it changed.
        uint32_t Generation() const noexcept { return m_generation; }

        // The renderer resolves logical page indices to physical bindless slots through it.
        GradientManager& GetGradientManager() noexcept { return m_gradientManager; }

    private:
        float m_uiScale = 1.0f;
        // Deduplication helpers used by Register / RegisterVisualColorStyle.
        VisualColorStyleIndex TryDedupVisualColorStyle(const VisualColorStyle& s,
                                                       uint64_t hash) const noexcept;
        VisualTextStyleIndex RegisterVisualTextStyles(const VisualTextStyle (&states)[Style::kStateCount],
                                                      uint64_t hash) noexcept;

        GradientManager&             m_gradientManager;
        uint32_t                     m_generation = 0;
        std::unordered_map<uint64_t, VisualTextStyleIndex> m_textOverrides; // (base text style, override) -> variant

        // deque: element rects hold LayoutStyle pointers across mid-frame registrations.
        std::deque<LayoutStyle>       m_layoutStyles;
        std::vector<VisualBoxStyle>   m_visualBoxStyles;
        std::vector<VisualTextStyle>  m_visualTextStyles;
        std::vector<VisualColorStyle> m_visualColorStyles;

        // Parallel to m_visualColorStyles; invalid for texture-only entries.
        std::vector<GradientHandle>   m_visualColorStyleHandles;

        bool visualColorStylesModified = false;
        bool visualBoxStylesModified = false;

        // Style hash -> indices sharing that hash (collisions).
        std::unordered_map<uint64_t, std::vector<LayoutStyleIndex>>      m_layoutHashMap;
        std::unordered_map<uint64_t, std::vector<VisualBoxStyleIndex>>   m_visualBoxHashMap;
        std::unordered_map<uint64_t, std::vector<VisualTextStyleIndex>>  m_visualTextHashMap;
        std::unordered_map<uint64_t, std::vector<VisualColorStyleIndex>> m_visualColorHashMap;

        // Free list for on-demand (runtime-mutable) color style slots.
        std::vector<VisualColorStyleIndex> m_freeColorStyleSlots;
    };
}
