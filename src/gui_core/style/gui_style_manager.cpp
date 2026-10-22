#include <gui_core/gui_base_types.h>
#include <gui_core/elements/gui_element_storage.h>

#include <gui_core/style/gui_style_manager.h>

#include <gui_core/graphics/gui_buffer.h>
#include <gui_core/graphics/gui_image.h>

#include <algorithm>
#include <cmath>

namespace GUI
{
    StyleManager::StyleManager(GradientManager& gradientManager) noexcept
        : m_gradientManager(gradientManager)
        , visualBoxStylesModified(true)
    {
    }

    StyleManager::~StyleManager()
    {
        // Release still-live atlas handles so the atlas's refcount asserts hold across restarts.
        for (auto h : m_visualColorStyleHandles)
        {
            if (h.IsValid())
                m_gradientManager.Release(h);
        }
    }

    // Text metrics at the UI scale: sizes and spacing exactly, decorations to whole
    // pixels (a set thickness stays at least 1px).
    static TextStyle ScaledTextStyle(const TextStyle& in, float s) noexcept
    {
        TextStyle t = in;
        auto px = [s](auto v) { return decltype(v)(std::floor(float(v) * s + 0.5f)); };
        t.fontSize      *= s;
        t.letterSpacing *= s;
        t.wordSpacing   *= s;
        if (t.lineHeightType == LineHeight::Absolute)
            t.lineHeight *= s;
        for (TextDecoration* d : { &t.throughLine, &t.bottomLine, &t.underLine, &t.overLine })
        {
            if (d->thickness != 0)
                d->thickness = decltype(d->thickness)(std::max(1, int(px(d->thickness))));
            d->offset = px(d->offset);
        }
        return t;
    }

    // Style metrics at the UI scale: fonts exactly, everything else to whole pixels
    // (round half up). Borders that are set stay at least 1px; "auto" and
    // unbounded sentinels are kept.
    static Style ScaledStyle(const Style& in, float s) noexcept
    {
        Style out = in;
        auto px = [s](auto v) { return decltype(v)(std::floor(float(v) * s + 0.5f)); };
        auto border = [&](uint16_t v) { return v == 0 ? uint16_t(0) : uint16_t(std::max(1, int(px(v)))); };
        auto edges = [&](BoxEdges e) { return BoxEdges{ px(e.top), px(e.right), px(e.bottom), px(e.left) }; };
        auto size = [&](int32_t v) {
            return (v == LayoutData::kAutoSize || v == LayoutData::kUnboundedMax) ? v : px(v);
        };
        auto track = [&](GridTrack t) {
            if (t.type == GridTrack::Type::Fixed) t.value = std::floor(t.value * s + 0.5f);
            t.minPx = std::floor(t.minPx * s + 0.5f);
            return t;
        };

        out.margin  = edges(in.margin);
        out.padding = edges(in.padding);
        out.borderWidths = BoxEdges{ border(in.borderWidths.top), border(in.borderWidths.right),
                                     border(in.borderWidths.bottom), border(in.borderWidths.left) };
        out.borderRadius = BoxCorners{ px(in.borderRadius.topLeft), px(in.borderRadius.topRight),
                                       px(in.borderRadius.bottomRight), px(in.borderRadius.bottomLeft) };
        out.borderDashSize   = in.borderDashSize * s;
        out.borderDashOffset = in.borderDashOffset * s;
        out.iconTextGap = px(in.iconTextGap);
        out.iconSize    = px(in.iconSize);
        out.iconOffsetY = px(in.iconOffsetY);

        LayoutData& l = out.layoutData;
        l.width     = size(in.layoutData.width);
        l.height    = size(in.layoutData.height);
        l.minWidth  = size(in.layoutData.minWidth);
        l.maxWidth  = size(in.layoutData.maxWidth);
        l.minHeight = size(in.layoutData.minHeight);
        l.maxHeight = size(in.layoutData.maxHeight);
        l.flex.gap  = px(in.layoutData.flex.gap);
        if (in.layoutData.flex.flexBasis != FlexProperties::kAuto)
            l.flex.flexBasis = px(in.layoutData.flex.flexBasis);
        l.grid.column.gap   = px(in.layoutData.grid.column.gap);
        l.grid.row.gap      = px(in.layoutData.grid.row.gap);
        l.grid.column.track = track(in.layoutData.grid.column.track);
        l.grid.row.track    = track(in.layoutData.grid.row.track);

        for (StyleState& state : out.states)
            state.textStyle = ScaledTextStyle(state.textStyle, s);
        return out;
    }

