
#include <gui_core/state/gui_state_manager.h>

#include <gui_core/style/gui_style_manager.h>

#include <gui_core/diagnostics/gui_logging.h>
#include <gui_core/diagnostics/gui_profiler.h>

#include <gui_core/text/gui_icon_text_placement.h>
#include <gui_core/text/gui_text_atlas_manager.h>

#include <gui_core/text/skribidi/gui_text_system_skribidi.h>
#include <gui_core/text/skribidi/internal/gui_skribidi_context.h>
#include <gui_core/text/skribidi/internal/nanosvg_bridge.h>

#include <gui_core/graphics/gui_graphics_context.h>
#include <gui_core/graphics/gui_renderer.h>

#include <fstream>
#include <utility>



#undef SKB_CURRENT_SELECTION
#define SKB_CURRENT_SELECTION \
	skb_text_range_t { .start = {.offset = INT32_MIN, .affinity = SKB_AFFINITY_NONE}, .end = {.offset = INT32_MIN, .affinity = SKB_AFFINITY_NONE} }

#undef SKB_CURRENT_SELECTION_END
#define SKB_CURRENT_SELECTION_END \
	skb_text_position_t { .offset = INT32_MIN, .affinity = SKB_AFFINITY_NONE }

namespace tachyon
{
	namespace
	{
		constexpr Diagnostics::LogCategory kLogText = Diagnostics::MakeLogCategory("text");
	}

	SkribidiTextSystem::SkribidiTextSystem()
		: m_skribidiContext(nullptr)
	{
	}

	SkribidiTextSystem::~SkribidiTextSystem()
	{
		Shutdown();
	}

	// -------------------- Initialization --------------------

	bool SkribidiTextSystem::Initialize(GraphicsContext* graphicsContext, StyleManager* styleManager,
	                                    TextAtlasManager* textAtlasManager,
	                                    GUIStateManager* guiStateManager, IClipboard* clipboard)
	{
		m_skribidiContext = new SkribidiContext();
		if (m_skribidiContext == nullptr)
			return false;
		m_skribidiContext->m_fontFallback.defaultDirectory  = m_fontDirectory;
		m_skribidiContext->m_fontFallback.fallbackDirectory = m_fallbackFontDirectory;

		return m_skribidiContext->Initialize(graphicsContext, styleManager, textAtlasManager, guiStateManager, clipboard);
	}

	void SkribidiTextSystem::Shutdown()
	{
		if (m_skribidiContext != nullptr)
		{
			m_skribidiContext->Shutdown();
			delete m_skribidiContext;
			m_skribidiContext = nullptr;
		}

		m_elementIdToTextIdMap.clear();
		m_textKeyToIdMap.clear();
		m_textEditorElements.clear();
		m_readOnlyEditorElements.clear();
		m_textEditorValidators.clear();
		m_valueEditElementId = ID::Invalid();
		m_valueEditText.clear();
	}

	void SkribidiTextSystem::UpdateGlyphs()
	{
		if (skb_image_atlas_rasterize_missing_items(m_skribidiContext->atlas, m_skribidiContext->temp_alloc, m_skribidiContext->rasterizer))
		{
			m_skribidiContext->atlas_dirty = true;
		}
    }

	void SkribidiTextSystem::BeginAtlasRecord(ID scopeId)
	{
		if (m_skribidiContext != nullptr) m_skribidiContext->BeginAtlasRecord(scopeId);
	}

	void SkribidiTextSystem::EndAtlasRecord()
	{
		if (m_skribidiContext != nullptr) m_skribidiContext->EndAtlasRecord();
	}

	bool SkribidiTextSystem::ReplayAtlasTouches(ID scopeId)
	{
		return m_skribidiContext == nullptr || m_skribidiContext->ReplayAtlasTouches(scopeId);
	}

	void SkribidiTextSystem::BeginFrame(int2 screenMousePos, MouseButtons mouseButtons, uint32_t timeMs)
	{
		m_textEditorElements.clear();
		m_readOnlyEditorElements.clear();
		m_textEditorValidators.clear();

		if (m_skribidiContext != nullptr)
		{
			m_skribidiContext->frameMousePos     = screenMousePos;
			m_skribidiContext->frameMouseButtons = mouseButtons;
			m_skribidiContext->frameTimeMs       = timeMs;
			m_skribidiContext->frameNumber++;
			EvictUnusedTexts();
		}
	}

	void SkribidiTextSystem::EvictUnusedTexts()
	{
		// Texts not registered for this many frames are dropped. Generous, so a
		// panel that is briefly not rebuilt (retained cache) doesn't reshape on wake.
		constexpr uint32_t kEvictAfterFrames = 120;
		auto* ctx = m_skribidiContext;

		static thread_local std::vector<TextID> evicted;
		evicted.clear();
		for (size_t i = 0; i < ctx->m_registry.size(); ++i)
		{
			RegisteredText& entry = ctx->m_registry[i];
			const TextID id = entry.textId;
			if (id == TextID::Invalid || ctx->frameNumber - entry.lastUsedFrame <= kEvictAfterFrames)
				continue;
			// Never pull a text out from under the editor.
			if (id == ctx->active_text_id || id == ctx->editing_text_id || id == ctx->pending_text_id ||
			    id == ctx->pending_text_prev_id || id == ctx->pending_select_all_id)
				continue;
			ctx->ResetLayout(&entry);
			entry = RegisteredText{};
			ctx->m_freeTextSlots.push_back(uint32_t(i));
			evicted.push_back(id);
		}
		if (evicted.empty())
			return;

		auto isEvicted = [](TextID id) { return std::find(evicted.begin(), evicted.end(), id) != evicted.end(); };
		std::erase_if(m_textKeyToIdMap,       [&](const auto& kv) { return isEvicted(kv.second); });
		std::erase_if(m_elementIdToTextIdMap, [&](const auto& kv) { return isEvicted(kv.second); });
	}

