#pragma once

#include <gui_core/gui_base_types.h>
#include <gui_core/gui_id.h>
#include <gui_core/gui_hash.h>

#include <gui_core/text/gui_numeric_field_config.h>
#include <gui_core/text/gui_numeric_type.h>
#include <gui_core/text/gui_text_converter.h>

#include <gui_core/input/gui_platform_input_handler.h>

#include <gui_core/style/gui_style.h>
#include <gui_core/style/gui_style_ref.h>

#include <bit>
#include <cstddef>
#include <stdint.h>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace tachyon
{
    /// One face of a font family: a font file, or font data in memory that stays valid
    /// while the family is registered (embedded in the executable, say).
    struct FontFaceSource
    {
        std::string_view           file;
        std::span<const std::byte> data;
    };

    // Forward declarations
    class GraphicsContext;
    class GUIStateManager;
    class IClipboard;
    class StyleManager;
    class ScrollManagement;
    class TextAtlasManager;
    enum class KeyAction : uint8_t;
    enum class MouseAction : uint8_t;
    enum class ElementFlags : uint32_t;

    // ------------------------------------------------------------------------
    // Icon Measurement Result
    // ------------------------------------------------------------------------

    struct IconMeasurement
    {
        int32_t width = 0;
        int32_t height = 0;

        constexpr int2 Size() const { return int2{ width, height }; }
        constexpr bool IsEmpty() const { return width == 0 && height == 0; }
    };

    // ------------------------------------------------------------------------
    // Text Measurement Result
    // ------------------------------------------------------------------------
    struct TextMeasurement
    {
        static constexpr int32_t kUnbounded = std::numeric_limits<int32_t>::max();
        
        int32_t minWidth = 0;   ///< min-content width: widest unbreakable run; maxWidth when it can't wrap
        int32_t maxWidth = 0;   ///< max-content width: the widest line without wrapping
        int32_t minHeight = 0;  ///< height of the text laid out at maxWidth
        int32_t maxHeight = 0;  ///< same as minHeight

        constexpr int2 MinSize() const { return int2{ minWidth, minHeight }; }
        constexpr int2 MaxSize() const { return int2{ maxWidth, maxHeight }; }
        constexpr bool IsEmpty() const { return maxWidth == 0 && maxHeight == 0; }
    };

    // ------------------------------------------------------------------------
    // Text Wrap Context - Constraints for text measurement
    // ------------------------------------------------------------------------

    struct TextWrapContext
    {
        int32_t maxWidth = INT32_MAX;
        int32_t maxHeight = INT32_MAX;
        uint32_t maxLines = UINT32_MAX;

        bool operator==(const TextWrapContext&) const = default;

        inline uint64_t GetHashCode() const { return Hash::Calculate(*this); }
    };

    // ------------------------------------------------------------------------
    // Text Editing Behavior
    // ------------------------------------------------------------------------

    enum class TextEditBehavior : uint8_t
    {
        None = 0,
        ContinuousUpdate = 1 << 0,  ///< update the text on every keystroke, not only on commit
        Multiline = 1 << 1,         ///< Enter inserts a newline instead of committing
        AcceptTabs = 1 << 2,        ///< Tab inserts a tab character instead of moving focus
        ReadOnly = 1 << 3           ///< selectable and copyable, but not editable
    };
    TACHYON_ENUM_OPERATORS(TextEditBehavior)

        
    // ----------------------------------------------------------------------------------
    // ContentRun - Represents a run of either text or an icon for rich text registration
    // ----------------------------------------------------------------------------------

    /// Colors of a link run, normal and hovered.
    struct LinkStyle
    {
        color4b color      { 64, 148, 255, 255 };
        color4b hoverColor { 128, 200, 255, 255 };
    };

    /// One run of rich text: a piece of text or an inline icon.
    struct ContentRun
    {
        static constexpr float kSizeAuto = -1.0f; ///< size the icon automatically

        enum Type { Text, Icon };
        Type type;

        std::string_view text;   ///< Text runs
        IconID           iconID; ///< Icon runs

        float iconWidth  = kSizeAuto;   ///< CSS px, or kSizeAuto
        float iconHeight = kSizeAuto;   ///< CSS px, or kSizeAuto

        intptr_t contentId = 0;    ///< non-zero makes the run hittable (HitTestContent returns it)
        bool     isLink    = false; ///< drawn as a link: underlined, in linkStyle's colors
        LinkStyle linkStyle;

        /// Text style of this run in place of the element's (font, size, weight, ...); null
        /// uses the element's. Caller-owned; must outlive the frame, like `text`.
        const TextStyle* textStyleOverride = nullptr;

        uint64_t GetHashCode() const
        {
            uint64_t hash = Hash::Calculate(type);
            if (type == Text)
            {
                hash = Hash::Combine(hash, Hash::Calculate(text));
            } else
            {
                if (iconID == IconID::Invalid)
                {
                    hash = Hash::Combine(hash, ((uint32_t)iconID), 0, 0);
                } else
                {
                    hash = Hash::Combine(hash, ((uint32_t)iconID), std::bit_cast<uint32_t>(iconWidth), std::bit_cast<uint32_t>(iconHeight));
                }
            }
            hash = Hash::Combine(hash, static_cast<uint64_t>(contentId), isLink ? 1u : 0u);
            if (textStyleOverride)
                hash = Hash::Combine(hash, textStyleOverride->GetHashCode());
            return hash;
        }
    };


    // ------------------------------------------------------------------------
    // Abstract Text System Interface
    // ------------------------------------------------------------------------

    /// Text and icon registration, measurement, rendering and editing.
    class ITextSystem : public IPlatformInputHandler
    {
    public:
        virtual ~ITextSystem() = default;

        struct TextEditUpdate
        {
            bool textChanged = false;
            bool textCommitted = false;

            constexpr bool HasUpdate() const
            {
                return textChanged || textCommitted;
            }
        };

        // -------------------- Initialization --------------------

        /// Where the built-in fonts are (default "fonts", relative to the working directory):
        /// Inter for text and JetBrains Mono for monospace. Empty loads none, for an app that
        /// registers its own. Initialize fails when they are set but missing. Call before Initialize.
        virtual void SetFontDirectory(std::string_view directory) = 0;

        /// Where the optional fallback fonts are: Noto, for other scripts, emoji and symbols.
        /// Empty (the default) loads none. Only the files an app ships there are used; missing
        /// ones are skipped with a warning. Call before Initialize.
        virtual void SetFallbackFontDirectory(std::string_view directory) = 0;

        virtual bool Initialize(GraphicsContext* graphicsContext, StyleManager* styleManager,
                                TextAtlasManager* textAtlasManager,
                                GUIStateManager* guiStateManager, IClipboard* clipboard) = 0;
        virtual void Shutdown() = 0;
        virtual void Update() = 0;

        /// Starts a frame, before the UI is built: clears the per-frame editor registrations
        /// that this frame's RegisterEditableText calls fill again.
        virtual void BeginFrame(int2 screenMousePos, MouseButtons mouseButtons, uint32_t timeMs) = 0;

        /// Records the glyph and icon atlas entries a cached scope's primitives use, until
        /// EndAtlasRecord. ReplayAtlasTouches keeps them alive on each frame the cached pixels
        /// are reused, and returns false when one was evicted or moved: the scope must render again.
        virtual void BeginAtlasRecord(ID /*scopeId*/) {}
        virtual void EndAtlasRecord() {}
        virtual bool ReplayAtlasTouches(ID /*scopeId*/) { return true; }

        // -------------------- Font Registration --------------------

        /// The default language as a BCP 47 tag ("ja", "zh-Hant", "ko"), for text whose
        /// style has no language of its own (TextStyle::language / -tachyon-language).
        /// It picks regional letterforms, e.g. Japanese kanji vs Chinese hanzi; empty
        /// means Simplified Chinese forms. Changing it lays all text out again.
        virtual void SetLanguage(std::string_view languageTag) = 0;

        /// Registers a font family from its faces (regular, bold, italic, ...); each face's
        /// weight and style come from the file. Returns FontIDFromName(familyName) for use in
        /// TextStyle::fontId, or FontID::Invalid if no face could be loaded. Re-registering a
        /// family name with the same faces is a no-op; other faces replace the old ones and all
        /// text is laid out again (a failed re-registration keeps the previous faces).
        virtual FontID RegisterFontFamily(std::string_view familyName, std::span<const FontFaceSource> faces) = 0;
        FontID RegisterFontFamily(std::string_view familyName, std::span<const std::string_view> faceFiles)
        {
            std::vector<FontFaceSource> faces;
            faces.reserve(faceFiles.size());
            for (std::string_view file : faceFiles)
                faces.push_back(FontFaceSource{ .file = file });
            return RegisterFontFamily(familyName, faces);
        }

        // -------------------- Text Registration --------------------

        /// Registers static text, with an optional icon, for `elementId`.
        virtual TextID RegisterText(ID elementId, IconID iconId, std::string_view text, StyleRef styleIndex, ElementState state) = 0;
        TextID RegisterText(ID elementId, std::string_view text, StyleRef styleIndex, ElementState state) { return RegisterText(elementId, IconID::Invalid, text, styleIndex, state); }
        
        /// Registers `text` as editable by `elementId`; `wasModified` is set when an edit changed
        /// it. `validator` limits what can be typed, pasted or dropped in; it must outlive the field.
        virtual TextID RegisterEditableText(ID elementId, std::string& text, TextEditBehavior behavior, StyleRef styleIndex, ElementState state, bool& wasModified,
                                            const ITextValidator* validator = nullptr) = 0;

        /// The text the focused field `elementId` edits, kept while it has the focus (its
        /// value isn't formatted again under the typing); null when it doesn't have it.
        /// `started` is set when the focus just arrived: fill it from the value then.
        virtual std::string* ValueEditText(ID elementId, bool& started) = 0;

        /// Registers a field that edits `value` as text: `converter` formats it and parses the
        /// edits back (see TextConverter). `wasModified` is set when an edit parsed.
        template<typename T, typename Converter = TextConverter<T>>
        TextID RegisterEditableValue(ID elementId, T& value, TextEditBehavior behavior, StyleRef styleIndex, ElementState state, bool& wasModified,
                                     const Converter& converter = DefaultTextConverter<T>())
        {
            bool started = false;
            std::string* editText = ValueEditText(elementId, started);
            std::string shownText;
            if (editText == nullptr)
                shownText = converter.Format(value, /*editing*/ false);
            else if (started)
                *editText = converter.Format(value, /*editing*/ true);
            std::string& text = editText ? *editText : shownText;

            bool edited = false;
            const TextID textId = RegisterEditableText(elementId, text, behavior, styleIndex, state, edited, &converter);
            if (edited && converter.Parse(text, value))
                wasModified = true;
            return textId;
        }

        /// Registers an editable number, clamped to `config`'s [minVal, maxVal] after an edit.
        template<typename T>
        TextID RegisterEditableNumber(ID elementId, T& value, TextEditBehavior behavior, StyleRef styleIndex, ElementState state, bool& wasModified, NumericFieldConfig<T> config = {})
        {
            bool edited = false;
            const TextID textId = RegisterEditableValue(elementId, value, behavior, styleIndex, state, edited);
            if (edited)
            {
                wasModified = true;
                value = std::clamp(value, config.minVal, config.maxVal);
            }
            return textId;
        }

        /// Draw a registered text in `color` instead of its style's text color, until it is
        /// registered again (call after each registration, e.g. for a NaN value).
        virtual void SetTextColor(TextID textId, VisualColorStyleIndex color) = 0;

        /// The text caret's color (the theme's --caret). Set again after the style manager is cleared.
        virtual void SetCaretColor(VisualColorStyleIndex color) = 0;
        /// Where committed text edits go as undo steps (none: not recorded).
        virtual void SetUndoHistory(class UndoHistory* history) = 0;

        /// The text registered as `textId`.
        virtual std::string_view GetText(TextID textId) const = 0;


        /// Registers text made of runs: styled text, inline icons and links.
        virtual TextID RegisterRichText(ID elementId, const std::span<ContentRun>& contentRuns,
                                        StyleRef styleIndex, ElementState state) = 0;

        /// Registers rich text that renders like RegisterRichText but can also be selected and
        /// copied (the runs' text joined). Pass TextEditBehavior::ReadOnly to block edits.
        virtual TextID RegisterEditableRichText(ID elementId, const std::span<ContentRun>& contentRuns,
                                                TextEditBehavior behavior, StyleRef styleIndex,
                                                ElementState state, bool& wasModified) = 0;

        /// The TextID `elementId` registered this frame, or TextID::Invalid. Lets an event
        /// handler find its text for hit-testing and hover.
        virtual TextID GetTextID(ID elementId) const = 0;

        // -------------------- Text Measurement (for layout) --------------------

        /// The unwrapped measurement of `textId`, in pixels; cached until its text or style changes.
        virtual TextMeasurement GetTextMeasurement(TextID textId) const = 0;

        /// The measurement of `textId` wrapped to `wrapContext.maxWidth`, in pixels.
        virtual TextMeasurement MeasureText(TextID textId, const TextWrapContext& wrapContext) const = 0;

        // -------------------- Text Rendering --------------------

        /// Draws `textId` into `rect` (renderer space, window-local), clipped to `clipRect`.
        /// `rect` can be larger than `innerRect` when the element scrolls its text.
        /// `innerRect` is the visible content area in absolute layout coordinates (the
        /// element's ElementRect::innerRect); mouse positions are hit-tested against it after
        /// mapping them with `screenToLayout`, the inverse of any ancestor zoom/pan.
        virtual void RenderText(TextID textId, IntRect rect, IntRect innerRect, IntRect clipRect, float pixelScale = 1.f,
                                const float3x2& screenToLayout = float3x2::Identity()) = 0;

        /// True when the run at (`localX`, `localY`), element-local, is a link.
        virtual bool HitTestClickable(ID elementId, int32_t localX, int32_t localY) const = 0;

        /// The contentId of the run at (`localX`, `localY`), element-local; 0 if none.
        virtual intptr_t HitTestContent(TextID textId, int32_t localX, int32_t localY) const = 0;

        /// Sets the hovered run of `textId` (by contentId), drawn in its hover style; 0 for none.
        virtual void SetHoveredContent(TextID textId, intptr_t contentId) = 0;



        // -------------------- Icon Measurement --------------------

        /// The natural size of `iconId`, in pixels (same as MeasureIcon).
        virtual IconMeasurement GetIconMeasurement(IconID iconId) const = 0;

        /// The natural size of `iconId`, in pixels.
        virtual IconMeasurement MeasureIcon(IconID iconId) const = 0;

        // -------------------- Icon Registration --------------------

        /// True when `iconId` is a registered icon.
        virtual bool IsValidIcon(IconID iconId) const = 0;

        /// Registers an icon from SVG data under a unique name. Registering a name again
        /// returns the existing icon.
        virtual IconID RegisterIconFromSVG(const char* name, const char* svg_data, size_t svg_size) = 0;

        /// Registers an icon from an SVG file, named by its path. IconID::Invalid if the file is
        /// missing, unreadable or doesn't parse.
        virtual IconID RegisterIconFromSVGFile(const char* path) = 0;

        // -------------------- Icon Rendering --------------------

        /// Draws `iconId` fitted into `rect` (aspect kept, centered), clipped to `clipRect`.
        /// Single-color icons are drawn in `tint`, like text; colored icons keep their colors.
        virtual void RenderIcon(IconID iconId, IntRect rect, IntRect clipRect, VisualColorStyleIndex tint) = 0;



        // -------------------- Editor Input Handling --------------------
        /// Keyboard input for the active text editor.
        virtual void OnKeyboard(ID windowId, KeyCode key, int repeat, KeyModifier modifiers, KeyAction action) = 0;

        /// Mouse movement for text selection and drag-drop.
        virtual void OnMouseMove(ID windowId, tachyon::int2 screenPos, MouseButtons button, KeyModifier modifiers) = 0;

        /// Mouse buttons for text editors: caret placement, selection, drag-drop.
        virtual void OnMouseButton(ID windowId, tachyon::int2 screenPos, MouseButtons button, KeyModifier modifiers, MouseAction action) = 0;

        /// Mouse wheel input for text editors.
        virtual void OnMouseScroll(ID windowId, tachyon::int2 screenPos, tachyon::float2 scrollOffset, KeyModifier modifiers) = 0;

        /// The focused text field changed (GUIStateManager::TextEditorFocus): commit `from`
        /// (ID::Invalid if none) and move the editor to `to` (ID::Invalid: no editor).
        virtual void SwitchTextEditor(ID from, ID to) = 0;

        /// True when the editor `elementId` inserts tabs (TextEditBehavior::AcceptTabs).
        virtual bool DoesTextEditorAcceptTabs(ID elementId) const = 0;

        /// True when `elementId` registered editable text this frame.
        virtual bool IsTextEditor(ID elementId) const = 0;

        /// Selects all text of the editor `elementId` the next time it renders.
        virtual void RequestSelectAll(ID elementId) = 0;

        /// Takes the pending edit of `textId`: the edited text goes to `outText`, the result
        /// says whether it changed or was committed.
        virtual TextEditUpdate ConsumeTextEditUpdate(TextID textId, std::string& outText) = 0;

        /// Replaces the text of `textId`; true when it changed.
        virtual bool UpdateText(TextID textId, std::string_view text) = 0;

        /// The active editor's selected text.
        virtual std::string Copy() = 0;

        /// Removes the active editor's selected text and returns it.
        virtual std::string Cut() = 0;

        /// Inserts `text` into the active editor at the caret, replacing the selection.
        virtual void Paste(std::string_view text) = 0;

        /// True when the screen point `screenSpace` is over selected text in `textEditorId`.
        virtual bool IsHoveringSelectedText(ID textEditorId, ID windowId, tachyon::int2 screenSpace) const = 0;

        /// The mouse cursor to show while text is dragged; true when it overrides the hover cursor.
        virtual bool TryGetTextDragCursor(ID windowId, tachyon::int2 screenSpace, MouseCursor& outCursor) const = 0;

        /// While text is dragged, sets `outTargetId` to the drop target under the cursor
        /// (ID::Invalid if none) and returns true. False when no text drag is in progress.
        virtual bool TryGetTextDragHoverTarget(ID windowId, tachyon::int2 screenSpace, ID& outTargetId) const = 0;

        // -------------------- Cache Management --------------------

        /// Drops every registered text and the live editor; earlier TextIDs become invalid. The
        /// focused field keeps its focus. Call when the styles are reloaded.
        virtual void ClearCache() = 0;

        // -------------------- Cursor Visibility --------------------

        /// Asks for the caret of `textEditorId` to be scrolled into view; editor input calls it
        /// when the caret moves. Doesn't scroll: whoever scrolls takes the request with GetCursorRect.
        virtual void EnsureCursorVisible(ID textEditorId) = 0;

        /// Takes a pending EnsureCursorVisible request for `textEditorId` and returns the caret
        /// rect, relative to the editor's inner rect. Empty when none is pending for it.
        virtual std::optional<IntRect> GetCursorRect(ID textEditorId) = 0;

        /// The active editor's caret in absolute screen coordinates (through any zoom/pan),
        /// to place the IME candidate window. Empty when no editor is active.
        virtual std::optional<IntRect> GetCaretScreenRect() const = 0;
    };
} // namespace tachyon