    float StyleManager::Px(float cssPx) const noexcept
    {
        if (cssPx == 0.0f)
            return 0.0f;
        const float px = std::max(1.0f, std::floor(std::abs(cssPx) * m_uiScale + 0.5f));
        return cssPx < 0.0f ? -px : px;
    }

    Style StyleManager::ToPixels(const Style &s) const noexcept
    {
        return m_uiScale == 1.0f ? s : ScaledStyle(s, m_uiScale);
    }

    TextStyle StyleManager::ToPixels(const TextStyle &t) const noexcept
    {
        return m_uiScale == 1.0f ? t : ScaledTextStyle(t, m_uiScale);
    }

    StyleRef StyleManager::Register(const Style &given, StyleUnits units) noexcept
    {
        const Style style = units == StyleUnits::Pixels ? given : ToPixels(given);

        LayoutStyle layoutStyle
        {
            .margin          = style.margin,
            .padding         = style.padding,
            .borderWidths    = style.borderWidths,

            .layoutData      = style.layoutData,

            .iconTextGap     = style.iconTextGap,
            .iconSize        = style.iconSize,
            .iconOffsetY     = style.iconOffsetY,

            .display         = style.display,
            .visibility      = style.visibility,
            .overflowX       = style.overflowX,
            .overflowY       = style.overflowY,
            .iconSide        = style.iconSide,
            .alignSelf       = style.alignSelf,
            .breakBehavior   = style.breakBehavior,
            .clampScroll     = style.clampScroll,
            .cursor          = style.cursor
        };

        VisualBoxStyle visualBoxStyle[Style::kStateCount];
        VisualTextStyle visualTextStyle[Style::kStateCount];

        uint64_t visualBoxHash = 0;
        uint64_t visualTextHash = 0;
        for (int i = 0; i < Style::kStateCount; i++)
        {
            auto& styleState = style.states[i];

            // If no explicit textColor is set, derive it from textStyle.color as a solid color fallback.
            GradientHandle textColorHandle;
            VisualColorStyle textColor;
            if (styleState.textColor.gradient.stopCount == 0)
            {
                ColorStyle fallbackTextColor;
                fallbackTextColor.type = ColorType::SolidColor;
                auto& c = styleState.textStyle.color;
                fallbackTextColor.gradient.stops[0] = ColorStop{}.SetColor(c);
                fallbackTextColor.gradient.stopCount = 1;
                textColor = fallbackTextColor.ToVisualStyle(m_gradientManager, style.textureIndex, textColorHandle);
            }
            else
            {
                textColor = styleState.textColor.ToVisualStyle(m_gradientManager, style.textureIndex, textColorHandle);
            }
            GradientHandle backgroundHandle;
            VisualColorStyle backgroundColor = styleState.backgroundColor.ToVisualStyle(m_gradientManager, style.textureIndex, backgroundHandle);

            VisualColorStyleIndex textColorIndex       = RegisterVisualColorStyle(textColor, textColorHandle);
            VisualColorStyleIndex backgroundColorIndex = RegisterVisualColorStyle(backgroundColor, backgroundHandle);

            VisualColorStyleIndex borderColorIndices[4];
            for (int e = 0; e < 4; e++)
            {
                GradientHandle borderHandle;
                VisualColorStyle borderColor = styleState.borderColors[e].ToVisualStyle(m_gradientManager, style.textureIndex, borderHandle);
                borderColorIndices[e] = RegisterVisualColorStyle(borderColor, borderHandle);
            }

            visualBoxStyle[i] = VisualBoxStyle
            {
                .borderWidths         = ((i & Style::kSelection) != 0) ? BoxEdges{}   : style.borderWidths,
                .borderRadius         = ((i & Style::kSelection) != 0) ? BoxCorners{} : style.borderRadius,

                .borderColorIndex     = { borderColorIndices[0], borderColorIndices[1], borderColorIndices[2], borderColorIndices[3] },
                .backgroundColorIndex = backgroundColorIndex,
                .borderDashSize       = (uint16_t)std::clamp((int)style.borderDashSize,   0, 65535),
                .borderDashOffset     = (uint16_t)std::clamp((int)style.borderDashOffset, 0, 65535),
            };
            visualBoxHash = Hash::Combine(visualBoxHash, visualBoxStyle[i].GetHashCode());

            visualTextStyle[i] = VisualTextStyle
            {
                .textStyle         = styleState.textStyle,
                .textLayoutStyle   = style.textLayoutStyle,
				.textColorIndex    = textColorIndex,
                .iconTextGap       = style.iconTextGap,
                .iconSize          = style.iconSize,
                .iconOffsetY       = style.iconOffsetY,
                .iconSide          = style.iconSide
            };
            visualTextHash = Hash::Combine(visualTextHash, visualTextStyle[i].GetHashCode());
        }


        // Deduplicate layout styles using hash map.
        LayoutStyleIndex layoutIndex = LayoutStyleIndex::Invalid;
        {
            const uint64_t layoutHash = layoutStyle.GetHashCode();

            auto layoutIterator = m_layoutHashMap.find(layoutHash);
            if (layoutIterator != m_layoutHashMap.end())
            {
                // Check collision bucket for an exact match.
                for (auto index : layoutIterator->second)
                {
                    const auto& existing = m_layoutStyles[static_cast<size_t>(index) - 1];
                    if (existing == layoutStyle)
                    {
                        layoutIndex = index;
                        break;
                    }
                }
            }

            if (layoutIndex == LayoutStyleIndex::Invalid)
            {
                m_layoutStyles.push_back(layoutStyle);
                int32_t idx = static_cast<int32_t>(m_layoutStyles.size()) - 1;
                layoutIndex = static_cast<LayoutStyleIndex>(idx + 1);
                m_layoutHashMap[layoutHash].push_back(layoutIndex);
            }
        }



        // Deduplicate visual box styles using hash map.
        VisualBoxStyleIndex visualBoxIndex = VisualBoxStyleIndex::Invalid;
        {
            auto visualBoxIterator = m_visualBoxHashMap.find(visualBoxHash);
            if (visualBoxIterator != m_visualBoxHashMap.end())
            {
                for (auto index : visualBoxIterator->second)
                {
                    bool match = true;
                    size_t offset = (static_cast<size_t>(index) - 1) * Style::kStateCount;
                    for (int i = 0; i < Style::kStateCount; i++)
                    {
                        const auto& existing = m_visualBoxStyles[offset + i];
                        if (existing != visualBoxStyle[i])
                        {
                            match = false;
                            break;
                        }
                    }
                    if (match)
                    {
                        visualBoxIndex = index;
                        break;
                    }
                }
            }

            if (visualBoxIndex == VisualBoxStyleIndex::Invalid)
            {
                // First index of state styles for this layout style
                int32_t idx = static_cast<int32_t>(m_visualBoxStyles.size()) / Style::kStateCount;
                for (int i = 0; i < Style::kStateCount; i++)
                {
                    m_visualBoxStyles.push_back(visualBoxStyle[i]);
                }
                visualBoxStylesModified = true;
                visualBoxIndex = static_cast<VisualBoxStyleIndex>(idx + 1);
                m_visualBoxHashMap[visualBoxHash].push_back(visualBoxIndex);
            }
        }



        const VisualTextStyleIndex visualTextIndex = RegisterVisualTextStyles(visualTextStyle, visualTextHash);
        return StyleRef{ layoutIndex, visualBoxIndex, visualTextIndex };
    }

