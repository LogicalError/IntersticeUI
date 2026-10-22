#include <gui_core/diagnostics/gui_logging.h>
#include <gui_core/gui_base_types.h>

#include <gui_core/style/gui_style_manager.h>
#include <gui_core/style/gui_gradient_manager.h>

#include <gui_core/text/gui_text_atlas_manager.h>

#include <gui_core/text/skribidi/internal/gui_skribidi_context.h>

#include <gui_core/graphics/gui_graphics_context.h>
#include <gui_core/graphics/gui_renderer.h>

#include <skribidi/skb_rich_layout.h>
#include <skribidi/skb_icon_collection.h>

static constexpr GUI::Diagnostics::LogCategory kLogText = GUI::Diagnostics::MakeLogCategory("text");

#undef SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY
#define SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(array) skb_attribute_set_t { .attributes = (array), .attributes_count = SKB_COUNTOF(array) }

namespace GUI
{
	skb_rect2_t view_transform_rect(view_t* view, skb_rect2_t geom)
	{
		return skb_rect2_t
		{
			.x = geom.x * view->scale + view->cx,
			.y = geom.y * view->scale + view->cy,
			.width = geom.width * view->scale,
			.height = geom.height * view->scale,
		};
	}

	void OnCreateTexture(skb_image_atlas_t* atlas, uint8_t texture_idx, void* ctx_ptr)
	{
		if (ctx_ptr == nullptr)
			return;
		auto* ctx = static_cast<SkribidiContext*>(ctx_ptr);
		if (ctx->textAtlasManager == nullptr)
			return;

		const skb_image_t* texture = skb_image_atlas_get_texture(atlas, texture_idx);
		if (texture)
		{
			auto textureIndex = ctx->textAtlasManager->CreateTexture(NULL, uint2 { .x = uint32_t(texture->width), .y = uint32_t(texture->height) }, texture->bpp);
			skb_image_atlas_set_texture_user_data(atlas, texture_idx, (uint32_t)textureIndex);
		}

	}

	bool SkribidiContext::Initialize(GraphicsContext* inGraphicsContext, StyleManager* inStyleManager,
	                                 TextAtlasManager* inTextAtlasManager,
	                                 GUIStateManager* inGuiStateManager, IClipboard* inClipboard)
	{
		if (initialized)
			return false;

		// A null GraphicsContext is allowed for measurement-only use (tests);
		// guiRenderer is only dereferenced when rendering.
		this->guiRenderer      = inGraphicsContext ? inGraphicsContext->guiRenderer : nullptr;
		this->styleManager     = inStyleManager;
		this->clipboard        = inClipboard;
		this->textAtlasManager = inTextAtlasManager;
		this->guiStateManager  = inGuiStateManager;

		{
			caretColorStyleIndex = styleManager->RegisterColorStyle(
				ColorStyle::Create(color4f{ 1.0f, 128.0f / 255.0f, 128.0f / 255.0f, 1.0f }));
		}

		atlas_scale = 0.25f;
		view = view_t { .cx = 0, .cy = 0, .scale = 1, .zoom_level = 0 };

		temp_alloc = skb_temp_alloc_create(512 * 1024);
		assert(temp_alloc);

		font_collection = skb_font_collection_create();
		assert(font_collection);

		if (!skribidi_load_fonts(font_collection, m_fontFallback))
		{
			LOG_ERROR(kLogText, "could not load the default fonts from {}", m_fontFallback.fontDirectory);
			Shutdown();
			return false;
		}

		icon_collection = skb_icon_collection_create();
		assert(icon_collection);

		rich_text_clipboard = skb_rich_text_create();
		if (!rich_text_clipboard)
		{
			Shutdown();
			return false;
		}

		auto config = skb_image_atlas_get_default_config();
		
		config.init_width = 1024;
		config.init_height = 1024;

		atlas = skb_image_atlas_create(&config);
		assert(atlas);
		skb_image_atlas_set_create_texture_callback(atlas, &OnCreateTexture, this);

        skb_rasterizer_config_t rasterizer_config = skb_rasterizer_get_default_config();
		rasterizer = skb_rasterizer_create(&rasterizer_config);
		assert(rasterizer);

		skb_image_atlas_compact(atlas);
		initialized = true;
		return true;
	}

	void SkribidiContext::ResetLayout(TextID textID)
	{
		ResetLayout(GetRegisteredText(textID));
	}

	void SkribidiContext::ResetLayout(RegisteredText* entry)
	{
		if (entry == nullptr)
			return;
		if (entry->wrap_layout) skb_layout_destroy(entry->wrap_layout);
		entry->wrap_layout = nullptr;
		entry->wrapMeasurement = {};
		entry->base_measurement_valid = false;
	}

	void SkribidiContext::Shutdown()
	{
        for (size_t i = 0; i < m_registry.size(); ++i)
		{
			ResetLayout(&m_registry[i]);
		}
		m_registry.clear();
		m_freeTextSlots.clear();

        editing_text_id = TextID::Invalid;

		if (editor)
		{
			skb_editor_destroy(editor);
			editor = nullptr;
		}

		if (rich_text_clipboard)
		{
			skb_rich_text_destroy(rich_text_clipboard);
			rich_text_clipboard = nullptr;
		}

		if (font_collection)
		{
			skb_font_collection_destroy(font_collection);
			font_collection = nullptr;
		}
		m_fontIdToFamilyId.clear();
		m_fontFamilyFaces.clear();
		m_nextFamilyId = 6;
		m_fontFallback.attempts.clear();

		if (icon_collection)
		{
			skb_icon_collection_destroy(icon_collection);
			icon_collection = nullptr;
		}

		if (atlas)
		{
			skb_image_atlas_destroy(atlas);
			atlas = nullptr;
		}

		if (rasterizer)
		{
			skb_rasterizer_destroy(rasterizer);
			rasterizer = nullptr;
		}

		if (temp_alloc)
		{
			skb_temp_alloc_destroy(temp_alloc);
			temp_alloc = nullptr;
		}

		rich_text_clipboard_hash = 0;
		rich_text_clipboard = nullptr;
		guiRenderer      = nullptr;
		styleManager     = nullptr;
		clipboard        = nullptr;
		textAtlasManager = nullptr;
		guiStateManager  = nullptr;
		view = {};
		drag_view = false;
		drag_text = false;
		atlas_scale = 0.0f;
		allow_char = false;
		show_glyph_bounds = false;
		show_glyph_details = false;
		show_caret_details = false;
		active_text_id = TextID::Invalid;
		active_element_id = ID::Invalid();
		pending_select_all_id = TextID::Invalid;
		editor_drag_active = false;
		pending_text_id = TextID::Invalid;
		pending_text_dirty = false;
		pending_text_committed = false;
		pending_text_valid = false;
		pending_text.clear();
		iconHandles.clear();
	}

