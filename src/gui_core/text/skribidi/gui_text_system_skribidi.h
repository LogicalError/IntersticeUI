#pragma once

#include <gui_core/input/gui_platform_input_handler.h>
#include <gui_core/text/gui_text_system.h>
#include <gui_core/gui_hash.h>

#include <span>
#include <unordered_map>
#include <unordered_set>
#include <string>

struct skb_layout_t;
struct skb_text_position_t;
struct skb_editor_t;

namespace GUI
{
    // Implementation detail, private to the skribidi backend.
    class SkribidiContext;
    struct RegisteredText;

    /// Skribidi-based implementation of ITextSystem.
    class SkribidiTextSystem : public ITextSystem
    {
    public:
        SkribidiTextSystem();
        ~SkribidiTextSystem() override;

        // -------------------- Initialization --------------------

        bool Initialize(GraphicsContext* graphicsContext, StyleManager* styleManager,
                        TextAtlasManager* textAtlasManager,
                        GUIStateManager* guiStateManager, IClipboard* clipboard) override;
        void Shutdown() override;
        void Update() override;
        void BeginFrame(int2 screenMousePos, MouseButtons mouseButtons, uint32_t timeMs) override;
        void BeginAtlasRecord(ID scopeId) override;
        void EndAtlasRecord() override;
        bool ReplayAtlasTouches(ID scopeId) override;

        // -------------------- Editor Input Handling --------------------

        void OnKeyboard(ID windowId, KeyCode key, int repeat, KeyModifier modifiers, KeyAction action) override;
        void OnMouseMove(ID windowId, int2 screenPos, MouseButtons button, KeyModifier modifiers) override;
        void OnMouseButton(ID windowId, int2 screenPos, MouseButtons button, KeyModifier modifiers, MouseAction action) override;
        void OnMouseScroll(ID windowId, int2 screenPos, float2 scrollOffset, KeyModifier modifiers) override;
        void OnTextComposition(ID windowId, std::string_view text, int32_t cursor) override;

        void SetActiveTextEditor(ID elementId) override;
        bool DoesTextEditorAcceptTabs(ID elementId) const override;
        bool IsTextEditor(ID elementId) const override;
        void RequestSelectAll(ID elementId) override;
        TextEditUpdate ConsumeTextEditUpdate(TextID textId, std::string& outText) override;
        bool UpdateText(TextID textId, std::string_view text) override;

        std::string Copy() override;
        std::string Cut() override;
        void Paste(std::string_view text) override;

        bool IsHoveringSelectedText(ID textEditorId, ID windowId, GUI::int2 screenSpace) const override;
        bool TryGetTextDragCursor(ID windowId, GUI::int2 screenSpace, MouseCursor& outCursor) const override;
        bool TryGetTextDragHoverTarget(ID windowId, GUI::int2 screenSpace, ID& outTargetId) const override;

        // -------------------- Cache Management --------------------

        void ClearCache() override;
        // Diagnostics: registered texts currently held (live entries).
        size_t RegisteredTextCount() const;



        // -------------------- Cursor Visibility --------------------

        void EnsureCursorVisible(ID textEditorId) override;
        std::optional<IntRect> GetCursorRect(ID textEditorId) override;
        std::optional<IntRect> GetCaretScreenRect() const override;

        // -------------------- Editor Registration --------------------


        TextID RegisterEditableText(ID elementId, std::string& text, TextEditBehavior behavior, StyleRef styleIndex, ElementState state, bool& wasModified) override;
        TextID RegisterEditableNumber(ID elementId, NumericType type, NumericValue& value, TextEditBehavior behavior, StyleRef styleIndex, ElementState state, bool& wasModified) override;


        // -------------------- Font Registration --------------------

        void SetFontDirectory(std::string_view directory) override;
        void SetLanguage(std::string_view languageTag) override;
        FontID RegisterFontFamily(std::string_view familyName, std::span<const std::string_view> faceFiles) override;

        // -------------------- Text Registration --------------------

        TextID RegisterText(ID elementId, IconID iconId, std::string_view text, StyleRef styleIndex, ElementState state) override;
        TextID RegisterRichText(ID elementId, const std::span<ContentRun>& contentRuns,
                                StyleRef styleIndex, ElementState state) override;
        TextID RegisterEditableRichText(ID elementId, const std::span<ContentRun>& contentRuns,
                                        TextEditBehavior behavior, StyleRef styleIndex,
                                        ElementState state, bool& wasModified) override;
        TextID GetTextID(ID elementId) const override;
        std::string_view GetText(TextID textId) const override;

        // -------------------- Text Measurement --------------------

        TextMeasurement GetTextMeasurement(TextID textId) const override;
        TextMeasurement MeasureText(TextID textId, const TextWrapContext& wrapContext) const override;

        // -------------------- Text Rendering --------------------

        void RenderText(TextID textId, IntRect rect, IntRect innerRect, IntRect clipRect, float pixelScale = 1.f,
                        const float3x2& screenToLayout = float3x2::Identity()) override;