    // Deduplicate visual text styles using hash map.
    VisualTextStyleIndex StyleManager::RegisterVisualTextStyles(const VisualTextStyle (&visualTextStyle)[Style::kStateCount],
                                                               uint64_t visualTextHash) noexcept
    {
        auto visualTextIterator = m_visualTextHashMap.find(visualTextHash);
        if (visualTextIterator != m_visualTextHashMap.end())
        {
            for (auto index : visualTextIterator->second)
            {
                bool match = true;
                size_t offset = (static_cast<size_t>(index) - 1) * Style::kStateCount;
                for (int i = 0; i < Style::kStateCount; i++)
                {
                    const auto& existing = m_visualTextStyles[offset + i];
                    if (existing != visualTextStyle[i])
                    {
                        match = false;
                        break;
                    }
                }
                if (match)
                    return index;
            }
        }

        // First index of state styles for this layout style
        int32_t idx = static_cast<int32_t>(m_visualTextStyles.size()) / Style::kStateCount;
        for (int i = 0; i < Style::kStateCount; i++)
            m_visualTextStyles.push_back(visualTextStyle[i]);
        const auto visualTextIndex = static_cast<VisualTextStyleIndex>(idx + 1);
        m_visualTextHashMap[visualTextHash].push_back(visualTextIndex);
        return visualTextIndex;
    }