	void SkribidiTextSystem::Update()
	{
		if (m_skribidiContext != nullptr)
		{
			if (!m_skribidiContext->atlas || !m_skribidiContext->temp_alloc ||
				!m_skribidiContext->rasterizer || !m_skribidiContext->textAtlasManager)
				return;

			UpdateGlyphs();

			if (!m_skribidiContext->atlas_dirty)
				return;
				
			m_skribidiContext->atlas_dirty = false;
			for (int32_t i = 0; i < skb_image_atlas_get_texture_count(m_skribidiContext->atlas); i++)
			{
				skb_rect2i_t dirty_bounds = skb_image_atlas_get_and_reset_texture_dirty_bounds(m_skribidiContext->atlas, i);
				if (!skb_rect2i_is_empty(dirty_bounds))
				{
					const skb_image_t* image = skb_image_atlas_get_texture(m_skribidiContext->atlas, i);
					assert(image);
					auto textAtlasManager = m_skribidiContext->textAtlasManager;
					auto textureIndex = (tachyon::TextureIndex)skb_image_atlas_get_texture_user_data(m_skribidiContext->atlas, i);
					if (textureIndex == tachyon::TextureIndex::Invalid)
					{
						textureIndex = textAtlasManager->CreateTexture(image->buffer, uint2 { uint32_t(image->width), uint32_t(image->height) }, image->bpp);
						assert(textureIndex != tachyon::TextureIndex::Invalid);
						skb_image_atlas_set_texture_user_data(m_skribidiContext->atlas, i, (uint32_t)textureIndex);
					} else
					{
						textAtlasManager->
							UpdateTexture(textureIndex, image->buffer, 
								uint2 { uint32_t(image->width), uint32_t(image->height) });
					}
				}
			}

			skb_image_atlas_compact(m_skribidiContext->atlas);

			m_skribidiContext->textAtlasManager->SyncTextureIndices();
		}
	}



	// -------------------- Icon Registration --------------------

	bool SkribidiTextSystem::IsValidIcon(IconID iconId) const
	{
		return iconId != IconID::Invalid;
	}

	IconID SkribidiTextSystem::RegisterIconFromSVG(const char* name,
												   const char* svg_data,
												   size_t svg_size)
	{
		if (!m_skribidiContext || !m_skribidiContext->icon_collection)
			return IconID::Invalid;

		// Names are unique in the collection: a repeat registration (e.g. a theme
		// reload re-creating its icons) gets the icon it registered before. Another
		// shape under a taken name is a clash: warned, and the first icon kept.
		const size_t source = std::hash<std::string_view>{}(std::string_view(svg_data, svg_size));
		if (const skb_icon_handle_t existing = skb_icon_collection_find_icon(m_skribidiContext->icon_collection, name))
		{
			const auto& handles = m_skribidiContext->iconHandles;
			for (size_t i = 0; i < handles.size(); i++)
				if (handles[i] == existing)
				{
					if (m_skribidiContext->iconSources[i] != source)
						TACHYON_LOG_WARNING(Diagnostics::MakeLogCategory("text"),
							"an icon named \"{}\" is already registered with another shape; the first is kept", name);
					return IconID{ (uint32_t)(i + 1) };
				}
		}

		skb_icon_handle_t handle = RegisterNanoSVG(
			m_skribidiContext->icon_collection,
			name,
			svg_data,
			svg_size);

		if (handle == 0)
			return IconID::Invalid;

		m_skribidiContext->iconHandles.push_back(handle);
		m_skribidiContext->iconSources.push_back(source);
		return IconID { (uint32_t)m_skribidiContext->iconHandles.size() };
	}

	IconID SkribidiTextSystem::RegisterIconFromSVGFile(const char* path)
	{
		if (!path)
			return IconID::Invalid;

		std::ifstream file(path, std::ios::binary | std::ios::ate);
		if (!file)
			return IconID::Invalid;

		const std::streamsize size = file.tellg();
		if (size <= 0)
			return IconID::Invalid;
		file.seekg(0, std::ios::beg);

		std::string buffer(static_cast<size_t>(size), '\0');
		if (!file.read(buffer.data(), size))
			return IconID::Invalid;

		return RegisterIconFromSVG(path, buffer.data(), buffer.size());
	}

	// -------------------- Icon Measurement --------------------

	IconMeasurement SkribidiTextSystem::GetIconMeasurement(IconID iconId) const
	{
		return MeasureIcon(iconId);
	}

	IconMeasurement SkribidiTextSystem::MeasureIcon(IconID iconId) const
	{
		if (!m_skribidiContext || !m_skribidiContext->icon_collection)
			return IconMeasurement {};
		
		if (iconId == IconID::Invalid)
			return IconMeasurement {};
		
		const skb_icon_handle_t handle = m_skribidiContext->GetIconHandle(iconId);
		if (handle == 0)
			return IconMeasurement {};
		skb_vec2_t size = skb_icon_collection_get_icon_size(m_skribidiContext->icon_collection, handle);
		
		return IconMeasurement {
			.width = static_cast<int32_t>(std::ceil(size.x)),
			.height = static_cast<int32_t>(std::ceil(size.y))
		};
	}




	void SkribidiTextSystem::SetCaretColor(VisualColorStyleIndex color)
	{
		if (m_skribidiContext && color != VisualColorStyleIndex::Invalid)
			m_skribidiContext->caretColorStyleIndex = color;
	}

	void SkribidiTextSystem::SetTextColor(TextID textId, VisualColorStyleIndex color)
	{
		if (!m_skribidiContext) return;
		if (auto* entry = m_skribidiContext->GetRegisteredText(textId))
			entry->textColorIndex = color;
	}