	skb_color_t toSKBColor(const color4b& color)
	{
		return skb_color_t { color.r, color.g, color.b, color.a };
	}

	skb_text_overflow_t toSKBTextOverflow(TextOverflow overflow)
	{
		switch (overflow)
		{
			case TextOverflow::Ellipsis: return SKB_OVERFLOW_ELLIPSIS;
			case TextOverflow::Clip: return SKB_OVERFLOW_NONE;
			default: return SKB_OVERFLOW_NONE;
		}
	}

	void runs_to_skribidi(const SkribidiContext& context, const std::span<ContentRun>& inputRuns, skb_icon_collection_t* iconCollection,
	                      skb_attribute_set_t attributeSet,
	                      std::vector<skb_content_run_t>& skbRuns,
	                      std::vector<RegisteredText::PerRunLinkAttributes>& linkAttrStorage,
	                      const std::unordered_map<uint32_t, uint8_t>& fontIdToFamilyId)
	{
		skbRuns.clear();
		linkAttrStorage.clear();
		skbRuns.reserve(inputRuns.size());

		// Reserve for per-run overrides: content runs hold pointers into this storage.
		size_t overrideRunCount = 0;
		for (const auto& run : inputRuns)
			if (run.type == ContentRun::Text &&
			    ((run.isLink && run.contentId != 0) || run.textStyleOverride != nullptr))
				overrideRunCount++;
		linkAttrStorage.reserve(overrideRunCount);

		for (size_t i = 0; i < inputRuns.size(); i++)
		{
			const auto& run = inputRuns[i];
			if (run.type == ContentRun::Text)
			{
				if (run.isLink && run.contentId != 0)
				{
					// Shared text attributes, then link-specific overrides.
					RegisteredText::PerRunLinkAttributes linkAttrs;
					int n = 0;
					const int copyMax = RegisteredText::PerRunLinkAttributes::kMaxCount - 6;
					for (int k = 0; k < attributeSet.attributes_count && n < copyMax; k++)
						linkAttrs.data[n++] = attributeSet.attributes[k];

					skb_color_t color      = toSKBColor(run.linkStyle.color);
					skb_color_t hoverColor = toSKBColor(run.linkStyle.hoverColor);
					linkAttrs.data[n++] = skb_attribute_make_paint_color(SKB_PAINT_TEXT, SKB_PAINT_STATE_DEFAULT, color);
					linkAttrs.data[n++] = skb_attribute_make_paint_color(SKB_PAINT_TEXT, SKB_PAINT_STATE_HOVER, hoverColor);
					linkAttrs.data[n++] = skb_attribute_make_paint_color(SKB_PAINT_DECORATION_LINK, SKB_PAINT_STATE_DEFAULT, color);
					linkAttrs.data[n++] = skb_attribute_make_paint_color(SKB_PAINT_DECORATION_LINK, SKB_PAINT_STATE_HOVER, hoverColor);
					linkAttrs.data[n++] = skb_attribute_make_decoration(SKB_DECORATION_LINE_UNDER, SKB_DECORATION_STYLE_SOLID, 0.0f, 0.0f, SKB_PAINT_DECORATION_LINK);
					linkAttrs.count = n;

					linkAttrStorage.push_back(linkAttrs); // no reallocation: reserved above
					skb_attribute_set_t linkAttrSet = { .attributes = linkAttrStorage.back().data, .attributes_count = n };
					skbRuns.push_back(skb_content_run_make_utf8(
						run.text.data(),
						static_cast<int32_t>(run.text.length()),
						linkAttrSet,
						run.contentId));
				}
				else if (run.textStyleOverride != nullptr)
				{
					// Shared text attributes, then the style override.
					RegisteredText::PerRunLinkAttributes overrideAttrs;
					std::span<skb_attribute_t> overrideSpan{overrideAttrs.data, RegisteredText::PerRunLinkAttributes::kMaxCount};
					int n = 0;
					const int copyMax = RegisteredText::PerRunLinkAttributes::kMaxCount - 16;
					for (int k = 0; k < attributeSet.attributes_count && n < copyMax; k++)
						overrideAttrs.data[n++] = attributeSet.attributes[k];
					const TextStyle overrideStyle = context.styleManager ? context.styleManager->ToPixels(*run.textStyleOverride)
					                                                     : *run.textStyleOverride;
					n += style_to_attributes(&overrideStyle, overrideSpan, n, fontIdToFamilyId);
					overrideAttrs.count = n;

					linkAttrStorage.push_back(overrideAttrs);
					skb_attribute_set_t overrideAttrSet = { .attributes = linkAttrStorage.back().data, .attributes_count = n };
					skbRuns.push_back(skb_content_run_make_utf8(
						run.text.data(),
						static_cast<int32_t>(run.text.length()),
						overrideAttrSet,
						run.contentId));
				}
				else
				{
					skbRuns.push_back(skb_content_run_make_utf8(
						run.text.data(),
						static_cast<int32_t>(run.text.length()),
						attributeSet,
						run.contentId));
				}
			} else
			if (run.type == ContentRun::Icon)
			{
				if (const skb_icon_handle_t iconHandle = context.GetIconHandle(run.iconID))
				{
					skb_vec2_t iconSize = skb_icon_collection_get_icon_size(
						iconCollection, (uint32_t)iconHandle);

					if (iconSize.x > 0 && iconSize.y > 0)
					{
						// CSS pixels; a negative size means the icon's natural size.
						const float width  = context.Px(run.iconWidth  < 0 ? iconSize.x : run.iconWidth);
						const float height = context.Px(run.iconHeight < 0 ? iconSize.y : run.iconHeight);

						skbRuns.push_back(skb_content_run_make_icon(
							iconHandle,
							width,
							height,
							attributeSet,
							run.contentId));
					} else
					{
						// Empty icon: show replacement text.
						const char* replacement = "?";
						skbRuns.push_back(skb_content_run_make_utf8(
							replacement, 1, attributeSet,
							run.contentId));
					}
				} else
				{
					// Unknown icon: show replacement text.
					const char* replacement = "?";
					skbRuns.push_back(skb_content_run_make_utf8(
						replacement, 1, attributeSet,
						run.contentId));
				}
			}
		}
	}