    StyleRef StyleManager::WithTextOverride(StyleRef base, const TextOverride& textOverride) noexcept
    {
        if (textOverride.font == FontID::Invalid && textOverride.language.empty())
            return base;
        const VisualTextStyleStates* baseText = GetVisualTextStyles(base.visualTextStyleIndex);
        if (!baseText)
            return base;

        const uint64_t key = Hash::Combine(Hash::Calculate(base.visualTextStyleIndex), Hash::Calculate(textOverride.font),
                                           Hash::Calculate(textOverride.language.view()));
        if (auto it = m_textOverrides.find(key); it != m_textOverrides.end())
            return StyleRef{ base.layoutStyleIndex, base.visualBoxStyleIndex, it->second };

        VisualTextStyle states[Style::kStateCount];
        uint64_t hash = 0;
        for (int i = 0; i < Style::kStateCount; i++)
        {
            states[i] = (*baseText)[i];
            if (textOverride.font != FontID::Invalid)
                states[i].textStyle.fontId = textOverride.font;
            if (!textOverride.language.empty())
                states[i].textStyle.language = textOverride.language;
            hash = Hash::Combine(hash, states[i].GetHashCode());
        }
        const VisualTextStyleIndex index = RegisterVisualTextStyles(states, hash);
        m_textOverrides[key] = index;
        return StyleRef{ base.layoutStyleIndex, base.visualBoxStyleIndex, index };
    }

    VisualColorStyleIndex StyleManager::TryDedupVisualColorStyle(const VisualColorStyle& visualColorStyle,
                                                                 uint64_t hash) const noexcept
    {
        auto visualColorIterator = m_visualColorHashMap.find(hash);
        if (visualColorIterator != m_visualColorHashMap.end())
        {
            for (auto index : visualColorIterator->second)
            {
                if (m_visualColorStyles[(static_cast<size_t>(index) - 1)] == visualColorStyle)
                    return index;
            }
        }
        return VisualColorStyleIndex::Invalid;
    }