        bool HitTestClickable(ID elementId, int32_t localX, int32_t localY) const override;
        intptr_t HitTestContent(TextID textId, int32_t localX, int32_t localY) const override;
        void SetHoveredContent(TextID textId, intptr_t contentId) override;


        // -------------------- Icon Registration --------------------

        /// Check if an icon exists
        bool IsValidIcon(IconID iconId) const override;

        /// Register icon for a specific element ID
        IconID RegisterIconFromSVG(const char* name, const char* svg_data, size_t svg_size) override;

        /// Load an SVG file from disk and register it as an icon.
        IconID RegisterIconFromSVGFile(const char* path) override;

        // -------------------- Icon Measurement --------------------

        IconMeasurement GetIconMeasurement(IconID iconId) const override;
        IconMeasurement MeasureIcon(IconID iconId) const override;

        // -------------------- Icon Rendering --------------------

        void RenderIcon(IconID iconId, IntRect rect, IntRect clipRect, VisualColorStyleIndex tint) override;

        static void ScreenToTextEditorLocalPoint(GUI::int2 screenSpace, const IntRect& textEditorInnerRect, GUI::float2& outLocal,
                                                 const float3x2& screenToLayout = float3x2::Identity());

    private:
        void UpdateGlyphs();
        // Recreates the font collection from the registered families, freeing replaced
        // fonts. Returns whether family `fontId` loaded.
        bool RebuildFontCollection(FontID fontId);
        void EvictUnusedTexts();

        bool TryGetCaretPositionInternal(ID windowId, GUI::int2 screenPos, ID targetTextEditorId, skb_editor_t*& outTargetEditor, skb_text_position_t& outPosition);
        void DropDraggedText(ID windowId, GUI::int2 screenPos, ID targetTextEditorId);
        void EndTextDragging();

        void RenderText(RegisteredText* entry, IntRect rect, IntRect innerRectScreen, IntRect clipRect, float pixelScale);
        void RenderEditor(RegisteredText* entry, IntRect rect, IntRect innerRectScreen, IntRect clipRect, float pixelScale);
        bool UpdateText(RegisteredText* entry, std::string_view text);

        bool ApplyTextEditUpdate(ID elementId, std::string& currentText, bool& wasModified);
        void ClearComposition();
        bool TryGetActiveTextEditorLocalPoint(ID windowId, GUI::int2 screenSpace, GUI::float2& outLocal) const;
        // True if (localX, localY) in the active editor falls within a link run (uses the active
        // entry's linkRanges). Lets read-only editors skip caret placement on a link press.
        bool IsActiveEditorPosOnLink(float localX, float localY) const;
        bool TryGetTextEditorLocalPoint(ID textEditorId, ID windowId, GUI::int2 screenSpace, GUI::float2& outLocal) const;
        ID FindTextEditorAtScreenPoint(ID windowId, GUI::int2 screenSpace) const;
        bool IsNumericTextEditor(ID elementId) const;
        bool CanDropDraggedTextInto(ID targetTextEditorId) const;
        void* UpdateLayoutWhenNecessary(RegisteredText* entry, float width) const;
        void* UpdateEditorWhenNecessary(RegisteredText* entry, float width) const;
        TextMeasurement UpdateBaseMeasurementWhenNecessary(RegisteredText* entry) const;
        TextMeasurement UpdateWrapMeasurementWhenNecessary(RegisteredText* entry, const TextWrapContext& wrapContext) const;
        TextMeasurement MeasureTextInternal(RegisteredText* entry, const skb_layout_t* layout) const;

        void UpdateRegisteredTextStyleState(RegisteredText* entry, GUI::StyleRef styleIndex, GUI::ElementState state);

        std::unordered_map<uint64_t, TextID> m_elementIdToTextIdMap;
        std::unordered_map<uint64_t, TextID> m_textKeyToIdMap;
        std::unordered_set<uint64_t> m_textEditorElements; // element IDs that are text editors
        std::unordered_set<uint64_t> m_readOnlyEditorElements; // Subset of the above flagged TextEditBehavior::ReadOnly

        std::unordered_map<uint64_t, NumericType> m_textEditorNumericModes;

        ID m_textDragSourceId = ID::Invalid();
        std::string m_textDragPayload;
        ID m_skipNextSelectAllId = ID::Invalid();
        bool m_textDragArmed = false;
        int2 m_textDragStartScreenPos = int2 { 0, 0 };
        ID m_numericEditElementId = ID::Invalid();
        NumericType m_numericEditMode = NumericType::None;
        std::string m_numericEditText;
        std::string m_numericParsedTextCache; 
        NumericValue m_numericParsedValueCache{};
        bool m_numericHasParsedCache = false;

        struct NumericDisplayCacheEntry
        {
            NumericType  type = NumericType::None;
            NumericValue value{};
            std::string  text;
            bool         isInitialized = false;
        };
        std::unordered_map<uint64_t, NumericDisplayCacheEntry> m_numericDisplayCache;

        ID m_pendingCursorVisibleEditorId = ID::Invalid();

        SkribidiContext* m_skribidiContext;
        std::string      m_fontDirectory { "fonts/Noto-otf" };
    };

} // namespace GUI