	skb_layout_t* SkribidiContext::CreateLayout(RegisteredText* entry, float layoutWidth, float layoutHeight) const
	{
		skb_attribute_set_t text_attribute_set = { .attributes = entry->text_attributes,   .attributes_count = entry->text_attribute_count };
		skb_attribute_set_t layout_attribute_set = { .attributes = entry->layout_attributes, .attributes_count = entry->layout_attribute_count };

		skb_layout_params_t layout_params =
		{
			.font_collection = font_collection,
			.icon_collection = icon_collection,
			.layout_width = layoutWidth,
			.layout_height = layoutHeight,
			.layout_attributes = layout_attribute_set
		};

		if (entry->isRichText && !entry->inputRuns.empty())
		{
			entry->BindRunStorage();
			runs_to_skribidi(*this, entry->inputRuns, icon_collection, text_attribute_set, entry->contentRuns, entry->perRunLinkAttrStorage, m_fontIdToFamilyId);
			return skb_layout_create_from_runs(temp_alloc, &layout_params,
											   entry->contentRuns.data(),
											   static_cast<int32_t>(entry->contentRuns.size()));
		} else
		{
			return skb_layout_create_utf8(temp_alloc, &layout_params,
										  entry->text.data(), int32_t(entry->text.length()),
										  text_attribute_set);
		}
	}

	int style_to_attributes(const TextLayoutStyle* textLayoutStyle, std::span<skb_attribute_t> attributes, int attribute_offset)
	{
		int attribute_count = attribute_offset;
		const int capacity = static_cast<int>(attributes.size());
		auto push = [&](skb_attribute_t attr)
		{
			assert(attribute_count < capacity && "style_to_attributes: layout attribute buffer overflow");
			if (attribute_count < capacity)
				attributes[attribute_count++] = attr;
		};
		push(skb_attribute_make_text_base_direction((skb_text_direction_t)textLayoutStyle->textDirection));
		push(skb_attribute_make_text_overflow(toSKBTextOverflow(textLayoutStyle->textOverflow)));
		push(skb_attribute_make_text_wrap((skb_text_wrap_t)textLayoutStyle->textWrapping));
		push(skb_attribute_make_horizontal_align((skb_align_t)textLayoutStyle->horizontalAlignment));
		push(skb_attribute_make_baseline_align((skb_baseline_t)textLayoutStyle->baseline));
		push(skb_attribute_make_vertical_trim((skb_vertical_trim_t)textLayoutStyle->verticalTrim));
		return attribute_count - attribute_offset;
	}

	int style_to_attributes(const TextStyle* textStyle, std::span<skb_attribute_t> attributes, int attribute_offset,
	                        const std::unordered_map<uint32_t, uint8_t>& fontIdToFamilyId)
	{
		int attribute_count = attribute_offset;
		const int capacity = static_cast<int>(attributes.size());
		auto push = [&](skb_attribute_t attr)
		{
			assert(attribute_count < capacity && "style_to_attributes: text attribute buffer overflow");
			if (attribute_count < capacity)
				attributes[attribute_count++] = attr;
		};

		enum Tags
		{
			LINE_THROUGH = SKB_TAG('s', 't', 'r', 'k'),
			LINE_BOTTOM = SKB_TAG('b', 't', 't', 'm'),
			LINE_UNDER = SKB_TAG('u', 'l', 'i', 'n'),
			LINE_OVER = SKB_TAG('o', 'v', 'e', 'r')
		};

		// fontId overrides generic family; fall back to family if fontId is not registered.
		const auto registered = textStyle->fontId != FontID::Invalid
			? fontIdToFamilyId.find(static_cast<uint32_t>(textStyle->fontId)) : fontIdToFamilyId.end();
		if (registered != fontIdToFamilyId.end())
			push(skb_attribute_make_font_family(registered->second));
		else if (textStyle->family != FontFamily::Default)
			push(skb_attribute_make_font_family((uint8_t)(skb_font_family_t)textStyle->family));
		if (textStyle->weight != FontWeight::Normal)
		{
			push(skb_attribute_make_font_weight((skb_weight_t)textStyle->weight));
		}
		if (textStyle->style != FontStyle::Normal)
		{
			push(skb_attribute_make_font_style((skb_style_t)textStyle->style));
		}
		if (textStyle->stretch != FontStretch::Normal)
		{
			push(skb_attribute_make_font_stretch((skb_stretch_t)textStyle->stretch));
		}
		push(skb_attribute_make_font_size(textStyle->fontSize));
		push(skb_attribute_make_paint_color(SKB_PAINT_TEXT, SKB_PAINT_STATE_DEFAULT, toSKBColor(textStyle->color)));
		push(skb_attribute_make_line_height((skb_line_height_t)textStyle->lineHeightType, textStyle->lineHeight));
		if (!textStyle->language.empty())
			push(skb_attribute_make_lang(textStyle->language.c_str()));
		if (textStyle->throughLine.thickness > 0)
		{
			push(skb_attribute_make_paint_color(LINE_THROUGH, SKB_PAINT_STATE_DEFAULT, skb_rgba(textStyle->throughLine.color.r, textStyle->throughLine.color.g, textStyle->throughLine.color.b, textStyle->throughLine.color.a)));
			push(skb_attribute_make_decoration(SKB_DECORATION_LINE_THROUGH, (skb_decoration_style_t)textStyle->throughLine.style, textStyle->throughLine.thickness, textStyle->throughLine.offset, LINE_THROUGH));
		}
		if (textStyle->bottomLine.thickness > 0)
		{
			push(skb_attribute_make_paint_color(LINE_BOTTOM, SKB_PAINT_STATE_DEFAULT, skb_rgba(textStyle->bottomLine.color.r, textStyle->bottomLine.color.g, textStyle->bottomLine.color.b, textStyle->bottomLine.color.a)));
			push(skb_attribute_make_decoration(SKB_DECORATION_LINE_BOTTOM, (skb_decoration_style_t)textStyle->bottomLine.style, textStyle->bottomLine.thickness, textStyle->bottomLine.offset, LINE_BOTTOM));
		}
		if (textStyle->underLine.thickness > 0)
		{
			push(skb_attribute_make_paint_color(LINE_UNDER, SKB_PAINT_STATE_DEFAULT, skb_rgba(textStyle->underLine.color.r, textStyle->underLine.color.g, textStyle->underLine.color.b, textStyle->underLine.color.a)));
			push(skb_attribute_make_decoration(SKB_DECORATION_LINE_UNDER, (skb_decoration_style_t)textStyle->underLine.style, textStyle->underLine.thickness, textStyle->underLine.offset, LINE_UNDER));
		}
		if (textStyle->overLine.thickness > 0)
		{
			push(skb_attribute_make_paint_color(LINE_OVER, SKB_PAINT_STATE_DEFAULT, skb_rgba(textStyle->overLine.color.r, textStyle->overLine.color.g, textStyle->overLine.color.b, textStyle->overLine.color.a)));
			push(skb_attribute_make_decoration(SKB_DECORATION_LINE_OVER, (skb_decoration_style_t)textStyle->overLine.style, textStyle->overLine.thickness, textStyle->overLine.offset, LINE_OVER));
		}
		return attribute_count - attribute_offset;
	}