    VisualColorStyleIndex StyleManager::RegisterVisualColorStyle(const VisualColorStyle& visualColorStyle,
                                                                 GradientHandle incomingHandle) noexcept
    {
        auto visualColorHash = visualColorStyle.GetHashCode();

        VisualColorStyleIndex dedupIdx = TryDedupVisualColorStyle(visualColorStyle, visualColorHash);
        if (dedupIdx != VisualColorStyleIndex::Invalid)
        {
            // Dedup hit: we already have a slot whose handle refs the same
            // atlas content. The incoming acquire is now redundant; release it.
            if (incomingHandle.IsValid())
                m_gradientManager.Release(incomingHandle);
            return dedupIdx;
        }

        int32_t idx = static_cast<int32_t>(m_visualColorStyles.size());
        m_visualColorStyles.push_back(visualColorStyle);
        m_visualColorStyleHandles.push_back(incomingHandle);
        visualColorStylesModified = true;
        VisualColorStyleIndex visualColorIndex = static_cast<VisualColorStyleIndex>(idx + 1);
        m_visualColorHashMap[visualColorHash].push_back(visualColorIndex);
        return visualColorIndex;
    }

    VisualColorStyleIndex StyleManager::RegisterColorStyle(const ColorStyle& s, ImageIndex textureIndex) noexcept
    {
        GradientHandle handle;
        VisualColorStyle visual = s.ToVisualStyle(m_gradientManager, textureIndex, handle);
        return RegisterVisualColorStyle(visual, handle);
    }

    VisualColorStyleIndex StyleManager::CreateColorStyle(const ColorStyle& s) noexcept
    {
        GradientHandle handle;
        VisualColorStyle visual = s.ToVisualStyle(m_gradientManager, ImageIndex::Invalid, handle);

        if (!m_freeColorStyleSlots.empty())
        {
            VisualColorStyleIndex idx = m_freeColorStyleSlots.back();
            m_freeColorStyleSlots.pop_back();

            size_t i = static_cast<size_t>(idx) - 1;
            // Release any stale handle lingering in this slot (should be invalid,
            // but be defensive in case the free-list was populated differently).
            if (m_visualColorStyleHandles[i].IsValid())
                m_gradientManager.Release(m_visualColorStyleHandles[i]);

            m_visualColorStyles[i] = visual;
            m_visualColorStyleHandles[i] = handle;
            visualColorStylesModified = true;
            return idx;
        }

        int32_t rawIdx = static_cast<int32_t>(m_visualColorStyles.size());
        m_visualColorStyles.push_back(visual);
        m_visualColorStyleHandles.push_back(handle);
        visualColorStylesModified = true;
        return static_cast<VisualColorStyleIndex>(rawIdx + 1);
    }

    void StyleManager::UpdateColorStyle(VisualColorStyleIndex idx, const ColorStyle& s) noexcept
    {
        if (idx == VisualColorStyleIndex::Invalid) return;
        size_t i = static_cast<size_t>(idx) - 1;
        if (i >= m_visualColorStyles.size()) return;

        GradientHandle newHandle;
        auto newVisualStyle = s.ToVisualStyle(m_gradientManager, ImageIndex::Invalid, newHandle);

        if (m_visualColorStyles[i] == newVisualStyle)
        {
            // No visual change: release the new acquire to keep the refcount balanced.
            if (newHandle.IsValid())
                m_gradientManager.Release(newHandle);
            return;
        }

        // Release old, adopt new.
        GradientHandle& slotHandle = m_visualColorStyleHandles[i];
        if (slotHandle.IsValid())
            m_gradientManager.Release(slotHandle);

        m_visualColorStyles[i] = std::move(newVisualStyle);
        slotHandle = newHandle;
        visualColorStylesModified = true;
    }

    void StyleManager::DestroyColorStyle(VisualColorStyleIndex idx) noexcept
    {
        if (idx == VisualColorStyleIndex::Invalid) return;
        size_t i = static_cast<size_t>(idx) - 1;
        if (i >= m_visualColorStyles.size()) return;

        GradientHandle& slotHandle = m_visualColorStyleHandles[i];
        if (slotHandle.IsValid())
        {
            m_gradientManager.Release(slotHandle);
            slotHandle = GradientHandle::Invalid();
        }

        m_visualColorStyles[i] = VisualColorStyle{}; // zero out
        visualColorStylesModified = true;
        m_freeColorStyleSlots.push_back(idx);
    }