	void SkribidiTextSystem::UpdateRegisteredTextStyleState(RegisteredText* entry, tachyon::StyleRef styleIndex, tachyon::ElementState state)
	{
		assert(entry != nullptr);

		// Colors follow every state; Selected only recolors, so the layout ignores it.
		auto* styleManager = m_skribidiContext->styleManager;
		if (const auto* colors = styleManager->GetVisualTextStyle(styleIndex.visualTextStyleIndex, state))
		{
			entry->textColorIndex = colors->textColorIndex;
			entry->iconColorIndex = colors->iconColorIndex;
			std::ranges::copy(colors->decorationColorIndex, entry->decorationColorIndex);
			std::ranges::copy(colors->textShadows, entry->textShadows);
		}
		state = state & ~ElementState::Selected;
		entry->styleIndex = styleIndex;
		entry->state = state;

		const VisualTextStyle* visualTextStyle = styleManager->GetVisualTextStyle(styleIndex.visualTextStyleIndex, state);
		const LayoutStyle*     layoutStyle     = styleManager->GetLayoutStyle(styleIndex.layoutStyleIndex);
		const VisualTextStyle  text       = visualTextStyle ? *visualTextStyle : VisualTextStyle{};
		const TextLayoutStyle  textLayout = layoutStyle ? layoutStyle->textLayoutStyle : TextLayoutStyle{};

		// Only what the text is measured and laid out from decides whether it's laid out again.
		if (entry->styled &&
			entry->textStyle       == text.textStyle &&
			entry->textLayoutStyle == textLayout &&
			entry->iconSide        == text.iconSide &&
			entry->iconTextGap     == text.iconTextGap &&
			entry->iconSize        == text.iconSize &&
			entry->iconOffsetY     == text.iconOffsetY)
		{
			return;
		}
		entry->styled          = true;
		entry->textStyle       = text.textStyle;
		entry->textLayoutStyle = textLayout;

		entry->baseMeasurement = {};
		m_skribidiContext->ResetLayout(entry);

		entry->layout_attribute_count = style_to_attributes(&textLayout, entry->layout_attributes, 0);
		entry->text_attribute_count   = style_to_attributes(&text.textStyle, entry->text_attributes,
		                                                    m_skribidiContext->PushDefaultLanguage(entry->text_attributes), m_skribidiContext->m_fontIdToFamilyId);
		entry->lineHeight			  = std::max(1, static_cast<int32_t>(std::ceil(text.textStyle.CalcLineHeight())));
		entry->iconSide				  = text.iconSide;
		entry->iconTextGap			  = text.iconTextGap;
		entry->iconSize				  = text.iconSize;
		entry->iconOffsetY			  = text.iconOffsetY;
		entry->horizontalAlignment	  = textLayout.horizontalAlignment;
		entry->verticalAlignment	  = textLayout.verticalAlignment;
		entry->textWrapping			  = textLayout.textWrapping;
	}

	// -------------------- Font Registration --------------------

	void SkribidiTextSystem::SetFontDirectory(std::string_view directory)
	{
		m_fontDirectory = std::string(directory);
	}

	void SkribidiTextSystem::SetFallbackFontDirectory(std::string_view directory)
	{
		m_fallbackFontDirectory = std::string(directory);
	}

	void SkribidiTextSystem::SetLanguage(std::string_view languageTag)
	{
		if (!m_skribidiContext)
			return;
		if (m_skribidiContext->m_defaultLanguage == languageTag)
			return;
		m_skribidiContext->m_defaultLanguage = std::string(languageTag);
		// Laid-out text carries the old language; lay it out again.
		ClearCache();
	}

	FontID SkribidiTextSystem::RegisterFontFamily(std::string_view familyName, std::span<const FontFaceSource> faceList)
	{
		if (!m_skribidiContext || !m_skribidiContext->font_collection)
			return FontID::Invalid;

		const FontID fontId = FontIDFromName(familyName);
		if (fontId == FontID::Invalid)
			return FontID::Invalid;
		const uint32_t key = static_cast<uint32_t>(fontId);
		std::vector<StoredFontFace> faces;
		for (const FontFaceSource& face : faceList)
			faces.push_back(StoredFontFace{ std::string(face.file), face.data });
		auto& registeredFaces = m_skribidiContext->m_fontFamilyFaces;
		if (auto it = registeredFaces.find(key); it != registeredFaces.end())
		{
			if (it->second == faces)
				return fontId;
			// skribidi can't free single fonts without breaking match order, so swapping a
			// family's faces rebuilds the collection; a failed swap restores the old faces.
			std::vector<StoredFontFace> previous = std::exchange(it->second, std::move(faces));
			if (RebuildFontCollection(fontId))
				return fontId;
			registeredFaces[key] = std::move(previous);
			RebuildFontCollection(fontId);
			return FontID::Invalid;
		}

		// skribidi family IDs are a uint8_t; 255 stays free so the counter can't wrap.
		const uint8_t familyId = m_skribidiContext->m_nextFamilyId;
		if (familyId == UINT8_MAX)
			return FontID::Invalid;
		if (!skribidi_add_font_family(m_skribidiContext->font_collection, familyId, faceList, m_skribidiContext->m_fontFallback))
			return FontID::Invalid;

		m_skribidiContext->m_fontIdToFamilyId[key] = familyId;
		registeredFaces[key] = std::move(faces);
		m_skribidiContext->m_nextFamilyId++;
		return fontId;
	}

	bool SkribidiTextSystem::RebuildFontCollection(FontID fontId)
	{
		SkribidiContext& ctx = *m_skribidiContext;

		// Glyphs still waiting to be rasterized point at the fonts about to go, and no
		// layout, editor or recorded atlas use may outlive them. Dropped recordings make
		// their cached scopes render again.
		if (skb_image_atlas_rasterize_missing_items(ctx.atlas, ctx.temp_alloc, ctx.rasterizer))
			ctx.atlas_dirty = true;
		ClearCache();
		std::erase_if(ctx.atlasRecords, [&](const auto& entry) { return &entry.second != ctx.atlasRecorder; });
		if (ctx.atlasRecorder)
			ctx.atlasRecorder->clear();
		ctx.atlasRecorderKeys.clear();

		skb_font_collection_destroy(ctx.font_collection);
		ctx.font_collection = skb_font_collection_create();
		ctx.m_fontFallback.attempts.clear();
		if (!skribidi_load_fonts(ctx.font_collection, ctx.m_fontFallback))
			TACHYON_LOG_ERROR(kLogText, "could not reload the default fonts");

		bool loaded = false;
		for (const auto& [key, stored] : ctx.m_fontFamilyFaces)
		{
			std::vector<FontFaceSource> faces;
			for (const StoredFontFace& face : stored)
				faces.push_back(FontFaceSource{ face.file, face.data });
			const bool ok = skribidi_add_font_family(ctx.font_collection, ctx.m_fontIdToFamilyId.at(key), faces, ctx.m_fontFallback);
			if (key == static_cast<uint32_t>(fontId))
				loaded = ok;
		}
		return loaded;
	}

