#include <gui_core/text/skribidi/gui_text_system_skribidi.h>
#include <gui_core/text/skribidi/internal/gui_default_fonts_skribidi.h>

#include <skribidi/skb_editor.h>
#include <skribidi/skb_image_atlas.h>

#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace tachyon
{
    // An editor's whole text, as UTF-8.
    inline std::string EditorText(const skb_editor_t* editor)
    {
        std::string text(size_t(std::max(0, skb_editor_get_text_utf8_count(editor))), '\0');
        if (!text.empty())
            text.resize(size_t(std::max(0, skb_editor_get_text_utf8(editor, text.data(), int32_t(text.size())))));
        return text;
    }

    // A selection as codepoint offsets, anchor to caret (equal: just a caret).
    struct EditSelection
    {
        int32_t anchor = 0;
        int32_t caret  = 0;
    };

    inline EditSelection EditorSelection(const skb_editor_t* editor)
    {
        const skb_text_range_t range = skb_editor_get_current_selection(editor);
        return { skb_editor_get_text_offset_from_text_position(editor, range.start),
                 skb_editor_get_text_offset_from_text_position(editor, range.end) };
    }

	class GUIRenderer;
	class StyleManager;
	class IClipboard;
	class TextAtlasManager;
	class GUIStateManager;

	struct view_t
	{
		float cx;
		float cy;
		float scale;
		float zoom_level;
		float viewport_width;
		float viewport_height;

		// Drag state
		float drag_start_mx;
		float drag_start_my;
		float drag_start_vx;
		float drag_start_vy;
	};

	struct RegisteredText
	{
		static constexpr int kMaxTextAttributeCount = 32;
		static constexpr int kMaxLayoutAttributeCount = 32;

		// Frame this entry was last registered in; unused entries are evicted.
		uint32_t lastUsedFrame = 0;

		ID           elementId;
		TextID       textId;
		IconID	     iconId;
		std::string  text;
		StyleRef     styleIndex;
		ElementState state;

		int32_t	  lineHeight;
		uint16_t  iconTextGap;
		uint16_t  iconSize;
		int16_t   iconOffsetY;
		IconSide  iconSide;
		TextAlign horizontalAlignment;
		TextAlign verticalAlignment;
		TextWrap  textWrapping = TextWrap::NoWrap;
		VisualColorStyleIndex textColorIndex = VisualColorStyleIndex::Invalid;
		VisualColorStyleIndex iconColorIndex = VisualColorStyleIndex::Invalid;

		skb_attribute_t		text_attributes[kMaxTextAttributeCount] {};
		int32_t				text_attribute_count = 0;
		skb_attribute_t		layout_attributes[kMaxLayoutAttributeCount] {};
		int32_t				layout_attribute_count = 0;

		bool	            base_measurement_valid = false;	
		TextMeasurement		baseMeasurement {};

		float				wrap_width = 0;
		skb_layout_t*		wrap_layout = nullptr;
		TextMeasurement		wrapMeasurement {};

		// For rich text with icons
		bool isRichText = false;
		std::vector<ContentRun>		   inputRuns;
		// Owned copies of the runs' text and style overrides (the caller's only live for a frame).
		std::string					   runText;
		std::vector<TextStyle>		   runStyleOverrides;
		std::vector<skb_content_run_t> contentRuns;

		// Per-link-run attribute storage (parallel to contentRuns, populated by runs_to_skribidi)
		struct PerRunLinkAttributes
		{
			static constexpr int kMaxCount = 40; // kMaxTextAttributeCount + link-specific overhead
			skb_attribute_t data[kMaxCount];
			int32_t count = 0;
		};
		std::vector<PerRunLinkAttributes> perRunLinkAttrStorage;

		// Link runs as codepoint ranges into `text` (the editor's text), for the selectable-rich
		// editor: drives link styling applied to the editor and hover/color in RenderEditor.
		// Populated by RegisterEditableRichText; empty for non-rich / non-editable text.
		struct LinkRange { intptr_t contentId; int32_t start; int32_t end; skb_color_t color; skb_color_t hoverColor; };
		std::vector<LinkRange> linkRanges;

		// Content ID currently being hovered (0 = none); drives hover paint state in RenderText
		intptr_t hoveredContentId = 0;

		// Offset from element outer-rect local space to content-rect local space.
		// Computed from layout style (margin + border + padding) and used by HitTestContent.
		int2 renderOrigin = {};

		// Inner rect (absolute layout space) and the screen-to-layout mapping (inverse
		// of any ancestor zoom/pan), set during rendering. Used by the text editor
		// system for screen-to-local coordinate conversion.
		IntRect  innerRect = {};
		float3x2 screenToLayout = float3x2::Identity();
		// Vertical alignment offset of the edited text inside innerRect (set when drawn).
		int32_t  editorOffsetY = 0;

		// Top-left of the edited text (absolute layout space): editor-local coordinates start here.
		float2 EditorOrigin() const { return float2{ float(innerRect.x), float(innerRect.y + editorOffsetY) }; }

		void CopyRunStorage()
		{
			runText.clear();
			runStyleOverrides.clear();
			for (const auto& run : inputRuns)
			{
				runText += run.text;
				if (run.textStyleOverride) runStyleOverrides.push_back(*run.textStyleOverride);
			}
		}

		// Re-points inputRuns into the owned copies; the entry may have moved since they were made.
		void BindRunStorage()
		{
			size_t offset = 0, overrideIndex = 0;
			for (auto& run : inputRuns)
			{
				run.text = std::string_view(runText).substr(offset, run.text.size());
				offset  += run.text.size();
				if (run.textStyleOverride) run.textStyleOverride = &runStyleOverrides[overrideIndex++];
			}
		}

		uint64_t GetHashCode() const
		{
			return GetHashCode(elementId, iconId, text);
		}

		static uint64_t GetHashCode(ID elementId, IconID iconID, std::string_view text)
		{
			return Hash::Combine(Hash::Calculate(elementId), Hash::Calculate(iconID), Hash::Calculate(text));
		}

		static uint64_t GetHashCode(ID elementId, const std::span<ContentRun>& contentRuns)
		{
            uint64_t hash = Hash::Calculate(elementId);
            for (size_t i = 0; i < contentRuns.size(); ++i)
			{
				hash = Hash::Combine(hash, contentRuns[i].GetHashCode());
			}
			return hash;
		}
	};

	// One deduplicated atlas lookup made while a cached scope was captured, plus
	// where it landed. Re-querying it on replay keeps the entry from being evicted.
	struct AtlasTouch
	{
		enum class Kind : uint8_t { Glyph, Icon, Decoration };
		struct Key
		{
			const void* collection;   // font / icon collection
			uint32_t    handle;       // font / icon handle, or decoration style
			uint32_t    glyphOrPos;   // glyph id, or decoration position
			float       a, b;         // font size | icon width, height | decoration thickness
			float       pixelScale;
			Kind        kind;
			uint8_t     alphaMode;
		};
		Key         key;
		skb_rect2_t texture;
		uint8_t     textureIdx;
		ImageIndex  imageIndex;
	};

	class SkribidiContext
	{
	public:
		SkribidiContext() = default;
		inline ~SkribidiContext() { Shutdown(); }

		// Initialization
		bool Initialize(GraphicsContext* graphicsContext, StyleManager* styleManager,
		                TextAtlasManager* textAtlasManager,
		                GUIStateManager* guiStateManager, IClipboard* clipboard);
		void Shutdown();

		RegisteredText* GetRegisteredText(TextID textId)
		{
			uint32_t index = ((uint32_t)textId) - 1;
			if (index >= m_registry.size()) return nullptr;
			RegisteredText& entry = m_registry[index];
			return (entry.textId == TextID::Invalid) ? nullptr : &entry;   // free slot
		}

		void ResetLayout(TextID textID);
		void ResetLayout(RegisteredText* entry);
		skb_layout_t* CreateLayout(RegisteredText* entry, float layoutWidth, float layoutHeight) const;

		// TextID = slot index + 1. Evicted entries leave a free slot (textId
		// Invalid) that the next registration reuses.
		std::vector<RegisteredText> m_registry;
		std::vector<uint32_t>       m_freeTextSlots;
		uint32_t                    frameNumber = 0;

		// Store a new entry in a free slot (or a new one) and return its TextID.
		TextID AddRegisteredText(RegisteredText&& entry)
		{
			uint32_t index;
			if (!m_freeTextSlots.empty()) { index = m_freeTextSlots.back(); m_freeTextSlots.pop_back(); }
			else                          { index = uint32_t(m_registry.size()); m_registry.emplace_back(); }
			entry.textId        = TextID{ index + 1 };
			entry.lastUsedFrame = frameNumber;
			m_registry[index]   = std::move(entry);
			return m_registry[index].textId;
		}
		std::vector<skb_icon_handle_t> iconHandles;  // skb handle per IconID, at index IconID - 1
		std::vector<size_t>            iconSources;  // hash of each icon's SVG, to tell a repeat from a clash

		// Returns 0 for an invalid or unknown icon
		skb_icon_handle_t GetIconHandle(IconID iconId) const
		{
			const uint32_t index = (uint32_t)iconId;
			return (index == 0 || index > iconHandles.size()) ? 0 : iconHandles[index - 1];
		}

		// Font registration: maps FontID (raw uint32) to skribidi family ID used for font selection.
		std::unordered_map<uint32_t, uint8_t> m_fontIdToFamilyId;
		std::unordered_map<uint32_t, std::vector<StoredFontFace>> m_fontFamilyFaces; // the faces each FontID was registered with
		uint8_t					m_nextFamilyId  = 6; // Built-in FontFamily values occupy 0-5
		FontFallbackState       m_fontFallback;
		std::string             m_defaultLanguage; // BCP 47; text without a language of its own uses it

		// Writes the default language as the first attribute (a style's own language,
		// pushed later, wins). Returns the number written: 0 or 1.
		int PushDefaultLanguage(std::span<skb_attribute_t> attributes) const
		{
			if (m_defaultLanguage.empty() || attributes.empty())
				return 0;
			attributes[0] = skb_attribute_make_lang(m_defaultLanguage.c_str());
			return 1;
		}

		VisualColorStyleIndex   caretColorStyleIndex = VisualColorStyleIndex::Invalid;
		// Registers the caret's color; again after the style manager drops its colors (a theme reload).
		void RegisterCaretColor();

		GUIRenderer*			guiRenderer      = nullptr;
		StyleManager*			styleManager     = nullptr;
		// A size in CSS pixels at the UI scale (StyleManager::Px).
		float Px(float cssPx) const;
		IClipboard*				clipboard        = nullptr;
		TextAtlasManager*		textAtlasManager = nullptr;
		GUIStateManager*		guiStateManager  = nullptr;
		skb_font_collection_t*	font_collection = nullptr;
		skb_temp_alloc_t*		temp_alloc = nullptr;
		skb_image_atlas_t*		atlas = nullptr;
        bool	                atlas_dirty = false;
		skb_rasterizer_t*		rasterizer = nullptr;
		skb_icon_collection_t*  icon_collection = nullptr;
		uint64_t				rich_text_clipboard_hash = 0;
		skb_rich_text_t*		rich_text_clipboard = nullptr;
		float					editing_text_width = 0;
		TextID                  editing_text_id = TextID::Invalid;
		TextID                  active_text_id = TextID::Invalid;
		TextID                  pending_select_all_id = TextID::Invalid;
		// An undo / redo's selection, for the field's editor once it has the text.
		ID                      pending_selection_element = ID::Invalid();
		EditSelection           pending_selection;
		// The edited field's selection just before its first change since the last commit, and
		// after its latest change: what an undo / redo of that edit gives back.
		ID                      edit_selection_element = ID::Invalid();
		EditSelection           edit_selection_before;
		EditSelection           edit_selection_after;
		TextID                  pending_text_prev_id = TextID::Invalid;
		TextID                  pending_text_id = TextID::Invalid;
		bool                    pending_text_dirty = false;
		bool                    pending_text_committed = false;
		bool                    pending_text_valid = false;
		std::string             pending_text;
		skb_editor_t*			editor = nullptr;
		bool					editor_drag_active = false;
		const ITextValidator*	active_validator = nullptr;   // the focused field's (none: any text)
		TextEditBehavior		text_edit_behavior = TextEditBehavior::None;
		bool                    active_is_multiline = false;
		bool                    active_continuous_update = false;
		bool                    active_accepts_tabs = false;
		bool                    active_read_only = false;
		bool                    composing = false; // the editor shows IME composition text

		view_t view {};

		// Atlas lookups. While a scope is being recorded, each lookup is also noted
		// so ReplayAtlasTouches can keep that scope's entries alive.
		skb_quad_t GetGlyphQuad(float x, float y, float pixelScale, skb_font_collection_t* fontCollection,
		                        skb_font_handle_t font, uint32_t glyphId, float fontSize,
		                        skb_color_t tint, skb_rasterize_alpha_mode_t mode);
		skb_quad_t GetIconQuad(float x, float y, float pixelScale, const skb_icon_collection_t* iconCollection,
		                       skb_icon_handle_t icon, float width, float height,
		                       skb_color_t tint, skb_rasterize_alpha_mode_t mode);
		skb_quad_t GetDecorationQuad(float x, float y, float pixelScale, skb_decoration_position_t position,
		                             skb_decoration_style_t style, float length, float patternOffset, float thickness,
		                             skb_color_t tint, skb_rasterize_alpha_mode_t mode);

		void BeginAtlasRecord(ID scopeId);
		void EndAtlasRecord();
		// False when an entry was evicted or moved (its scope must re-render).
		bool ReplayAtlasTouches(ID scopeId);

		std::unordered_map<ID, std::vector<AtlasTouch>> atlasRecords;
		std::vector<AtlasTouch>*     atlasRecorder = nullptr;
		std::unordered_set<uint64_t> atlasRecorderKeys;

		// Per-frame OS snapshot, written by BeginFrame.
		int2         frameMousePos     {};
		MouseButtons frameMouseButtons {};
		uint32_t     frameTimeMs       = 0;

		float atlas_scale = 0.0f;
		bool allow_char = false;
		bool show_glyph_bounds = false;
		bool show_glyph_details = false;
		bool show_caret_details = false;
		bool initialized = false;
		bool drag_view = false;
		bool drag_text = false;
	};



	void OnCreateTexture(skb_image_atlas_t* atlas, uint8_t texture_idx, void* ctx_ptr);

	skb_color_t toSKBColor(const color4b& color);

	skb_text_overflow_t toSKBTextOverflow(TextOverflow overflow);

	// fontIdToFamilyId resolves TextStyle::fontId (SkribidiContext::m_fontIdToFamilyId).
	int style_to_attributes(const TextStyle* textStyle, std::span<skb_attribute_t> attributes, int attribute_offset,
	                        const std::unordered_map<uint32_t, uint8_t>& fontIdToFamilyId);
	int style_to_attributes(const TextLayoutStyle* textLayoutStyle, std::span<skb_attribute_t> attributes, int attribute_offset);

	void runs_to_skribidi(const SkribidiContext& context, const std::span<ContentRun>& contentRuns, skb_icon_collection_t* iconCollection,
	                      skb_attribute_set_t attributeSet,
	                      std::vector<skb_content_run_t>& skbRuns,
	                      std::vector<RegisteredText::PerRunLinkAttributes>& linkAttrStorage,
	                      const std::unordered_map<uint32_t, uint8_t>& fontIdToFamilyId);

	class TextRun
	{
	public:
		const char* const text;
		const TextStyle* style;
	};

	skb_rect2_t view_transform_rect(view_t* view, skb_rect2_t geom);

	skb_editor_t* create_editor(SkribidiContext* ctx,
								float width,
								const TextLayoutStyle* textLayoutStyle,
								const TextStyle* textStyle,
								const TextStyle* selectedTextStyle,
								std::string_view text);

	void capture_editor_text(SkribidiContext* ctx);

	// Moving the selection [selA, selB) (either order) to `dropOffset` in the same
	// text: where the moved text is reinserted once the selection is cut.
	int32_t drop_offset_after_cut(int32_t dropOffset, int32_t selA, int32_t selB);

	// Selectable rich text: the editor's text (the runs' text; icons excluded) and
	// each link run's range in editor text offsets.
	std::string build_editor_text(std::span<const ContentRun> runs, std::vector<RegisteredText::LinkRange>& outLinks);

	std::string get_editor_text_utf8(skb_editor_t* editor);
	std::string get_rich_text_utf8(const skb_rich_text_t* rich_text);

	void set_rich_text_utf8(skb_editor_t* editor, SkribidiContext* ctx, skb_rich_text_t* rich_text, std::string_view text, skb_text_range_t text_range);

	void on_editor_input_filter(skb_editor_t* editor, skb_rich_text_t* input_text, skb_text_range_t text_range, void* context);

	void on_editor_text_change(skb_editor_t* /*editor*/, skb_editor_text_change_reason_t /*reason*/, void* context);

	void draw_image_quad_sdf(SkribidiContext* ctx, skb_rect2_t geom, skb_rect2_t image, float scale, int bpp, skb_rasterize_alpha_mode_t mode, VisualColorStyleIndex textColorIndex, uint32_t texID, IntRect clipRect);

	void draw_image_pattern_quad_sdf(SkribidiContext* ctx, skb_rect2_t geom, skb_rect2_t pattern, skb_rect2_t image, float scale, int bpp, skb_rasterize_alpha_mode_t mode, VisualColorStyleIndex textColorIndex, uint32_t texID, IntRect clipRect);

};