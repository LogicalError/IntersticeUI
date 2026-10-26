#pragma once

#include <gui_core/style/gui_style.h>
#include <gui_core/style/gui_gradient_manager.h>

#include <stdint.h>
#include <deque>
#include <vector>
#include <unordered_map>

namespace interstice
{
	// Defined in gui_core/style/gui_style_ref.h.
    struct StyleRef;
    enum class LayoutStyleIndex : uint32_t;
    enum class VisualBoxStyleIndex : uint16_t;
    enum class VisualTextStyleIndex : uint16_t;


    /// A box style's variants, one per visual element state (Style::kStateCount).
    using VisualBoxStyleStates = VisualBoxStyle[Style::kStateCount];
    /// A text style's variants, one per visual element state (Style::kStateCount).
    using VisualTextStyleStates = VisualTextStyle[Style::kStateCount];

    /// Per-element text overrides for StyleManager::WithTextOverride.
    struct TextOverride
    {
        FontID      font = FontID::Invalid; ///< Invalid = keep the base font
        LanguageTag language;               ///< empty = keep the base language
    };

    /// Units of a style's metrics: CSS pixels (scaled to the UI) or final pixels.
    enum class StyleUnits : uint8_t { Css, Pixels };

    /// Bakes serializable `Style`s into deduplicated runtime layout/visual styles;
    /// a StyleRef holds indices into the (possibly shared) pools.
    /// Each VisualColorStyle owns exactly one GradientHandle; Register / Create /
    /// Update / Destroy handle Acquire / Release, so callers never manage them.
    class StyleManager
    {
    public:
        /// Gradients of registered color styles are allocated from `gm`, which must outlive it.
        explicit StyleManager(GradientManager& gm) noexcept;
        ~StyleManager();

        /// Bakes `s` into the pools, reusing identical entries, and returns its indices.
        /// `s` is in CSS px and stored at the UI scale (SetUIScale); StyleUnits::Pixels
        /// registers a style that is already at it (see ToPixels).
        StyleRef Register(const Style &s, StyleUnits units = StyleUnits::Css) noexcept;
        /// A CSS-pixel style at the UI scale, as Register would store it.
        Style     ToPixels(const Style &s) const noexcept;
        /// A CSS-pixel text style at the UI scale.
        TextStyle ToPixels(const TextStyle &t) const noexcept;

        /// UI scale applied to every style as it's registered (see Register). Changing
        /// it affects later registrations only; re-register (theme reload) after.
        void  SetUIScale(float scale) noexcept { m_uiScale = scale > 0.0f ? scale : 1.0f; }
        float GetUIScale() const noexcept { return m_uiScale; } ///< see SetUIScale
        /// A size given in code (CSS px) at the UI scale: whole pixels, and a
        /// non-zero size stays at least 1.
        int32_t Px(int32_t cssPx) const noexcept { return int32_t(Px(float(cssPx))); }
        float   Px(float cssPx) const noexcept; ///< see Px(int32_t)

        /// A variant of `base` whose text uses another font and/or language, for one
        /// element's text (e.g. a text box). Unset fields keep the base style's values;
        /// layout and box styles are shared. Cheap to call every frame; a theme reload
        /// (ClearAll) drops the variants, so derive from the current base each frame.
        StyleRef WithTextOverride(StyleRef base, const TextOverride& textOverride) noexcept;

        /// @name Lookups
        /// Each returns nullptr for an out-of-range index. The per-state getters ignore
        /// non-visual state bits; Disabled overrides every other state.
        ///@{
        const LayoutStyle*           GetLayoutStyle(LayoutStyleIndex index) const noexcept;
        const VisualColorStyle*      GetVisualColorStyle(VisualColorStyleIndex index) const noexcept;
        /// All state variants of a box style.
        const VisualBoxStyleStates*  GetVisualBoxStyles(VisualBoxStyleIndex index) const noexcept;
        const VisualBoxStyle*        GetVisualBoxStyle(VisualBoxStyleIndex index, interstice::ElementState state) const noexcept;
        /// The variant for `state` plus Selection (selected text / ranges).
        const VisualBoxStyle*        GetSelectionVisualBoxStyle(VisualBoxStyleIndex index, interstice::ElementState state = interstice::ElementState::None) const noexcept;
        /// The variant for `state` plus Selection (selected text / ranges).
        const VisualTextStyle*       GetSelectionVisualTextStyle(VisualTextStyleIndex index, interstice::ElementState state = interstice::ElementState::None) const noexcept;
        /// All state variants of a text style.
        const VisualTextStyleStates* GetVisualTextStyles(VisualTextStyleIndex index) const noexcept;
        const VisualTextStyle*       GetVisualTextStyle(VisualTextStyleIndex index, interstice::ElementState state) const noexcept;
        /// The shadows of a box style by its flat GPU index (ComputeGPUStyleIndex); nullptr if none.
        const VisualShadowStyle*     GetVisualShadowStyle(uint32_t gpuStyleIndex) const noexcept;
        ///@}

        /// Number of registered (deduplicated) styles in each pool.
        ///@{
        inline size_t LayoutStyleCount() const noexcept { return m_layoutStyles.size(); }

        inline size_t VisualBoxStyleCount() const noexcept { return m_visualBoxStyles.size() / Style::kStateCount; }

        inline size_t VisualTextStyleCount() const noexcept { return m_visualTextStyles.size() / Style::kStateCount; }