	skb_editor_t* create_editor(SkribidiContext* ctx,
								float width,
								const TextLayoutStyle* textLayoutStyle,
								const TextStyle* textStyle,
								const TextStyle* selectedTextStyle,
								std::string_view text)
	{
		skb_attribute_t textAttributes[32] = {};
		int textAttributeCount = style_to_attributes(textStyle, textAttributes, ctx->PushDefaultLanguage(textAttributes), ctx->m_fontIdToFamilyId);

		skb_attribute_t compositionAttributes[32] = {};
		int compositionAttributesCount = style_to_attributes(selectedTextStyle, compositionAttributes, ctx->PushDefaultLanguage(compositionAttributes), ctx->m_fontIdToFamilyId);

		skb_attribute_t layout_attributes[32] = {};
		style_to_attributes(textLayoutStyle, layout_attributes, 0);

		skb_editor_params_t edit_params =
		{
			.font_collection = ctx->font_collection,
			.icon_collection = ctx->icon_collection,
			.editor_width = width,
			.layout_attributes = SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(layout_attributes),
			.paragraph_attributes = skb_attribute_set_t
			{
				.attributes = textAttributes,
				.attributes_count = textAttributeCount,
			},
			.composition_attributes = skb_attribute_set_t
			{
				.attributes = compositionAttributes,
				.attributes_count = compositionAttributesCount,
			},
		};

		auto editor = skb_editor_create(&edit_params);
		assert(editor);
		skb_editor_set_text_utf8(editor, ctx->temp_alloc, text.data(), int32_t(text.length()));

		return editor;
	}

	std::string build_editor_text(std::span<const ContentRun> runs, std::vector<RegisteredText::LinkRange>& outLinks)
	{
		std::string text;
		outLinks.clear();
		int32_t codepoints = 0; // editor offsets count codepoints, not UTF-8 bytes
		for (const auto& run : runs)
		{
			if (run.type != ContentRun::Text) continue; // icons aren't part of the selectable text
			const int32_t start = codepoints;
			for (char c : run.text)
				if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++codepoints;
			text.append(run.text);
			if (run.isLink && run.contentId != 0)
				outLinks.push_back({ run.contentId, start, codepoints,
				                     toSKBColor(run.linkStyle.color), toSKBColor(run.linkStyle.hoverColor) });
		}
		return text;
	}

	int32_t drop_offset_after_cut(int32_t dropOffset, int32_t selA, int32_t selB)
	{
		const int32_t lo = std::min(selA, selB);
		const int32_t hi = std::max(selA, selB);
		if (dropOffset <= lo) return dropOffset;
		if (dropOffset >= hi) return dropOffset - (hi - lo);
		return lo; // inside the selection: the text stays where it is
	}

	void capture_editor_text(SkribidiContext* ctx)
	{
		if (!ctx || !ctx->editor)
			return;

		int32_t text_len = skb_editor_get_text_utf8_count(ctx->editor);
		ctx->pending_text.clear();
		if (text_len > 0)
		{
			ctx->pending_text.resize(text_len);
			int32_t written = skb_editor_get_text_utf8(ctx->editor, ctx->pending_text.data(), text_len);
			if (written < 0)
				written = 0;
			ctx->pending_text.resize(written);
		}
		ctx->pending_text_valid = true;
	}

	// Normalizes a run of leading zeros in `s` starting at `start`.
	// "007" -> "7"  (followed by another digit)
	// "000" -> "0", "00.5" -> "0.5"  (followed by non-digit or end: keep one zero)
	static void strip_leading_zeros(std::string& s, size_t start)
	{
		size_t i = start;
		while (i < s.size() && s[i] == '0') i++;
		const size_t num_zeros = i - start;
		if (num_zeros <= 1)
			return;

		const bool followed_by_digit = (i < s.size()) && (s[i] >= '1' && s[i] <= '9');
		if (followed_by_digit)
			s.erase(start, num_zeros);
		else
			s.erase(start, num_zeros - 1);
	}

	static bool filter_float_text(std::string& text)
	{
		std::string filtered;
		filtered.reserve(text.size());

		// [sign] mantissa [e|E [sign] digits]. Partial forms ("1e", "1e-") stay, so
		// exponent notation can be typed or pasted; the value parses once complete.
		bool seen_dot        = false;
		bool seen_digit      = false;
		size_t exponent_at   = std::string::npos;  // index of 'e' in `filtered`

		for (char c : text)
		{
			if (c >= '0' && c <= '9')
			{
				filtered.push_back(c);
				seen_digit = true;
				continue;
			}

			if (c == '+' || c == '-')
			{
				if (filtered.empty() || (exponent_at != std::string::npos && filtered.size() == exponent_at + 1))
					filtered.push_back(c);
				continue;
			}

			if (c == '.')
			{
				if (!seen_dot && exponent_at == std::string::npos)
				{
					filtered.push_back(c);
					seen_dot = true;
				}
				continue;
			}

			if ((c == 'e' || c == 'E') && seen_digit && exponent_at == std::string::npos)
			{
				exponent_at = filtered.size();
				filtered.push_back(c);
				continue;
			}
		}

		{
			const size_t mantissa_start = (!filtered.empty() && (filtered[0] == '+' || filtered[0] == '-')) ? 1 : 0;
			std::string exponent;
			if (exponent_at != std::string::npos)
			{
				exponent = filtered.substr(exponent_at);
				filtered.resize(exponent_at);
			}
			strip_leading_zeros(filtered, mantissa_start);
			filtered += exponent;
		}

		if (filtered == text)
			return false;

		text = std::move(filtered);
		return true;
	}