	// -------------------- Text Registration --------------------

	size_t SkribidiTextSystem::RegisteredTextCount() const
	{
		if (!m_skribidiContext) return 0;
		return m_skribidiContext->m_registry.size() - m_skribidiContext->m_freeTextSlots.size();
	}

	uint64_t SkribidiTextSystem::LayoutsMade() const
	{
		return m_skribidiContext ? m_skribidiContext->layoutsMade : 0;
	}

	TextID SkribidiTextSystem::RegisterRichText(ID elementId, const std::span<ContentRun>& runs,
                                                StyleRef styleIndex, ElementState state)
	{
		if (!m_skribidiContext)
			return TextID::Invalid;

		uint64_t key = RegisteredText::GetHashCode(elementId, runs);
		if (auto it = m_textKeyToIdMap.find(key); it != m_textKeyToIdMap.end())
		{
			auto* entry = m_skribidiContext->GetRegisteredText(it->second);
			entry->lastUsedFrame = m_skribidiContext->frameNumber;
			UpdateRegisteredTextStyleState(entry, styleIndex, state);
			// Always sync the per-element map
			m_elementIdToTextIdMap[Hash::Calculate(elementId)] = it->second;
			return it->second;
		}

		// The layout is created lazily, when the text is first measured.

		RegisteredText entry
		{
			.elementId  = elementId,
			.iconId		= IconID::Invalid,
			.text		= std::string(),
			.isRichText = true,
			.inputRuns  = std::vector<ContentRun>(runs.begin(), runs.end())
		};
		entry.CopyRunStorage();
		UpdateRegisteredTextStyleState(&entry, styleIndex, state);

		const TextID textId = m_skribidiContext->AddRegisteredText(std::move(entry));
		m_textKeyToIdMap[key] = textId;
		m_elementIdToTextIdMap[Hash::Calculate(elementId)] = textId;
		return textId;
	}

	TextID SkribidiTextSystem::RegisterText(ID elementId, IconID iconId, std::string_view text, tachyon::StyleRef styleIndex, tachyon::ElementState state)
	{
		if (!m_skribidiContext)
			return TextID::Invalid;

		uint64_t key = RegisteredText::GetHashCode(elementId, iconId, text);
		if (auto it = m_textKeyToIdMap.find(key); it != m_textKeyToIdMap.end())
		{
			auto* entry = m_skribidiContext->GetRegisteredText(it->second);
			entry->lastUsedFrame = m_skribidiContext->frameNumber;
			UpdateRegisteredTextStyleState(entry, styleIndex, state);
			// Always sync the per-element map
			m_elementIdToTextIdMap[Hash::Calculate(elementId)] = it->second;
			return it->second;
		}

		// The layout is created lazily, when the text is first measured.

		RegisteredText entry
		{
			.elementId  = elementId,
			.iconId     = iconId,
			.text       = std::string(text),
			.isRichText = false
		};
		UpdateRegisteredTextStyleState(&entry, styleIndex, state);

		const TextID textId = m_skribidiContext->AddRegisteredText(std::move(entry));
		m_textKeyToIdMap[key] = textId;
		m_elementIdToTextIdMap[Hash::Calculate(elementId)] = textId;
		return textId;
	}

	// -------------------- Text Measurement --------------------

	TextMeasurement SkribidiTextSystem::GetTextMeasurement(TextID textId) const
	{
		if (!m_skribidiContext) return {};
		auto registeredText = m_skribidiContext->GetRegisteredText(textId);
		if (registeredText == nullptr) return {};
		return UpdateBaseMeasurementWhenNecessary(registeredText);
	}

	TextMeasurement SkribidiTextSystem::MeasureText(TextID textId, const TextWrapContext& wrapContext) const
	{
		if (!m_skribidiContext) return {};
		//TACHYON_PROFILE_SCOPE("MeasureText");
		auto registeredText = m_skribidiContext->GetRegisteredText(textId);
		if (registeredText == nullptr) return {};
		if (wrapContext.maxWidth == INT32_MAX)
		{
            return UpdateBaseMeasurementWhenNecessary(registeredText);
		}
		return UpdateWrapMeasurementWhenNecessary(registeredText, wrapContext);
	}

