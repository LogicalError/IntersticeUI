#pragma once

#include <gui_core/input/gui_platform_input_handler.h>
#include <gui_core/input/gui_undo_history.h>
#include <gui_core/text/gui_text_system.h>
#include <gui_core/gui_hash.h>

#include <span>
#include <unordered_map>
#include <unordered_set>
#include <string>

struct skb_layout_t;
struct skb_text_position_t;
struct skb_editor_t;

namespace tachyon
{
    /// @addtogroup text
    /// @{

    /// Implementation detail, private to the skribidi backend.
    class SkribidiContext;
    struct RegisteredText;
    struct EditSelection;

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

        void SwitchTextEditor(ID from, ID to) override;
        void SetUndoHistory(UndoHistory* history) override { m_undoHistory = history; }
        /// The field the editor holds: the text focus as of the last switch.
        ID ActiveElementID() const;
        bool DoesTextEditorAcceptTabs(ID elementId) const override;
        bool IsTextEditor(ID elementId) const override;
        void RequestSelectAll(ID elementId) override;
        TextEditUpdate ConsumeTextEditUpdate(TextID textId, std::string& outText) override;
        bool UpdateText(TextID textId, std::string_view text) override;

        std::string Copy() override;
        std::string Cut() override;
        void Paste(std::string_view text) override;

        bool IsHoveringSelectedText(ID textEditorId, ID windowId, tachyon::int2 screenSpace) const override;
        bool TryGetTextDragCursor(ID windowId, tachyon::int2 screenSpace, MouseCursor& outCursor) const override;
        bool TryGetTextDragHoverTarget(ID windowId, tachyon::int2 screenSpace, ID& outTargetId) const override;

        // -------------------- Cache Management --------------------

        void ClearCache() override;
        /// Number of registered texts held, for diagnostics.
        size_t RegisteredTextCount() const;



        // -------------------- Cursor Visibility --------------------

        void EnsureCursorVisible(ID textEditorId) override;
        std::optional<IntRect> GetCursorRect(ID textEditorId) override;
        std::optional<IntRect> GetCaretScreenRect() const override;

        // -------------------- Editor Registration --------------------


        TextID RegisterEditableText(ID elementId, std::string& text, TextEditBehavior behavior, StyleRef styleIndex, ElementState state, bool& wasModified,
                                    const ITextValidator* validator = nullptr) override;
        std::string* ValueEditText(ID elementId, bool& started) override;
        void   SetTextColor(TextID textId, VisualColorStyleIndex color) override;
        void   SetCaretColor(VisualColorStyleIndex color) override;


        // -------------------- Font Registration --------------------

        void SetFontDirectory(std::string_view directory) override;
        void SetFallbackFontDirectory(std::string_view directory) override;
        void SetLanguage(std::string_view languageTag) override;
        using ITextSystem::RegisterFontFamily;
        FontID RegisterFontFamily(std::string_view familyName, std::span<const FontFaceSource> faces) override;

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

        std::optional<MouseCursor> LinkCursorAt(ID elementId, int32_t localX, int32_t localY) const override;
        intptr_t HitTestContent(TextID textId, int32_t localX, int32_t localY) const override;
        void SetHoveredContent(TextID textId, intptr_t contentId) override;


        // -------------------- Icon Registration --------------------

        bool IsValidIcon(IconID iconId) const override;

        IconID RegisterIconFromSVG(const char* name, const char* svg_data, size_t svg_size) override;

        IconID RegisterIconFromSVGFile(const char* path) override;

        // -------------------- Icon Measurement --------------------

        IconMeasurement GetIconMeasurement(IconID iconId) const override;
        IconMeasurement MeasureIcon(IconID iconId) const override;

        // -------------------- Icon Rendering --------------------

        void RenderIcon(IconID iconId, IntRect rect, IntRect clipRect, VisualColorStyleIndex tint,
                        std::span<const VisualShadow> shadows = {}) override;

        static void ScreenToTextEditorLocalPoint(tachyon::int2 screenSpace, const IntRect& textEditorInnerRect, tachyon::float2& outLocal,
                                                 const float3x2& screenToLayout = float3x2::Identity());

    private:
        void UpdateGlyphs();
        /// Recreates the font collection from the registered families, freeing replaced
        /// fonts. Returns whether family `fontId` loaded.
        bool RebuildFontCollection(FontID fontId);
        void EvictUnusedTexts();