    void StyleManager::ClearAll() noexcept
    {
        for (auto& h : m_visualColorStyleHandles)
        {
            if (h.IsValid())
                m_gradientManager.Release(h);
        }
        m_visualColorStyleHandles.clear();
        m_visualColorStyles.clear();
        m_visualColorHashMap.clear();
        m_freeColorStyleSlots.clear();

        m_layoutStyles.clear();
        m_layoutHashMap.clear();

        m_visualBoxStyles.clear();
        m_visualBoxHashMap.clear();

        m_visualTextStyles.clear();
        m_visualTextHashMap.clear();
        m_textOverrides.clear();

        visualColorStylesModified = true;
        visualBoxStylesModified = true;
        ++m_generation;
    }

    const LayoutStyle* StyleManager::GetLayoutStyle(LayoutStyleIndex index) const noexcept
    {
        if (index == LayoutStyleIndex::Invalid) return nullptr;
        size_t idx = static_cast<size_t>(index) - 1;
        if (idx >= m_layoutStyles.size()) return nullptr;
        return &m_layoutStyles[idx];
    }

    const VisualColorStyle* StyleManager::GetVisualColorStyle(VisualColorStyleIndex index) const noexcept
    {
        if (index == VisualColorStyleIndex::Invalid) return nullptr;
        size_t idx = static_cast<size_t>(index) - 1;
        if (idx >= m_visualColorStyles.size()) return nullptr;
        return &m_visualColorStyles[idx];
    }

    const VisualBoxStyleStates* StyleManager::GetVisualBoxStyles(VisualBoxStyleIndex index) const noexcept
    {
        if (index == VisualBoxStyleIndex::Invalid) return nullptr;
        size_t idx = static_cast<size_t>(index) - 1;
        if (idx * Style::kStateCount >= m_visualBoxStyles.size()) return nullptr;
        return (VisualBoxStyleStates*)&m_visualBoxStyles[idx * Style::kStateCount];
    }

    const VisualBoxStyle* StyleManager::GetVisualBoxStyle(VisualBoxStyleIndex index, GUI::ElementState state) const noexcept
    {
        if (index == VisualBoxStyleIndex::Invalid) return nullptr;
        size_t idx = static_cast<size_t>(index) - 1;
        if ((idx + 1) * Style::kStateCount > m_visualBoxStyles.size()) return nullptr;
        state &= ElementState::VisualMask;
        if ((state & ElementState::Disabled) != ElementState::None) state = ElementState::Disabled;
        return &m_visualBoxStyles[(idx * Style::kStateCount) + (int)state];
    }

    const VisualBoxStyle* StyleManager::GetSelectionVisualBoxStyle(VisualBoxStyleIndex index, GUI::ElementState state) const noexcept
    {
        return GetVisualBoxStyle(index, state | ElementState::Selection);
    }

    const VisualTextStyle* StyleManager::GetSelectionVisualTextStyle(VisualTextStyleIndex index, GUI::ElementState state) const noexcept
    {
        return GetVisualTextStyle(index, state | ElementState::Selection);
    }

    const VisualTextStyleStates* StyleManager::GetVisualTextStyles(VisualTextStyleIndex index) const noexcept
    {
        if (index == VisualTextStyleIndex::Invalid) return nullptr;
        size_t idx = static_cast<size_t>(index) - 1;
        if (idx * Style::kStateCount >= m_visualTextStyles.size()) return nullptr;
        return (VisualTextStyleStates*)& m_visualTextStyles[idx * Style::kStateCount];
    }

    const VisualTextStyle* StyleManager::GetVisualTextStyle(VisualTextStyleIndex index, GUI::ElementState state) const noexcept
    {
        if (index == VisualTextStyleIndex::Invalid) return nullptr;
        size_t idx = static_cast<size_t>(index) - 1;
        if ((idx + 1) * Style::kStateCount > m_visualTextStyles.size()) return nullptr;
        state &= ElementState::VisualMask;
        if ((state & ElementState::Disabled) != ElementState::None) state = ElementState::Disabled;
        return &m_visualTextStyles[(idx * Style::kStateCount) + (int)state];
    }
}