	static bool filter_int_text(std::string& text)
	{
		std::string filtered;
		filtered.reserve(text.size());

		for (char c : text)
		{
			if (c >= '0' && c <= '9')
			{
				filtered.push_back(c);
				continue;
			}

			if ((c == '+' || c == '-') && filtered.empty())
			{
				filtered.push_back(c);
				continue;
			}
		}

		const size_t digits_start = (!filtered.empty() && (filtered[0] == '+' || filtered[0] == '-')) ? 1 : 0;
		strip_leading_zeros(filtered, digits_start);

		if (filtered == text)
			return false;

		text = std::move(filtered);
		return true;
	}

	static bool filter_uint_text(std::string& text)
	{
		std::string filtered;
		filtered.reserve(text.size());

		for (char c : text)
		{
			if (c >= '0' && c <= '9')
			{
				filtered.push_back(c);
				continue;
			}
		}

		strip_leading_zeros(filtered, 0);

		if (filtered == text)
			return false;

		text = std::move(filtered);
		return true;
	}

	std::string format_to_text(double value, bool clamp)
	{
		if (std::isinf(value))
		{
			if (value < 0)
				return "-Infinity";
			return "Infinity";					
		}
		if (std::isnan(value))
		{
			return "NaN";
		}
		if (clamp)
		{
			char buffer[128] = {};
			int written = snprintf(buffer, sizeof(buffer), "%.9g", value);
			if (written <= 0) return "0";
			return std::string(buffer, static_cast<size_t>(written));
		} else
		{
			char buffer[128] = {};
			int written = snprintf(buffer, sizeof(buffer), "%g", value);
			if (written <= 0) return "0";
			return std::string(buffer, static_cast<size_t>(written));
		}
	}

	std::string format_to_text(float value, bool clamp) { return format_to_text(static_cast<double>(value), clamp); }