        inline size_t VisualColorStyleCount() const noexcept { return m_visualColorStyles.size(); }
        ///@}


        /// Horizontal padding plus border widths of a layout style, in pixels; 0 for an invalid index.
        inline int32_t GetBorderBoxWidth(LayoutStyleIndex layoutStyleIndex) const noexcept
        {
            const interstice::LayoutStyle* ls = GetLayoutStyle(layoutStyleIndex);
            return ls == nullptr ? 0 : ls->GetBorderBoxWidth();
        }

    public:
        /// Color styles modified since the last upload.
        bool AreVisualColorStylesModified() const noexcept { return visualColorStylesModified; }

        /// Called by the renderer after uploading the color styles.
        void ResetVisualColorStylesModified() noexcept { visualColorStylesModified = false; }


        /// Box styles modified since the last upload.
        bool AreVisualBoxStylesModified() const noexcept { return visualBoxStylesModified; }

        /// Called by the renderer after uploading the box styles.
        void ResetVisualBoxStylesModified() noexcept { visualBoxStylesModified = false; }


        /// The flat box style pool (kStateCount entries per style), for GPU upload.
        std::span<const interstice::VisualBoxStyle> GetVisualBoxStylesSpan() noexcept { return m_visualBoxStyles; }

        /// The color style pool, for GPU upload.
        std::span<const interstice::VisualColorStyle> GetVisualColorStylesSpan() noexcept { return m_visualColorStyles; }

        /// Takes ownership of `incomingHandle`: released on a dedup hit, otherwise stored with the slot.
        VisualColorStyleIndex RegisterVisualColorStyle(const VisualColorStyle& s,
                                                       GradientHandle incomingHandle) noexcept;

        /// Bakes `s` and registers it, reusing an identical entry; the usual way to register
        /// a color. The manager owns the atlas slot.
        VisualColorStyleIndex RegisterColorStyle(const ColorStyle& s,
                                                 ImageIndex textureIndex = ImageIndex::Invalid) noexcept;

        // ---- On-demand (runtime-mutable) color style management ----

        /// A color style of its own that can be changed later (UpdateColorStyle); never
        /// shared with other registrations. Valid until DestroyColorStyle or ClearAll.
        VisualColorStyleIndex CreateColorStyle(const ColorStyle& s) noexcept;

        /// Replaces the CreateColorStyle style at `idx` in place. The GPU copy is
        /// re-uploaded only when the contents changed.
        void UpdateColorStyle(VisualColorStyleIndex idx, const ColorStyle& s) noexcept;

        /// Frees a CreateColorStyle style and its atlas slot; `idx` may be reused later.
        void DestroyColorStyle(VisualColorStyleIndex idx) noexcept;

        /// Releases every atlas slot and clears every style, for a theme reload; every
        /// index and StyleRef handed out before is invalid, so holders re-register (see
        /// Generation). The gradient atlas' GPU images are kept.
        void ClearAll() noexcept;

        /// Bumped by ClearAll. Anything that caches style indices across frames
        /// compares against it and re-registers when it changed.
        uint32_t Generation() const noexcept { return m_generation; }

        /// The renderer resolves logical page indices to physical bindless slots through it.
        GradientManager& GetGradientManager() noexcept { return m_gradientManager; }

    private:
        float m_uiScale = 1.0f;
        /// Deduplication helpers used by Register / RegisterVisualColorStyle.
        VisualColorStyleIndex TryDedupVisualColorStyle(const VisualColorStyle& s,
                                                       uint64_t hash) const noexcept;
        VisualTextStyleIndex RegisterVisualTextStyles(const VisualTextStyle (&states)[Style::kStateCount],
                                                      uint64_t hash) noexcept;
        /// 1-based index of the deduplicated shadow style, 0 when there are no shadows.
        uint16_t RegisterShadows(const BoxShadow (&layers)[kMaxBoxShadows], const BoxShadow& ring, BoxCorners radius) noexcept;

        GradientManager&             m_gradientManager;
        uint32_t                     m_generation = 0;
        std::unordered_map<uint64_t, VisualTextStyleIndex> m_textOverrides; ///< (base text style, override) -> variant

        /// deque: element rects hold LayoutStyle pointers across mid-frame registrations.
        std::deque<LayoutStyle>       m_layoutStyles;
        std::vector<VisualBoxStyle>   m_visualBoxStyles;
        std::vector<VisualTextStyle>  m_visualTextStyles;
        std::vector<VisualColorStyle> m_visualColorStyles;
        std::vector<VisualShadowStyle> m_visualShadowStyles; ///< few: searched linearly at registration

        /// Parallel to m_visualColorStyles; invalid for texture-only entries.
        std::vector<GradientHandle>   m_visualColorStyleHandles;

        bool visualColorStylesModified = false;
        bool visualBoxStylesModified = false;

        /// Style hash -> indices sharing that hash (collisions).
        std::unordered_map<uint64_t, std::vector<LayoutStyleIndex>>      m_layoutHashMap;
        std::unordered_map<uint64_t, std::vector<VisualBoxStyleIndex>>   m_visualBoxHashMap;
        std::unordered_map<uint64_t, std::vector<VisualTextStyleIndex>>  m_visualTextHashMap;
        std::unordered_map<uint64_t, std::vector<VisualColorStyleIndex>> m_visualColorHashMap;

        /// Free list for on-demand (runtime-mutable) color style slots.
        std::vector<VisualColorStyleIndex> m_freeColorStyleSlots;
    };
}