	TextMeasurement SkribidiTextSystem::MeasureTextInternal(RegisteredText* entry, const skb_layout_t* layout) const
	{
		if (!m_skribidiContext)
			return {};

		if (!layout) return {};

		TACHYON_PROFILE_SCOPE("MeasureTextInternal");
		auto bounds = skb_layout_get_content_bounds(layout);

		// Content bounds give the line box width (the full layout width, e.g. 16384
		// when measuring unconstrained), not the glyph extent. The natural width is
		// the widest line minus its trimmed whitespace (padding_left/right).
		int32_t maxWidth = 0;
		const int32_t lineCount = skb_layout_get_lines_count(layout);
		const skb_layout_line_t* lines = skb_layout_get_lines(layout);
		for (int32_t i = 0; i < lineCount; ++i)
		{
			float w = lines[i].bounds.width - lines[i].padding_left - lines[i].padding_right;
			if (w > 0) maxWidth = std::max(maxWidth, (int32_t)std::ceil(w));
		}
		int32_t maxHeight = (int32_t)std::ceil(bounds.height);

		auto measurement = TextMeasurement
		{
			.minWidth = maxWidth,
			.maxWidth = maxWidth,
			.minHeight = maxHeight,
			.maxHeight = maxHeight
		};

		if (entry->iconId != IconID::Invalid)
		{
			auto iconMeasurement = GetIconMeasurement(entry->iconId);
			if (entry->iconSize > 0)
			{
				iconMeasurement.width  = entry->iconSize;
				iconMeasurement.height = entry->iconSize;
			}
            switch (entry->iconSide)
            {
                case IconSide::None: break;
                case IconSide::Left:
                case IconSide::Right:
                {
					measurement.minWidth += entry->iconTextGap + iconMeasurement.width;
					measurement.maxWidth += entry->iconTextGap + iconMeasurement.width;

					measurement.minHeight = std::max(measurement.minHeight, iconMeasurement.height);
					measurement.maxHeight = std::max(measurement.maxHeight, iconMeasurement.height);
                    break;
                }
                case IconSide::Top:
                case IconSide::Bottom:
                {
					measurement.minHeight += entry->iconTextGap + iconMeasurement.height;
					measurement.maxHeight += entry->iconTextGap + iconMeasurement.height;

					measurement.minWidth = std::max(measurement.minWidth, iconMeasurement.width);
					measurement.maxWidth = std::max(measurement.maxWidth, iconMeasurement.width);
                    break;
                }
            }
		}

		return measurement;
	}

	// -------------------- Text Rendering --------------------

	TextMeasurement SkribidiTextSystem::UpdateBaseMeasurementWhenNecessary(RegisteredText* entry) const
	{
		if (entry == nullptr) return {};

		// Valid until a text, state or style change invalidates it.
		if (entry->base_measurement_valid)
		{
			return entry->baseMeasurement;
		}

		//TACHYON_PROFILE_SCOPE("UpdateBaseMeasurementWhenNecessary");

		entry->base_measurement_valid = true;
		
		// Max-content: one unconstrained line per paragraph.
		skb_layout_t* layout = m_skribidiContext->CreateLayout(entry, 16384.0f, 16384.0f);
		entry->baseMeasurement = MeasureTextInternal(entry, layout);
		skb_layout_destroy(layout);

		// Min-content: wrapping text laid out as narrow as it goes (one word, or one
		// character for word-char, per line). Text that doesn't wrap is one line either way.
		if (entry->textWrapping != TextWrap::NoWrap && entry->baseMeasurement.maxWidth > 0)
		{
			skb_layout_t* narrow = m_skribidiContext->CreateLayout(entry, 1.0f, 16384.0f);
			entry->baseMeasurement.minWidth = std::min(entry->baseMeasurement.maxWidth,
			                                           MeasureTextInternal(entry, narrow).maxWidth);
			skb_layout_destroy(narrow);
		}
		return entry->baseMeasurement;
	}

	TextMeasurement SkribidiTextSystem::UpdateWrapMeasurementWhenNecessary(RegisteredText* entry, const TextWrapContext& wrapContext) const
	{
		if (entry == nullptr) return {};

		//TACHYON_PROFILE_SCOPE("UpdateWrapMeasurementWhenNecessary");
		[[maybe_unused]]
		auto layout = (skb_layout_t*)UpdateLayoutWhenNecessary(entry, (float)wrapContext.maxWidth);
		if (entry->wrapMeasurement.IsEmpty())
		{
			entry->wrapMeasurement = MeasureTextInternal(entry, entry->wrap_layout);
		}
		return entry->wrapMeasurement;
	}

	void* SkribidiTextSystem::UpdateLayoutWhenNecessary(RegisteredText* entry, float width) const
	{
		if (entry == nullptr) return {};

		// Text, state or style changes destroy wrap_layout and reset wrapMeasurement;
		// both are rebuilt here on demand.
		if (entry->wrap_layout != nullptr &&
			entry->wrap_width == width)
		{
			return entry->wrap_layout;
		}

		TACHYON_PROFILE_SCOPE("UpdateLayoutWhenNecessary");
		// Another width only replaces the wrapped layout; the base measurement doesn't depend on it.
		if (entry->wrap_layout != nullptr)
			skb_layout_destroy(entry->wrap_layout);
		entry->wrapMeasurement = {};

		entry->wrap_width = width;
		entry->wrap_layout = m_skribidiContext->CreateLayout(entry, width, 16384);

		assert(entry->wrap_layout);
		return entry->wrap_layout;
	}