	std::string format_to_text(int64_t value)
	{
		char buffer[64] = {};
		int written = snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(value));
		if (written <= 0) return "0";
		return std::string(buffer, static_cast<size_t>(written));
	}
	std::string format_to_text(int32_t value) { return format_to_text(static_cast<int64_t>(value)); }
	std::string format_to_text(int16_t value) { return format_to_text(static_cast<int64_t>(value)); }
	std::string format_to_text(int8_t  value) { return format_to_text(static_cast<int64_t>(value)); }

	std::string format_to_text(uint64_t value)
	{
		char buffer[64] = {};
		int written = snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(value));
		if (written <= 0) return "0";
		return std::string(buffer, static_cast<size_t>(written));
	}
	std::string format_to_text(uint32_t value) { return format_to_text(static_cast<uint64_t>(value)); }
	std::string format_to_text(uint16_t value) { return format_to_text(static_cast<uint64_t>(value)); }
	std::string format_to_text(uint8_t  value) { return format_to_text(static_cast<uint64_t>(value)); }

	std::string format_to_text(NumericType type, NumericValue& outValue)
	{
		switch (type)
		{
			case NumericType::TypeFloat:  return format_to_text(outValue.value_float, true);
			case NumericType::TypeDouble: return format_to_text(outValue.value_double, true);

			case NumericType::TypeUInt64: return format_to_text(outValue.value_uint64);
			case NumericType::TypeUInt32: return format_to_text(outValue.value_uint32);
			case NumericType::TypeUInt16: return format_to_text(outValue.value_uint16);
			case NumericType::TypeUInt8:  return format_to_text(outValue.value_uint8);

			case NumericType::TypeInt64: return format_to_text(outValue.value_int64);
			case NumericType::TypeInt32: return format_to_text(outValue.value_int32);
			case NumericType::TypeInt16: return format_to_text(outValue.value_int16);
			case NumericType::TypeInt8:  return format_to_text(outValue.value_int8);

			default:
				return {};
		}
	}

	std::string to_string(NumericType type, NumericValue& outValue)
	{
		switch (type)
		{
			case NumericType::TypeFloat:  return format_to_text(outValue.value_float, false);
			case NumericType::TypeDouble: return format_to_text(outValue.value_double, false);

			case NumericType::TypeUInt64: return std::to_string(outValue.value_uint64);
			case NumericType::TypeUInt32: return std::to_string(outValue.value_uint32);
			case NumericType::TypeUInt16: return std::to_string(outValue.value_uint16);
			case NumericType::TypeUInt8:  return std::to_string(outValue.value_uint8);

			case NumericType::TypeInt64:  return std::to_string(outValue.value_int64);
			case NumericType::TypeInt32:  return std::to_string(outValue.value_int32);
			case NumericType::TypeInt16:  return std::to_string(outValue.value_int16);
			case NumericType::TypeInt8:   return std::to_string(outValue.value_int8);

			default:
				return {};
		}
	}

	bool try_parse_text(std::string_view text, double& outValue)
	{
		if (text.empty()) return false;
		std::string tmp(text);
		char* end = nullptr;
		errno = 0;
		double value = strtod(tmp.c_str(), &end);
		if (end == tmp.c_str() || *end != '\0') return false;
		if (errno == ERANGE || !std::isfinite(value)) return false;
		outValue = value;
		return true;
	}

	bool try_parse_text(std::string_view text, float& outValue)
	{
		if (text.empty()) return false;
		std::string tmp(text);
		char* end = nullptr;
		errno = 0;
		float value = strtof(tmp.c_str(), &end);
		if (end == tmp.c_str() || *end != '\0') return false;
		if (errno == ERANGE || !std::isfinite(value)) return false;

		outValue = value;
		return true;
	}

	bool try_parse_text(std::string_view text, int64_t& outValue)
	{
		if (text.empty())
			return false;

		std::string tmp(text);
		char* end = nullptr;
		errno = 0;
		long long value = strtoll(tmp.c_str(), &end, 10);

		if (end == tmp.c_str() || *end != '\0')
			return false;
		if (errno == ERANGE)
			return false;

		outValue = static_cast<int64_t>(value);
		return true;
	}

	bool try_parse_text(std::string_view text, uint64_t& outValue)
	{
		if (text.empty())
			return false;

		// strtoull does not handle leading '-' the way we want for unsigned parsing,
		// so reject negative values explicitly.
		if (!text.empty() && text.front() == '-')
			return false;

		std::string tmp(text);
		char* end = nullptr;
		errno = 0;
		unsigned long long value = strtoull(tmp.c_str(), &end, 10);

		if (end == tmp.c_str() || *end != '\0')
			return false;
		if (errno == ERANGE)
			return false;

		outValue = static_cast<uint64_t>(value);
		return true;
	}

	bool try_parse_text(std::string_view text, int32_t& outValue)
	{
		int64_t value;
		if (!try_parse_text(text, value)) return false;
		if (value < INT32_MIN || value > INT32_MAX) return false;
		outValue = static_cast<int32_t>(value);
		return true;
	}

	bool try_parse_text(std::string_view text, uint32_t& outValue)
	{
		uint64_t value;
		if (!try_parse_text(text, value)) return false;
		if (value > UINT32_MAX) return false;
		outValue = static_cast<uint32_t>(value);
		return true;
	}

	bool try_parse_text(std::string_view text, int16_t& outValue)
	{
		int64_t value;
		if (!try_parse_text(text, value)) return false;
		if (value < INT16_MIN || value > INT16_MAX) return false;
		outValue = static_cast<int16_t>(value);
		return true;
	}

	bool try_parse_text(std::string_view text, uint16_t& outValue)
	{
		uint64_t value;
		if (!try_parse_text(text, value)) return false;
		if (value > UINT16_MAX) return false;
		outValue = static_cast<uint16_t>(value);
		return true;
	}

	bool try_parse_text(std::string_view text, int8_t& outValue)
	{
		int64_t value;
		if (!try_parse_text(text, value)) return false;
		if (value < INT8_MIN || value > INT8_MAX) return false;
		outValue = static_cast<int8_t>(value);
		return true;
	}

	bool try_parse_text(std::string_view text, uint8_t& outValue)
	{
		uint64_t value;
		if (!try_parse_text(text, value)) return false;
		if (value > UINT8_MAX) return false;
		outValue = static_cast<uint8_t>(value);
		return true;
	}

	bool try_parse_text(std::string_view text, NumericType type, NumericValue& outValue)
	{
		switch (type)
		{
			case NumericType::TypeFloat:  return try_parse_text(text, outValue.value_float);
			case NumericType::TypeDouble: return try_parse_text(text, outValue.value_double);

			case NumericType::TypeUInt64: return try_parse_text(text, outValue.value_uint64);
			case NumericType::TypeUInt32: return try_parse_text(text, outValue.value_uint32);
			case NumericType::TypeUInt16: return try_parse_text(text, outValue.value_uint16);
			case NumericType::TypeUInt8:  return try_parse_text(text, outValue.value_uint8);

			case NumericType::TypeInt64: return try_parse_text(text, outValue.value_int64);
			case NumericType::TypeInt32: return try_parse_text(text, outValue.value_int32);
			case NumericType::TypeInt16: return try_parse_text(text, outValue.value_int16);
			case NumericType::TypeInt8:  return try_parse_text(text, outValue.value_int8);

			default:
				outValue = {};
				return false;
		}
	}

	std::string get_editor_text_utf8(skb_editor_t* editor)
	{
		if (!editor)
			return {};

		int32_t text_len = skb_editor_get_text_utf8_count(editor);
		if (text_len <= 0)
			return {};

		std::string text;
		text.resize(text_len);
		int32_t written = skb_editor_get_text_utf8(editor, text.data(), text_len);
		if (written < 0)
			written = 0;
		text.resize(written);
		return text;
	}

	std::string get_rich_text_utf8(const skb_rich_text_t* rich_text)
	{
		if (!rich_text)
			return {};

		const int32_t codepoint_count = skb_rich_text_get_utf32_count(rich_text);
		if (codepoint_count <= 0)
			return {};

		skb_text_range_t text_range =
		{
			.start = {.offset = 0, .affinity = SKB_AFFINITY_TRAILING},
			.end = {.offset = codepoint_count, .affinity = SKB_AFFINITY_TRAILING}
		};

		int32_t utf8_count = skb_rich_text_get_utf8_count_in_range(rich_text, text_range);
		if (utf8_count <= 0)
			return {};

		std::string text;
		text.resize(utf8_count);
		int32_t written = skb_rich_text_get_utf8_in_range(rich_text, text_range, text.data(), utf8_count);
		if (written < 0)
			written = 0;
		text.resize(written);
		return text;
	}

	void set_rich_text_utf8(skb_editor_t* editor, SkribidiContext* ctx, skb_rich_text_t* rich_text, std::string_view text, skb_text_range_t text_range)
	{
		if (!rich_text || !ctx)
			return;

		skb_attribute_set_t paragraph_attributes = {};
		if (skb_rich_text_get_paragraphs_count(rich_text) > 0)
		{
			paragraph_attributes = skb_rich_text_get_paragraph_attributes(rich_text, 0);
		}
		else if (editor)
		{
			auto paragraph_pos = skb_editor_get_paragraph_position_from_text_position(editor, text_range.start);
			if (paragraph_pos.paragraph_idx >= 0)
				paragraph_attributes = skb_editor_get_paragraph_attributes(editor, paragraph_pos.paragraph_idx);
		}

		skb_rich_text_reset(rich_text);
		if (text.empty())
			return;

		skb_rich_text_append_paragraph(rich_text, paragraph_attributes);

		skb_attribute_set_t text_attributes = {};
		if (editor)
		{
			text_attributes.attributes = (skb_attribute_t*)skb_editor_get_active_attributes(editor);
			text_attributes.attributes_count = skb_editor_get_active_attributes_count(editor);
		}

		skb_rich_text_append_utf8(rich_text, ctx->temp_alloc, text.data(), int32_t(text.size()), text_attributes);
	}

	FilterFunc* get_text_filter_func(NumericType type)
	{
		if ((int(type) & int(NumericType::TypeUnsignedBit)) == int(NumericType::TypeUnsignedBit))
			return &filter_uint_text;
		if ((int(type) & int(NumericType::TypeSignedBit)) == int(NumericType::TypeSignedBit))
			return &filter_int_text;
		if ((int(type) & int(NumericType::TypeFloatBit)) == int(NumericType::TypeFloatBit))
			return &filter_float_text;
		assert(false && "Unknown numeric type");
		return nullptr;
	}

	bool filter_numeric_paste(NumericType type, std::string& text)
	{
		// The filter's return value only says whether it changed the text.
		get_text_filter_func(type)(text);
		return !text.empty();
	}

	void on_editor_input_filter(skb_editor_t* editor, skb_rich_text_t* input_text, skb_text_range_t text_range, void* context)
	{
		auto* ctx = static_cast<SkribidiContext*>(context);
		if (!ctx || !editor || !input_text)
			return;
		if (ctx->active_numeric_mode == NumericType::None)
			return;

		auto filter_func = get_text_filter_func(ctx->active_numeric_mode);

		std::string current_text = get_editor_text_utf8(editor);
		std::string inserted_text = get_rich_text_utf8(input_text);

		if (inserted_text.empty())
			return;

		skb_range_t replace_range = skb_editor_get_offset_range_from_text_range(editor, text_range);
		int32_t replace_start = replace_range.start;
		int32_t replace_end = replace_range.end;
		if (replace_start < 0) replace_start = 0;
		if (replace_end < replace_start) replace_end = replace_start;
		if (replace_start > int32_t(current_text.size())) replace_start = int32_t(current_text.size());
		if (replace_end > int32_t(current_text.size())) replace_end = int32_t(current_text.size());

		std::string candidate;
		candidate.reserve(current_text.size() + inserted_text.size());
		candidate.append(current_text.data(), size_t(replace_start));
		candidate.append(inserted_text);
		candidate.append(current_text.data() + replace_end, current_text.size() - size_t(replace_end));

		std::string filtered_candidate = candidate;
		bool candidate_changed = filter_func(filtered_candidate);
		if (!candidate_changed)
			return;

		std::string filtered_inserted = inserted_text;
		filter_func(filtered_inserted);

		if (filtered_inserted.empty())
		{
			skb_rich_text_reset(input_text);
			return;
		}

		std::string candidate_with_filtered_insert;
		candidate_with_filtered_insert.reserve(current_text.size() + filtered_inserted.size());
		candidate_with_filtered_insert.append(current_text.data(), size_t(replace_start));
		candidate_with_filtered_insert.append(filtered_inserted);
		candidate_with_filtered_insert.append(current_text.data() + replace_end, current_text.size() - size_t(replace_end));

		std::string final_validation = candidate_with_filtered_insert;
		if (filter_func(final_validation))
		{
			skb_rich_text_reset(input_text);
			return;
		}

		set_rich_text_utf8(editor, ctx, input_text, filtered_inserted, text_range);
	}

	void on_editor_text_change(skb_editor_t* /*editor*/, skb_editor_text_change_reason_t /*reason*/, void* context)
	{
		auto* ctx = static_cast<SkribidiContext*>(context);
		if (!ctx)
			return;

		// Pending state owned by another editor (or orphaned by a deactivation) is
		// stale: a carried-over textCommitted would commit this editor's first edit.
		if (ctx->pending_text_id != ctx->editing_text_id)
		{
			ctx->pending_text_committed = false;
			ctx->pending_text_valid     = false;
			ctx->pending_text.clear();
		}

		ctx->pending_text_id = ctx->editing_text_id;
		ctx->pending_text_dirty = true;
		ctx->pending_text_valid = false;
	}

	void draw_image_quad_sdf(SkribidiContext* ctx, skb_rect2_t geom, skb_rect2_t image, float scale, int bpp, skb_rasterize_alpha_mode_t mode, VisualColorStyleIndex textColorIndex, uint32_t texID, IntRect clipRect)
	{
		if (texID == 0)
		{
			LOG_ERROR(kLogText, "draw_image_quad_sdf: text_id == 0");
			return;
		}

		const float2 pos  = float2 { geom.x, geom.y };
		const float2 size = float2 { geom.width, geom.height };
		const float2 uv0  = float2 { image.x, image.y };
		const float2 uv1  = float2 { image.x + image.width , image.y + image.height };

		auto textureIndex = (TextureIndex)texID;
		ImageIndex image_index = ctx->textAtlasManager->GetImageIndex(textureIndex);

		ctx->guiRenderer->RenderGlyph(
			pos, size, uv0, uv1, scale, bpp, (int)mode, textColorIndex, clipRect, image_index);
	}

	void draw_image_pattern_quad_sdf(SkribidiContext* ctx, skb_rect2_t geom, skb_rect2_t pattern, skb_rect2_t image, float scale, int bpp, skb_rasterize_alpha_mode_t mode, VisualColorStyleIndex textColorIndex, uint32_t texID, IntRect clipRect)
	{
		// Tiles the "imageData" part of the texture by slicing the quad into pieces,
		// which keeps the glyph shader free of pattern mapping.

		//
		// sx  [==================] geom
		//  +-----+-----+-----+-----+  repeat_x
		//     |--+-----+-----+---|	(ux,uwidth)
		//        [====] imageData

		// Wrap the offset so that it is always between -1..0
		const float mx = fmodf(pattern.x, 1.f);
		const float sx = mx > 0.f ? mx - 1.f : mx;
		const float my = fmodf(pattern.y, 1.f);
		const float sy = my > 0.f ? my - 1.f : my;

		const int32_t repeat_x = (int32_t)ceilf(skb_absf(sx) + pattern.width);
		const int32_t repeat_y = (int32_t)ceilf(skb_absf(sy) + pattern.height);

		// Size of the imageData to repeat.
		const float unit_pat_width = geom.width / pattern.width;
		const float unit_pat_height = geom.height / pattern.height;

		for (int32_t y = 0; y < repeat_y; y++)
		{
			const float cy = sy + (float)y;
			const float uy = skb_maxf(0.f, cy);
			const float uheight = skb_minf(cy + 1.f, pattern.height) - uy;

			for (int32_t x = 0; x < repeat_x; x++)
			{
				// Start position in normalized coordinates
				const float cx = sx + (float)x;
				// Position and size of the current tile on normalized coordinates.
				const float ux = skb_maxf(0.f, cx);
				const float uwidth = skb_minf(cx + 1.f, pattern.width) - ux;

				// Quad to draw
				skb_rect2_t sub_geom =
				{
					.x = geom.x + ux * unit_pat_width,
					.y = geom.y + uy * unit_pat_height,
					.width = uwidth * unit_pat_width,
					.height = uheight * unit_pat_height,
				};

				// Portion of imageData to map to the quad.
				// The first tile of row or col may be truncated from the start edge, otherwise we always truncate from end.
				skb_rect2_t sub_image =
				{
					.x = image.x + skb_maxf(0.f, -cx) * image.width,
					.y = image.y + skb_maxf(0.f, -cy) * image.height,
					.width = uwidth * image.width,
					.height = uheight * image.height,
				};

				draw_image_quad_sdf(ctx, sub_geom, sub_image, scale, bpp, mode, textColorIndex, texID, clipRect);
			}
		}
	}

	// ---- Atlas lookups + retained-render-cache recording ----------------------

	namespace
	{
		AtlasTouch::Key MakeTouchKey(AtlasTouch::Kind kind, const void* collection, uint32_t handle,
		                             uint32_t glyphOrPos, float a, float b, float pixelScale,
		                             skb_rasterize_alpha_mode_t mode)
		{
			AtlasTouch::Key key;
			std::memset(&key, 0, sizeof(key)); // padding takes part in the hash
			key.collection = collection;
			key.handle     = handle;
			key.glyphOrPos = glyphOrPos;
			key.a          = a;
			key.b          = b;
			key.pixelScale = pixelScale;
			key.kind       = kind;
			key.alphaMode  = static_cast<uint8_t>(mode);
			return key;
		}

		ImageIndex AtlasImageIndex(SkribidiContext* ctx, uint8_t textureIdx)
		{
			const uint32_t texID = (uint32_t)skb_image_atlas_get_texture_user_data(ctx->atlas, textureIdx);
			return texID == 0 ? ImageIndex::Invalid
			                  : ctx->textAtlasManager->GetImageIndex((TextureIndex)texID);
		}

		skb_quad_t QueryTouch(SkribidiContext* ctx, const AtlasTouch::Key& k)
		{
			const auto mode = static_cast<skb_rasterize_alpha_mode_t>(k.alphaMode);
			const skb_color_t white = skb_rgba(255, 255, 255, 255);
			switch (k.kind)
			{
				case AtlasTouch::Kind::Glyph:
					return skb_image_atlas_get_glyph_quad(ctx->atlas, 0.f, 0.f, k.pixelScale,
						(skb_font_collection_t*)k.collection, k.handle, k.glyphOrPos, k.a, white, mode);
				case AtlasTouch::Kind::Icon:
					return skb_image_atlas_get_icon_quad(ctx->atlas, 0.f, 0.f, k.pixelScale,
						(const skb_icon_collection_t*)k.collection, k.handle, k.a, k.b, white, mode);
				case AtlasTouch::Kind::Decoration:
					return skb_image_atlas_get_decoration_quad(ctx->atlas, 0.f, 0.f, k.pixelScale,
						(skb_decoration_position_t)k.glyphOrPos, (skb_decoration_style_t)k.handle,
						1.f, 0.f, k.a, white, mode);
			}
			return skb_quad_t{};
		}

		void RecordTouch(SkribidiContext* ctx, const AtlasTouch::Key& key, const skb_quad_t& quad)
		{
			if (!ctx->atlasRecorder) return;
			if (!ctx->atlasRecorderKeys.insert(Hash::CalculateBytes(&key, sizeof(key))).second) return;
			ctx->atlasRecorder->push_back(AtlasTouch{ key, quad.texture, quad.texture_idx,
			                                          AtlasImageIndex(ctx, quad.texture_idx) });
		}
	}

	skb_quad_t SkribidiContext::GetGlyphQuad(float x, float y, float pixelScale, skb_font_collection_t* fontCollection,
	                                         skb_font_handle_t font, uint32_t glyphId, float fontSize,
	                                         skb_color_t tint, skb_rasterize_alpha_mode_t mode)
	{
		const skb_quad_t quad = skb_image_atlas_get_glyph_quad(atlas, x, y, pixelScale,
			fontCollection, font, glyphId, fontSize, tint, mode);
		if (atlasRecorder)
			RecordTouch(this, MakeTouchKey(AtlasTouch::Kind::Glyph, fontCollection, font, glyphId,
			                               fontSize, 0.f, pixelScale, mode), quad);
		return quad;
	}

	float SkribidiContext::Px(float cssPx) const
	{
		return styleManager ? styleManager->Px(cssPx) : cssPx;
	}

	skb_quad_t SkribidiContext::GetIconQuad(float x, float y, float pixelScale, const skb_icon_collection_t* iconCollection,
	                                        skb_icon_handle_t icon, float width, float height,
	                                        skb_color_t tint, skb_rasterize_alpha_mode_t mode)
	{
		const skb_quad_t quad = skb_image_atlas_get_icon_quad(atlas, x, y, pixelScale,
			iconCollection, icon, width, height, tint, mode);
		if (atlasRecorder)
			RecordTouch(this, MakeTouchKey(AtlasTouch::Kind::Icon, iconCollection, icon, 0,
			                               width, height, pixelScale, mode), quad);
		return quad;
	}

	skb_quad_t SkribidiContext::GetDecorationQuad(float x, float y, float pixelScale, skb_decoration_position_t position,
	                                              skb_decoration_style_t style, float length, float patternOffset, float thickness,
	                                              skb_color_t tint, skb_rasterize_alpha_mode_t mode)
	{
		const skb_quad_t quad = skb_image_atlas_get_decoration_quad(atlas, x, y, pixelScale,
			position, style, length, patternOffset, thickness, tint, mode);
		if (atlasRecorder)
			RecordTouch(this, MakeTouchKey(AtlasTouch::Kind::Decoration, nullptr, (uint32_t)style, (uint32_t)position,
			                               thickness, 0.f, pixelScale, mode), quad);
		return quad;
	}

	void SkribidiContext::BeginAtlasRecord(ID scopeId)
	{
		auto& touches = atlasRecords[scopeId];
		touches.clear();
		atlasRecorder = &touches;
		atlasRecorderKeys.clear();
	}

	void SkribidiContext::EndAtlasRecord()
	{
		atlasRecorder = nullptr;
		atlasRecorderKeys.clear();
	}

	bool SkribidiContext::ReplayAtlasTouches(ID scopeId)
	{
		auto it = atlasRecords.find(scopeId);
		if (it == atlasRecords.end()) return false;
		bool intact = true;
		for (const AtlasTouch& t : it->second)
		{
			const skb_quad_t quad = QueryTouch(this, t.key);
			if (quad.texture_idx != t.textureIdx
				|| quad.texture.x != t.texture.x || quad.texture.y != t.texture.y
				|| quad.texture.width != t.texture.width || quad.texture.height != t.texture.height
				|| AtlasImageIndex(this, quad.texture_idx) != t.imageIndex)
				intact = false;
		}
		return intact;
	}

} // namespace GUI