        bool TryGetCaretPositionInternal(ID windowId, tachyon::int2 screenPos, ID targetTextEditorId, skb_editor_t*& outTargetEditor, skb_text_position_t& outPosition);
        void DropDraggedText(ID windowId, tachyon::int2 screenPos, ID targetTextEditorId);
        void SelectFromDropPoint(skb_editor_t* editor, skb_text_position_t dropPoint);
        void EndTextDragging();

        void RenderText(RegisteredText* entry, IntRect rect, IntRect innerRectScreen, IntRect clipRect, float pixelScale);
        /// RenderIcon in the current shadow pass (SkribidiContext::DrawWithShadows).
        void DrawIcon(IconID iconId, IntRect rect, IntRect clipRect, VisualColorStyleIndex tint);
        void RenderEditor(RegisteredText* entry, IntRect rect, IntRect innerRectScreen, IntRect clipRect, float pixelScale);
        bool UpdateText(RegisteredText* entry, std::string_view text);

        bool ApplyTextEditUpdate(ID elementId, std::string& currentText, bool& wasModified);
        void ClearComposition();
        bool TryGetActiveTextEditorLocalPoint(ID windowId, tachyon::int2 screenSpace, tachyon::float2& outLocal) const;
        /// True if (`localX`, `localY`) in the active editor falls within a link run. Lets
        /// read-only editors skip caret placement on a link press.
        bool IsActiveEditorPosOnLink(float localX, float localY) const;
        bool TryGetTextEditorLocalPoint(ID textEditorId, ID windowId, tachyon::int2 screenSpace, tachyon::float2& outLocal) const;
        ID FindTextEditorAtScreenPoint(ID windowId, tachyon::int2 screenSpace) const;
        /// The validator a text editor registered with (null: any text).
        const ITextValidator* TextEditorValidator(ID elementId) const;
        bool CanDropDraggedTextInto(ID targetTextEditorId) const;
        void* UpdateLayoutWhenNecessary(RegisteredText* entry, float width) const;
        void* UpdateEditorWhenNecessary(RegisteredText* entry, float width) const;
        TextMeasurement UpdateBaseMeasurementWhenNecessary(RegisteredText* entry) const;
        TextMeasurement UpdateWrapMeasurementWhenNecessary(RegisteredText* entry, const TextWrapContext& wrapContext) const;
        TextMeasurement MeasureTextInternal(RegisteredText* entry, const skb_layout_t* layout) const;

        void UpdateRegisteredTextStyleState(RegisteredText* entry, tachyon::StyleRef styleIndex, tachyon::ElementState state);

        std::unordered_map<uint64_t, TextID> m_elementIdToTextIdMap;
        std::unordered_map<uint64_t, TextID> m_textKeyToIdMap;
        std::unordered_set<uint64_t> m_textEditorElements; ///< element IDs that are text editors
        std::unordered_set<uint64_t> m_readOnlyEditorElements; ///< element IDs of read-only text editors

        std::unordered_map<uint64_t, const ITextValidator*> m_textEditorValidators;

        ID m_textDragSourceId = ID::Invalid();
        UndoHistory* m_undoHistory = nullptr;
        /// Text an undo / redo (or a drop) sets on a field, by element: given to its bound
        /// value the next time it registers.
        std::unordered_map<uint64_t, std::string> m_externalText;
        void PushTextEdit(ID elementId, const std::string& before, const std::string& after);
        /// An undo / redo gives the focus back to the field it changed (without selecting all),
        /// with the selection it had then.
        void FocusChangedField(ID elementId, EditSelection selection);
        /// Remembers the selection before input that may change the text, for the undo step
        /// to restore. Call before such input.
        void NoteSelectionBeforeEdit();
        std::string m_textDragPayload;
        ID m_skipNextSelectAllId = ID::Invalid();
        bool m_textDragArmed = false;
        int2 m_textDragStartScreenPos = int2 { 0, 0 };
        ID          m_valueEditElementId = ID::Invalid(); ///< the focused value field (ValueEditText)
        std::string m_valueEditText;                      ///< the text it edits

        ID m_pendingCursorVisibleEditorId = ID::Invalid();

        SkribidiContext* m_skribidiContext;
        std::string      m_fontDirectory { "fonts" };
        std::string      m_fallbackFontDirectory;
    };

    /// @}
} // namespace tachyon