	void SkribidiTextSystem::RenderText(TextID textId, IntRect rect, IntRect innerRectScreen, IntRect clipRect, float pixelScale,
	                                    const float3x2& screenToLayout)
	{
		if (!m_skribidiContext) return;
		auto entry = m_skribidiContext->GetRegisteredText(textId);
		if (entry == nullptr) return;
		entry->screenToLayout = screenToLayout;

		if ((entry->state & ElementState::Focused) != ElementState::None &&
			entry->elementId == m_skribidiContext->guiStateManager->TextEditorFocus().GetFocusedID())
		{
			RenderEditor(entry, rect, innerRectScreen, clipRect, pixelScale);
		} else
		{
			RenderText(entry, rect, innerRectScreen, clipRect, pixelScale);

			// While dragging selected text from another editor, show insertion caret in hovered destination textbox
			if (m_textDragSourceId != ID::Invalid() &&
				m_skribidiContext->drag_text &&
				entry->elementId != m_textDragSourceId &&
				IsTextEditor(entry->elementId) &&
				CanDropDraggedTextInto(entry->elementId) &&
				(m_skribidiContext->frameMouseButtons & MouseButtons::Left) != MouseButtons::None)
			{
				const int2 screenMousePos = m_skribidiContext->frameMousePos;
				const ID hoveredTextEditor = FindTextEditorAtScreenPoint(ID::Invalid(), screenMousePos);
				if (hoveredTextEditor == entry->elementId)
				{
					tachyon::float2 localPos{ static_cast<float>(screenMousePos.x), static_cast<float>(screenMousePos.y) };
					if (TryGetTextEditorLocalPoint(entry->elementId, ID::Invalid(), screenMousePos, localPos))
					{
						const auto* layout = static_cast<const skb_layout_t*>(UpdateLayoutWhenNecessary(entry, static_cast<float>(rect.w)));
						if (layout)
						{
							const skb_text_position_t caretPos = skb_layout_hit_test(layout, SKB_MOVEMENT_CARET, localPos.x, localPos.y);
							const skb_caret_info_t caretInfo = skb_layout_get_caret_info_at(layout, caretPos);

							const float x0 = rect.x + caretInfo.x + caretInfo.descender * caretInfo.slope;
							const float y0 = rect.y + caretInfo.y + caretInfo.descender;
							const float x1 = rect.x + caretInfo.x + caretInfo.ascender * caretInfo.slope;
							const float y1 = rect.y + caretInfo.y + caretInfo.ascender;

							const tachyon::int2 p0 = { static_cast<int>(x0), static_cast<int>(y0) };
							const tachyon::int2 p1 = { static_cast<int>(x1), static_cast<int>(y1) };

							m_skribidiContext->guiRenderer->RenderLine(p0, p1, clipRect, LineStyle{ .colorStyleIndex = m_skribidiContext->caretColorStyleIndex, .lineWidth = m_skribidiContext->Px(2.0f) });
						}
					}
				}
			}
		}
	}

	std::optional<MouseCursor> SkribidiTextSystem::LinkCursorAt(ID elementId, int32_t localX, int32_t localY) const
	{
		if (!m_skribidiContext) return std::nullopt;
		uint64_t key = Hash::Calculate(elementId);
		auto it = m_elementIdToTextIdMap.find(key);
		if (it == m_elementIdToTextIdMap.end())
			return std::nullopt;
		auto* entry = m_skribidiContext->GetRegisteredText(it->second);
		if (!entry || !entry->wrap_layout) return std::nullopt;
		intptr_t contentId = skb_layout_hit_test_content(entry->wrap_layout,
			float(localX - entry->renderOrigin.x),
			float(localY - entry->renderOrigin.y)).content_id;
		if (contentId == 0)
			return std::nullopt;
		for (const auto& run : entry->inputRuns)
		{
			if (run.contentId == contentId && run.isLink)
				return run.linkStyle.cursor;
		}
		return std::nullopt;
	}

	intptr_t SkribidiTextSystem::HitTestContent(TextID textId, int32_t localX, int32_t localY) const
	{
		if (!m_skribidiContext) return 0;
		auto* entry = m_skribidiContext->GetRegisteredText(textId);
		if (!entry || !entry->wrap_layout) return 0;
		return skb_layout_hit_test_content(entry->wrap_layout,
			float(localX - entry->renderOrigin.x),
			float(localY - entry->renderOrigin.y)).content_id;
	}

	void SkribidiTextSystem::SetHoveredContent(TextID textId, intptr_t contentId)
	{
		if (!m_skribidiContext) return;
		auto* entry = m_skribidiContext->GetRegisteredText(textId);
		if (!entry) return;
		entry->hoveredContentId = contentId;
	}

	// -------------------- Icon Rendering --------------------

	void SkribidiTextSystem::RenderIcon(IconID iconId, IntRect rect, IntRect clipRect, VisualColorStyleIndex tint, std::span<const VisualShadow> shadows)
	{
		if (!m_skribidiContext)
			return;
		m_skribidiContext->DrawWithShadows(shadows, [&] { DrawIcon(iconId, rect, clipRect, tint); });
	}

	void SkribidiTextSystem::DrawIcon(IconID iconId, IntRect rect, IntRect clipRect, VisualColorStyleIndex tint)
	{
		if (!m_skribidiContext || !m_skribidiContext->icon_collection)
			return;
		
		if (iconId == IconID::Invalid)
			return;
		
		const skb_icon_handle_t handle = m_skribidiContext->GetIconHandle(iconId);
		if (handle == 0)
			return;

		auto* ctx = m_skribidiContext;
		
		skb_vec2_t naturalSize = skb_icon_collection_get_icon_size(ctx->icon_collection, handle);
		
		// Fit within rect, keeping the aspect ratio.
		float iconAspect = naturalSize.x / naturalSize.y;
		float rectAspect = float(rect.w) / float(rect.h);
		
		float renderWidth, renderHeight;
		float offsetX = 0.0f, offsetY = 0.0f;
		
		if (iconAspect > rectAspect)
		{
			// Icon is wider - fit to width
			renderWidth = float(rect.w);
			renderHeight = renderWidth / iconAspect;
			offsetY = (float(rect.h) - renderHeight) * 0.5f;
		}
		else
		{
			// Icon is taller or same aspect - fit to height
			renderHeight = float(rect.h);
			renderWidth = renderHeight * iconAspect;
			offsetX = (float(rect.w) - renderWidth) * 0.5f;
		}
		
		float prev_x = ctx->view.cx;
		float prev_y = ctx->view.cy;
		ctx->view = view_t { 
			.cx = std::round(float(rect.x) + offsetX) - 0.5f,
			.cy = std::round(float(rect.y) + offsetY) - 0.5f, 
			.scale = 1.f,
			.zoom_level = 0.0f
		};
		
		skb_rasterize_alpha_mode_t mode = SKB_RASTERIZE_ALPHA_SDF;
		
		skb_quad_t quad = ctx->GetIconQuad(0.0f, 0.0f,  // Position in icon space
			ctx->view.scale,  // pixel_scale
			ctx->icon_collection,
			handle,
			renderWidth,
			renderHeight,
			skb_rgba(255, 255, 255, 255),  // tint_color
			mode);  // alpha_mode
		
		int bpp = skb_image_atlas_get_texture(ctx->atlas, quad.texture_idx)->bpp;
		
		draw_glyph_quad_sdf(ctx,
			view_transform_rect(&ctx->view, quad.geom),
			quad.texture,
			1.f / quad.scale, bpp, mode,
			(quad.flags & SKB_QUAD_IS_COLOR) ? VisualColorStyleIndex::Invalid : tint,
			(uint32_t)skb_image_atlas_get_texture_user_data(ctx->atlas, quad.texture_idx),
			clipRect);
		
		ctx->view.cx = prev_x;
		ctx->view.cy = prev_y;
	}

	void SkribidiTextSystem::RenderText(RegisteredText* entry, IntRect rect, IntRect innerRectScreen, IntRect clipRect, float pixelScale)
	{
		entry->innerRect = innerRectScreen;

		// A Left/Right icon takes its width out of the layout, so text-align: end
		// doesn't draw text under the icon. Same math as ComputeIconTextPlacement.
		const bool hasIcon = (entry->iconId != IconID::Invalid);
		IconMeasurement iconMeasurement = hasIcon ? GetIconMeasurement(entry->iconId)
		                                          : IconMeasurement{};
		if (hasIcon && entry->iconSize > 0)
		{
			iconMeasurement.width  = entry->iconSize;
			iconMeasurement.height = entry->iconSize;
		}
		const int32_t layoutMaxWidth = IconAwareLayoutMaxWidth(
			rect.size.x, iconMeasurement, entry->iconSide, entry->iconTextGap, hasIcon);
		auto layout = (skb_layout_t*)UpdateLayoutWhenNecessary(entry, float(layoutMaxWidth));
		if (layout == nullptr) return;


		auto* ctx = m_skribidiContext;

		IntRect textRect = rect;
		IntRect iconRenderRect = { 0, 0, 0, 0 };

		if (hasIcon)
		{
			const skb_rect2_t textBounds = skb_layout_get_bounds(layout);
			const int32_t textWidth  = static_cast<int32_t>(std::ceil(textBounds.width));
			const int32_t textHeight = static_cast<int32_t>(std::ceil(textBounds.height));

			const IconTextPlacement placement = ComputeIconTextPlacement(
				rect, textWidth, textHeight, iconMeasurement,
				entry->iconSide, entry->iconTextGap,
				entry->horizontalAlignment, entry->verticalAlignment);
			textRect       = placement.textRect;
			iconRenderRect = placement.iconRect;

			// Per-style nudge for icons whose ink is off-centre in the viewBox;
			// positive lifts the icon.
			if (entry->iconOffsetY != 0)
				iconRenderRect.y -= entry->iconOffsetY;
		}
		else
		{
			// skribidi lays glyphs from the top of the rect, so vertical alignment is
			// applied here; horizontal alignment is a layout attribute.
			const skb_rect2_t textBounds = skb_layout_get_bounds(layout);
			const int32_t textHeight = static_cast<int32_t>(std::ceil(textBounds.height));
			textRect.position.y = rect.position.y + VerticalAlignOffset(entry->verticalAlignment, rect.size.y, textHeight);
		}

		entry->renderOrigin = textRect.position - rect.position;

		float prev_x = ctx->view.cx;
		float prev_y = ctx->view.cy;
		const view_t textView
		{ 
			.cx = float(textRect.position.x), 
			.cy = float(textRect.position.y), 
			.scale = 1.f, 
			.zoom_level = 0.f 
		};

		const skb_layout_run_t*		layout_runs			= skb_layout_get_layout_runs(layout);
		const int32_t				layout_runs_count	= skb_layout_get_layout_runs_count(layout);
		const skb_glyph_t*			glyphs				= skb_layout_get_glyphs(layout);
		const skb_layout_params_t*	layout_params		= skb_layout_get_params(layout);
		const int32_t				decorations_count	= skb_layout_get_decorations_count(layout);
		const skb_decoration_t*		decorations			= skb_layout_get_decorations(layout);

		skb_rasterize_alpha_mode_t mode = SKB_RASTERIZE_ALPHA_SDF;

		// The renderer takes colour-style indices, so a run's own colour (e.g. a link)
		// is registered on demand; RegisterColorStyle dedups.
		auto runColorIndex = [&](const skb_attribute_paint_t& p) -> VisualColorStyleIndex
		{
			const color4b c{ p.color.r, p.color.g, p.color.b, p.color.a };
			return m_skribidiContext->styleManager->RegisterColorStyle(ColorStyle::Create(c), ImageIndex::Invalid);
		};

		// Under the text, its decorations and its icon, every shadow; then those themselves.
		ctx->DrawWithShadows(entry->textShadows, [&]
		{
			ctx->view = textView;
			for (int32_t i = 0; i < decorations_count; i++)
			{
				const skb_decoration_t* decoration = &decorations[i];
				if (decoration->layer != SKB_DECORATION_UNDER)
					continue;

				const skb_layout_run_t* run = &layout_runs[decoration->line.layout_run_idx];
				const skb_attribute_set_t run_attributes = skb_layout_get_layout_run_attributes(layout, run);
				const uint32_t paint_state = (run->content_id != 0 && run->content_id == entry->hoveredContentId)
					? SKB_PAINT_STATE_HOVER : SKB_PAINT_STATE_DEFAULT;
				// A link's underline is shown ONLY on hover; plain underlines always show.
				const bool isLinkDeco = (decoration->line.paint_tag == SKB_PAINT_DECORATION_LINK);
				if (isLinkDeco && paint_state != SKB_PAINT_STATE_HOVER)
					continue;
				const skb_attribute_paint_t paint = skb_attributes_get_paint(decoration->line.paint_tag, paint_state, run_attributes, layout_params->attribute_collection);
				if (paint.paint_tag != decoration->line.paint_tag)
					continue;

				skb_quad_t pat_quad = ctx->GetDecorationQuad(decoration->line.x, decoration->line.y, pixelScale,
					(skb_decoration_position_t)decoration->line.position,
					(skb_decoration_style_t)decoration->line.style,
					decoration->line.length, decoration->line.pattern_offset, decoration->line.thickness,
					paint.color, mode);

				int bpp = skb_image_atlas_get_texture(ctx->atlas, pat_quad.texture_idx)->bpp;
				// Snap the underline to whole pixel rows so the SDF doesn't blur it.
				skb_rect2_t decoRect = view_transform_rect(&ctx->view, pat_quad.geom);
				decoRect.y      = std::round(decoRect.y);
				decoRect.height = std::max(1.0f, std::round(decoRect.height));
				draw_image_pattern_quad_sdf(ctx,
					decoRect,
					pat_quad.pattern, pat_quad.texture,
					1.0f / pat_quad.scale, bpp, mode,
					isLinkDeco ? runColorIndex(paint) : entry->DecorationColor((skb_decoration_position_t)decoration->line.position),
					(uint32_t)skb_image_atlas_get_texture_user_data(ctx->atlas, pat_quad.texture_idx),
					clipRect);
			}

			for (int32_t ri = 0; ri < layout_runs_count; ri++)
			{
				const skb_layout_run_t* run = &layout_runs[ri];
			
				if (run->type == SKB_CONTENT_RUN_ICON)
				{
					skb_quad_t quad = ctx->GetIconQuad(run->bounds.x, run->bounds.y,
					 pixelScale,  // pixel_scale
						ctx->icon_collection,
						run->icon_handle,
						run->bounds.width,
						run->bounds.height,
						skb_rgba(255, 255, 255, 255), // tint_color
						mode);  // alpha_mode
				
					int bpp = skb_image_atlas_get_texture(ctx->atlas, quad.texture_idx)->bpp;
				
					draw_glyph_quad_sdf(ctx,
						view_transform_rect(&ctx->view, quad.geom),
						quad.texture,
						1.f / quad.scale, bpp, mode,
						(quad.flags & SKB_QUAD_IS_COLOR) ? VisualColorStyleIndex::Invalid : entry->textColorIndex,
						(uint32_t)skb_image_atlas_get_texture_user_data(ctx->atlas, quad.texture_idx),
						clipRect);

					continue;
				}
			
				const skb_attribute_set_t run_attributes = skb_layout_get_layout_run_attributes(layout, run);
				const uint32_t paint_state = (run->content_id != 0 && run->content_id == entry->hoveredContentId)
					? SKB_PAINT_STATE_HOVER : SKB_PAINT_STATE_DEFAULT;
				skb_attribute_paint_t paint = skb_attributes_get_paint(SKB_PAINT_TEXT, paint_state, run_attributes, layout_params->attribute_collection);

				skb_color_t color = paint.color;
				// A run with its own colour (link) renders in that colour; otherwise the
				// element's single text colour is used.
				const VisualColorStyleIndex glyphColor = (run->content_id != 0) ? runColorIndex(paint) : entry->textColorIndex;
				for (int32_t gi = run->glyph_range.start; gi < run->glyph_range.end; gi++)
				{
					const skb_glyph_t* glyph = &glyphs[gi];
					const float gx = glyph->offset_x;
					const float gy = glyph->offset_y;

					skb_quad_t quad = ctx->GetGlyphQuad(gx, gy, pixelScale,
						layout_params->font_collection, run->font_handle, glyph->gid,
						run->font_size, color, mode);
					if (quad.flags & SKB_QUAD_IS_EMPTY)
						continue;

					int bpp = skb_image_atlas_get_texture(ctx->atlas, quad.texture_idx)->bpp;

					draw_glyph_quad_sdf(ctx,
						view_transform_rect(&ctx->view, quad.geom),
						quad.texture,
						1.f / quad.scale, bpp, mode,
						(quad.flags & SKB_QUAD_IS_COLOR) ? VisualColorStyleIndex::Invalid : glyphColor,
						(uint32_t)skb_image_atlas_get_texture_user_data(ctx->atlas, quad.texture_idx),
						clipRect);
				}
			}

			for (int32_t i = 0; i < decorations_count; i++)
			{
				const skb_decoration_t* decoration = &decorations[i];
				if (decoration->layer != SKB_DECORATION_OVER)
					continue;

				const skb_layout_run_t* run = &layout_runs[decoration->line.layout_run_idx];
				const skb_attribute_set_t run_attributes = skb_layout_get_layout_run_attributes(layout, run);
				const uint32_t paint_state = (run->content_id != 0 && run->content_id == entry->hoveredContentId)
					? SKB_PAINT_STATE_HOVER : SKB_PAINT_STATE_DEFAULT;
				const skb_attribute_paint_t paint = skb_attributes_get_paint(decoration->line.paint_tag, paint_state, run_attributes, layout_params->attribute_collection);
				if (paint.paint_tag != decoration->line.paint_tag)
					continue;

				skb_quad_t pat_quad = ctx->GetDecorationQuad(decoration->line.x, decoration->line.y, pixelScale,
					(skb_decoration_position_t)decoration->line.position,
					(skb_decoration_style_t)decoration->line.style,
					decoration->line.length, decoration->line.pattern_offset, decoration->line.thickness,
					paint.color, mode);

				int bpp = skb_image_atlas_get_texture(ctx->atlas, pat_quad.texture_idx)->bpp;
				draw_image_pattern_quad_sdf(ctx,
					view_transform_rect(&ctx->view, pat_quad.geom),
					pat_quad.pattern, pat_quad.texture,
					1.0f / pat_quad.scale, bpp, mode,
					entry->DecorationColor((skb_decoration_position_t)decoration->line.position),
					(uint32_t)skb_image_atlas_get_texture_user_data(ctx->atlas, pat_quad.texture_idx),
					clipRect);
			}

			ctx->view.cx = prev_x;
			ctx->view.cy = prev_y;

			// After restoring the view: DrawIcon sets its own.
			if (entry->iconId != IconID::Invalid)
			{
				DrawIcon(entry->iconId, iconRenderRect, clipRect, entry->iconColorIndex);
			}
		});
	}

} // namespace tachyon