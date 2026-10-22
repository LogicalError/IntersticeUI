#pragma once

#include <gui_core/gui_base_types.h>
#include <gui_core/gui_id.h>
#include <gui_core/gui_hash.h>

#include <gui_core/text/gui_numeric_field_config.h>

#include <gui_core/input/gui_platform_input_handler.h>

#include <gui_core/style/gui_style.h>
#include <gui_core/style/gui_style_ref.h>

#include <stdint.h>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace GUI
{
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
        
        int32_t minWidth = 0;   // Width layout starts the text at (the unwrapped width, #69)
        int32_t maxWidth = 0;   // Total width if no wrapping occurs
        int32_t minHeight = 0;  // Height of text if every line is unbroken
        int32_t maxHeight = 0;  // Height of text if every word is on its own line

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
        ContinuousUpdate = 1 << 0,  // Update text on every keystroke (vs only on commit)
        Multiline = 1 << 1,         // Allow newlines (vs commit on Enter key)
        AcceptTabs = 1 << 2,        // Allow Tab key to insert tab character (vs navigate focus)
        ReadOnly = 1 << 3           // Selectable + copyable, but blocks all mutation
    };
    ENUM_OPERATORS(TextEditBehavior)

        
    // ----------------------------------------------------------------------------------
    // ContentRun - Represents a run of either text or an icon for rich text registration
    // ----------------------------------------------------------------------------------

    // Per-run link styling (colors for default and hover states)
    struct LinkStyle
    {
        color4b color      { 64, 148, 255, 255 };
        color4b hoverColor { 128, 200, 255, 255 };
    };

    // Helper struct for mixed content
    struct ContentRun
    {
        static constexpr float kSizeAuto = -1.0f; // size the icon automatically

        enum Type { Text, Icon };
        Type type;

        std::string_view text;   // For Text type
        IconID           iconID; // For Icon type

        float iconWidth  = kSizeAuto;   // CSS pixels
        float iconHeight = kSizeAuto;

        intptr_t contentId = 0;    // 0 = not hittable; non-zero = queryable via HitTestContent
        bool     isLink    = false; // if true, renders with link styling (underline + linkStyle colors)
        LinkStyle linkStyle;

        // Optional per-run text style override. When non-null, overrides font, size, weight etc.
        // Caller owns the pointer; lifetime must exceed the frame (same as text string_view).
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
                    hash = Hash::Combine(hash, ((uint32_t)iconID), *(uint32_t*)&iconWidth, *(uint32_t*)&iconHeight);
                }
            }
            hash = Hash::Combine(hash, static_cast<uint64_t>(contentId), isLink ? 1u : 0u);
            if (textStyleOverride)
                hash = Hash::Combine(hash, textStyleOverride->GetHashCode());
            return hash;
        }
    };


	// ------------------------------------------------------------------------
	// Numeric Value Union/Type - For editable numeric text fields
	// ------------------------------------------------------------------------

    enum class NumericType
    {
        None,       // not a numeric type,

        TypeUnsignedBit = 16,
        TypeSignedBit   = 32,
        TypeFloatBit    = 64,

        TypeUInt8  = TypeUnsignedBit | 1,
        TypeUInt16 = TypeUnsignedBit | 2,
        TypeUInt32 = TypeUnsignedBit | 3,
        TypeUInt64 = TypeUnsignedBit | 4,
        
        TypeInt8   = TypeSignedBit | 1,
        TypeInt16  = TypeSignedBit | 2,
        TypeInt32  = TypeSignedBit | 3,
        TypeInt64  = TypeSignedBit | 4,

        TypeFloat  = TypeFloatBit | 1,
        TypeDouble = TypeFloatBit | 2
    };

    union NumericValue
    {
        uint8_t  value_uint8;
        uint16_t value_uint16;
        uint32_t value_uint32;
        uint64_t value_uint64;
        
        int8_t  value_int8;
        int16_t value_int16;
        int32_t value_int32;
        int64_t value_int64;

        float   value_float;
        double  value_double;
    };
    
    template<typename T> NumericType GetNumericType(const T&     /*value*/) { return NumericType::None; }
    template<> inline NumericType GetNumericType(const int8_t&   /*value*/) { return NumericType::TypeInt8; }
    template<> inline NumericType GetNumericType(const int16_t&  /*value*/) { return NumericType::TypeInt16; }
    template<> inline NumericType GetNumericType(const int32_t&  /*value*/) { return NumericType::TypeInt32; }
    template<> inline NumericType GetNumericType(const int64_t&  /*value*/) { return NumericType::TypeInt64; }
    template<> inline NumericType GetNumericType(const uint8_t&  /*value*/) { return NumericType::TypeUInt8; }
    template<> inline NumericType GetNumericType(const uint16_t& /*value*/) { return NumericType::TypeUInt16; }
    template<> inline NumericType GetNumericType(const uint32_t& /*value*/) { return NumericType::TypeUInt32; }
    template<> inline NumericType GetNumericType(const uint64_t& /*value*/) { return NumericType::TypeUInt64; }
    template<> inline NumericType GetNumericType(const float&    /*value*/) { return NumericType::TypeFloat; }
    template<> inline NumericType GetNumericType(const double&   /*value*/) { return NumericType::TypeDouble; }
    
    template<typename T> NumericType CopyNumericTypeTo(const T& src, NumericValue& dst) { dst = {}; return NumericType::None; }
    template<> inline NumericType CopyNumericTypeTo(const int8_t&   src, NumericValue& dst) { dst.value_int8   = src; return NumericType::TypeInt8; }
    template<> inline NumericType CopyNumericTypeTo(const int16_t&  src, NumericValue& dst) { dst.value_int16  = src; return NumericType::TypeInt16; }
    template<> inline NumericType CopyNumericTypeTo(const int32_t&  src, NumericValue& dst) { dst.value_int32  = src; return NumericType::TypeInt32; }
    template<> inline NumericType CopyNumericTypeTo(const int64_t&  src, NumericValue& dst) { dst.value_int64  = src; return NumericType::TypeInt64; }
    template<> inline NumericType CopyNumericTypeTo(const uint8_t&  src, NumericValue& dst) { dst.value_uint8  = src; return NumericType::TypeUInt8; }
    template<> inline NumericType CopyNumericTypeTo(const uint16_t& src, NumericValue& dst) { dst.value_uint16 = src; return NumericType::TypeUInt16; }
    template<> inline NumericType CopyNumericTypeTo(const uint32_t& src, NumericValue& dst) { dst.value_uint32 = src; return NumericType::TypeUInt32; }
    template<> inline NumericType CopyNumericTypeTo(const uint64_t& src, NumericValue& dst) { dst.value_uint64 = src; return NumericType::TypeUInt64; }
    template<> inline NumericType CopyNumericTypeTo(const float&    src, NumericValue& dst) { dst.value_float  = src; return NumericType::TypeFloat; }
    template<> inline NumericType CopyNumericTypeTo(const double&   src, NumericValue& dst) { dst.value_double = src; return NumericType::TypeDouble; }
        
    template<typename T> bool CopyNumericTypeTo(NumericType type, const NumericValue& dst, T&     src) { dst = {}; return NumericType::None; } 
    template<> inline bool CopyNumericTypeTo(NumericType type, const NumericValue& src, int8_t&   dst) { if (type != NumericType::TypeInt8  ) return false; dst = src.value_int8  ; return true; }
    template<> inline bool CopyNumericTypeTo(NumericType type, const NumericValue& src, int16_t&  dst) { if (type != NumericType::TypeInt16 ) return false; dst = src.value_int16 ; return true; }
    template<> inline bool CopyNumericTypeTo(NumericType type, const NumericValue& src, int32_t&  dst) { if (type != NumericType::TypeInt32 ) return false; dst = src.value_int32 ; return true; }
    template<> inline bool CopyNumericTypeTo(NumericType type, const NumericValue& src, int64_t&  dst) { if (type != NumericType::TypeInt64 ) return false; dst = src.value_int64 ; return true; }
    template<> inline bool CopyNumericTypeTo(NumericType type, const NumericValue& src, uint8_t&  dst) { if (type != NumericType::TypeUInt8 ) return false; dst = src.value_uint8 ; return true; }
    template<> inline bool CopyNumericTypeTo(NumericType type, const NumericValue& src, uint16_t& dst) { if (type != NumericType::TypeUInt16) return false; dst = src.value_uint16; return true; }
    template<> inline bool CopyNumericTypeTo(NumericType type, const NumericValue& src, uint32_t& dst) { if (type != NumericType::TypeUInt32) return false; dst = src.value_uint32; return true; }
    template<> inline bool CopyNumericTypeTo(NumericType type, const NumericValue& src, uint64_t& dst) { if (type != NumericType::TypeUInt64) return false; dst = src.value_uint64; return true; }
    template<> inline bool CopyNumericTypeTo(NumericType type, const NumericValue& src, float&    dst) { if (type != NumericType::TypeFloat ) return false; dst = src.value_float ; return true; }
    template<> inline bool CopyNumericTypeTo(NumericType type, const NumericValue& src, double&   dst) { if (type != NumericType::TypeDouble) return false; dst = src.value_double; return true; }
    
    template<typename T> bool EqualsNumericType(NumericType type, NumericValue& left, const T& right) { return false; }
    template<> inline bool EqualsNumericType(NumericType type, NumericValue& left, const int8_t&   right) { return type == NumericType::TypeInt8   && left.value_int8   == right; }
    template<> inline bool EqualsNumericType(NumericType type, NumericValue& left, const int16_t&  right) { return type == NumericType::TypeInt16  && left.value_int16  == right; }
    template<> inline bool EqualsNumericType(NumericType type, NumericValue& left, const int32_t&  right) { return type == NumericType::TypeInt32  && left.value_int32  == right; }
    template<> inline bool EqualsNumericType(NumericType type, NumericValue& left, const int64_t&  right) { return type == NumericType::TypeInt64  && left.value_int64  == right; }
    template<> inline bool EqualsNumericType(NumericType type, NumericValue& left, const uint8_t&  right) { return type == NumericType::TypeUInt8  && left.value_uint8  == right; }
    template<> inline bool EqualsNumericType(NumericType type, NumericValue& left, const uint16_t& right) { return type == NumericType::TypeUInt16 && left.value_uint16 == right; }
    template<> inline bool EqualsNumericType(NumericType type, NumericValue& left, const uint32_t& right) { return type == NumericType::TypeUInt32 && left.value_uint32 == right; }
    template<> inline bool EqualsNumericType(NumericType type, NumericValue& left, const uint64_t& right) { return type == NumericType::TypeUInt64 && left.value_uint64 == right; }
    template<> inline bool EqualsNumericType(NumericType type, NumericValue& left, const float&    right) { return type == NumericType::TypeFloat  && left.value_float  == right; }
    template<> inline bool EqualsNumericType(NumericType type, NumericValue& left, const double&   right) { return type == NumericType::TypeDouble && left.value_double == right; }

    inline bool EqualsNumericType(NumericType leftType, NumericValue& left, NumericType rightType, const NumericValue& right)
    { 
        if (leftType != rightType) return false;
        switch (leftType)
        {
		    case NumericType::TypeFloat:  return left.value_float == right.value_float;
			case NumericType::TypeDouble: return left.value_double == right.value_double;

			case NumericType::TypeUInt64: return left.value_uint64 == right.value_uint64;
            case NumericType::TypeUInt32: return left.value_uint32 == right.value_uint32;
            case NumericType::TypeUInt16: return left.value_uint16 == right.value_uint16;
            case NumericType::TypeUInt8:  return left.value_uint8  == right.value_uint8;

			case NumericType::TypeInt64: return left.value_int64 == right.value_int64;
            case NumericType::TypeInt32: return left.value_int32 == right.value_int32;
            case NumericType::TypeInt16: return left.value_int16 == right.value_int16;
            case NumericType::TypeInt8:  return left.value_int8  == right.value_int8;

            default: return false;
        }
    }


    // ------------------------------------------------------------------------
    // Abstract Text System Interface
    // ------------------------------------------------------------------------

    /// Unified interface for all text operations: registration, measurement, rendering, and editing
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

        /// Where the default fonts are (default "fonts/Noto-otf", relative to the working
        /// directory). Call before Initialize.
        virtual void SetFontDirectory(std::string_view directory) = 0;

        virtual bool Initialize(GraphicsContext* graphicsContext, StyleManager* styleManager,
                                TextAtlasManager* textAtlasManager,
                                GUIStateManager* guiStateManager, IClipboard* clipboard) = 0;
        virtual void Shutdown() = 0;
        virtual void Update() = 0;

        /// Called once per frame before construction begins.
        /// Clears per-frame state (e.g. text editor element tracking) so it can be
        /// rebuilt from RegisterEditableText calls during construction.
        virtual void BeginFrame(int2 screenMousePos, MouseButtons mouseButtons, uint32_t timeMs) = 0;

        // Glyph/icon atlas entries a cached scope's captured primitives point at must
        // outlive the scope's rebuilds. Record while the scope renders; replay each
        // frame the capture is blitted. Replay returns false if an entry was evicted
        // or moved, meaning the capture is stale and the scope must re-render.
        virtual void BeginAtlasRecord(ID /*scopeId*/) {}
        virtual void EndAtlasRecord() {}
        virtual bool ReplayAtlasTouches(ID /*scopeId*/) { return true; }

        // -------------------- Font Registration --------------------

        /// The default language as a BCP 47 tag ("ja", "zh-Hant", "ko"), for text whose
        /// style has no language of its own (TextStyle::language / -interstice-language).
        /// It picks regional letterforms, e.g. Japanese kanji vs Chinese hanzi; empty
        /// means Simplified Chinese forms. Changing it lays all text out again.
        virtual void SetLanguage(std::string_view languageTag) = 0;

        /// Register a font family from its face files (regular, bold, italic, ...); each face's
        /// weight and style come from the file. Returns FontIDFromName(familyName) for use in
        /// TextStyle::fontId, or FontID::Invalid if no face could be loaded. Re-registering a
        /// family name with the same faces is a no-op; other faces replace the old ones and all
        /// text is laid out again (a failed re-registration keeps the previous faces).
        virtual FontID RegisterFontFamily(std::string_view familyName, std::span<const std::string_view> faceFiles) = 0;

        // -------------------- Text Registration --------------------

        /// Register constant text for a specific element ID
        virtual TextID RegisterText(ID elementId, IconID iconId, std::string_view text, StyleRef styleIndex, ElementState state) = 0;
        TextID RegisterText(ID elementId, std::string_view text, StyleRef styleIndex, ElementState state) { return RegisterText(elementId, IconID::Invalid, text, styleIndex, state); }
        
        /// Register editable text for a specific element ID
        virtual TextID RegisterEditableText(ID elementId, std::string& text, TextEditBehavior behavior, StyleRef styleIndex, ElementState state, bool& wasModified) = 0;

        /// Register editable numeric text for a specific element ID.
        /// The text representation and numeric<->text conversion are handled by the text system.
        /// If a NumericFieldConfig is provided, the value is clamped to [minVal, maxVal] after text editing.
        template<typename T>
        TextID RegisterEditableNumber(ID elementId, T& value, TextEditBehavior behavior, StyleRef styleIndex, ElementState state, bool& wasModified, NumericFieldConfig<T> config = {})
        {
            NumericValue numeric_value{};
            NumericType numeric_type = CopyNumericTypeTo(value, numeric_value);
			bool local_was_modified = false;
            auto textID = RegisterEditableNumber(elementId, numeric_type, numeric_value, behavior, styleIndex, state, local_was_modified);

            if (local_was_modified) {
                wasModified = true;
                CopyNumericTypeTo(numeric_type, numeric_value, value);
                value = std::clamp(value, config.minVal, config.maxVal);
            }
            return textID;
        }

        /// Register editable numeric text for a specific element ID.
        /// The text representation and int<->text conversion are handled by the text system.
        virtual TextID RegisterEditableNumber(ID elementId, NumericType type, NumericValue& value, TextEditBehavior behavior, StyleRef styleIndex, ElementState state, bool& wasModified) = 0;

        /// Get the original text string from a registered TextID
        virtual std::string_view GetText(TextID textId) const = 0;


        // Register text WITH inline icons
        virtual TextID RegisterRichText(ID elementId, const std::span<ContentRun>& contentRuns,
                                        StyleRef styleIndex, ElementState state) = 0;

        /// Register selectable rich text: renders like RegisterRichText (styled/clickable runs)
        /// but is also an editor, so it can be selected and copied. Pass TextEditBehavior::ReadOnly
        /// to block edits. The selectable/copyable text is the concatenation of the runs' text.
        virtual TextID RegisterEditableRichText(ID elementId, const std::span<ContentRun>& contentRuns,
                                                TextEditBehavior behavior, StyleRef styleIndex,
                                                ElementState state, bool& wasModified) = 0;

        /// Look up the TextID registered for an element this frame (Invalid if none). Lets a
        /// control's deferred event handler resolve its TextID for hit-testing / hover.
        virtual TextID GetTextID(ID elementId) const = 0;

        // -------------------- Text Measurement (for layout) --------------------

        /// Get cached base measurement for a registered text (no wrapping)
        virtual TextMeasurement GetTextMeasurement(TextID textId) const = 0;

        /// Measure text with specific wrapping constraints
        virtual TextMeasurement MeasureText(TextID textId, const TextWrapContext& wrapContext) const = 0;

        // -------------------- Text Rendering --------------------

        /// Render text at the specified position with clipping.
        /// `rect` is in renderer space (layout / window-local) and is where the
        /// text actually paints — it may be larger than `innerRect` when the
        /// element scrolls its text content.
        /// `innerRect` is the visible content area in absolute layout coordinates
        /// (the element's ElementRect::innerRect). The text system hit-tests
        /// incoming OS mouse positions against it after mapping them with
        /// `screenToLayout`, the inverse of any ancestor content transform
        /// (zoom/pan); identity when there is none.
        virtual void RenderText(TextID textId, IntRect rect, IntRect innerRect, IntRect clipRect, float pixelScale = 1.f,
                                const float3x2& screenToLayout = float3x2::Identity()) = 0;

        /// Returns true if the hittable run at (localX, localY) is clickable
        virtual bool HitTestClickable(ID elementId, int32_t localX, int32_t localY) const = 0;

        /// Returns the content_id of the hittable run at (localX, localY) in element-local coords, or 0 if none.
        virtual intptr_t HitTestContent(TextID textId, int32_t localX, int32_t localY) const = 0;

        /// Sets the currently-hovered content ID for hover-state rendering. Call with 0 to clear.
        virtual void SetHoveredContent(TextID textId, intptr_t contentId) = 0;



        // -------------------- Icon Measurement --------------------

        /// Get cached measurement for a registered icon
        virtual IconMeasurement GetIconMeasurement(IconID iconId) const = 0;

        /// Measure an icon by its ID
        virtual IconMeasurement MeasureIcon(IconID iconId) const = 0;

        // -------------------- Icon Registration --------------------

        /// Check if an icon exists
        virtual bool IsValidIcon(IconID iconId) const = 0;

        /// Register an icon from SVG data under a unique name. Registering a name
        /// again returns the existing icon.
        virtual IconID RegisterIconFromSVG(const char* name, const char* svg_data, size_t svg_size) = 0;

        /// Register an icon by loading an SVG file from disk. The path is
        /// used as the icon's name (for skribidi-side lookup/debugging).
        /// Returns IconID::Invalid if the file is missing, unreadable, or
        /// fails to parse.
        virtual IconID RegisterIconFromSVGFile(const char* path) = 0;

        // -------------------- Icon Rendering --------------------

        /// Single-color (monochrome) icons are drawn in `tint`, like text; colored icons
        /// keep their own colors.
        virtual void RenderIcon(IconID iconId, IntRect rect, IntRect clipRect, VisualColorStyleIndex tint) = 0;



        // -------------------- Editor Input Handling --------------------
        /// Handle keyboard input for text editors
        virtual void OnKeyboard(ID windowId, KeyCode key, int repeat, KeyModifier modifiers, KeyAction action) = 0;

        /// Handle mouse movement for text editors
        virtual void OnMouseMove(ID windowId, GUI::int2 screenPos, MouseButtons button, KeyModifier modifiers) = 0;

        /// Handle mouse button events for text editors
        virtual void OnMouseButton(ID windowId, GUI::int2 screenPos, MouseButtons button, KeyModifier modifiers, MouseAction action) = 0;

        /// Handle mouse scroll events for text editors
        virtual void OnMouseScroll(ID windowId, GUI::int2 screenPos, GUI::float2 scrollOffset, KeyModifier modifiers) = 0;

        /// Sets which text editor is currently active for input. Pass ID::Invalid to disable.
        virtual void SetActiveTextEditor(ID elementId) = 0;

        /// Check if a text editor accepts Tab key for character insertion (vs navigation)
        virtual bool DoesTextEditorAcceptTabs(ID elementId) const = 0;

        /// Check if an element has editable text registered (i.e., is a text editor)
        virtual bool IsTextEditor(ID elementId) const = 0;

        /// Request selecting all text the next time the specified editor is rendered.
        virtual void RequestSelectAll(ID elementId) = 0;

        /// Consume any pending text edits for the specified text ID.
        virtual TextEditUpdate ConsumeTextEditUpdate(TextID textId, std::string& outText) = 0;

        /// Update the stored text for an existing TextID.
        virtual bool UpdateText(TextID textId, std::string_view text) = 0;

        /// Copy currently selected text from the active editor.
        virtual std::string Copy() = 0;

        /// Cut currently selected text from the active editor and return it.
        virtual std::string Cut() = 0;

        /// Paste text into the active editor at the current cursor/selection.
        virtual void Paste(std::string_view text) = 0;

        /// Returns true when the provided screen-space point is over selected text in the given text editor.
        virtual bool IsHoveringSelectedText(ID textEditorId, ID windowId, GUI::int2 screenSpace) const = 0;

        /// Returns cursor feedback while text drag-drop is active.
        /// Return true when a drag cursor should override normal hover cursors.
        virtual bool TryGetTextDragCursor(ID windowId, GUI::int2 screenSpace, MouseCursor& outCursor) const = 0;

        /// During text drag-drop, returns the element ID of the valid drop target under the cursor.
        /// Returns true when text drag is active; outTargetId is Invalid if no valid drop target is under the cursor.
        /// Returns false when no text drag is in progress (caller should use normal hover logic).
        virtual bool TryGetTextDragHoverTarget(ID windowId, GUI::int2 screenSpace, ID& outTargetId) const = 0;

        // -------------------- Cache Management --------------------

        /// Clear all text caches (call when fonts/styles change)
        virtual void ClearCache() = 0;

        // -------------------- Cursor Visibility --------------------

        /// Signal that the cursor has moved and should be scrolled into view.
        /// Called internally when the user navigates with keyboard or mouse.
        /// Does not perform scrolling itself — call GetCursorRect() to consume
        /// the pending request and perform the actual scroll externally.
        virtual void EnsureCursorVisible(ID textEditorId) = 0;

        /// Returns the cursor bounding rect (in editor-local space) if there is a pending
        /// cursor-visibility request, and clears that request. Returns empty if no request is pending
        /// or if the active editor does not match textEditorId.
        virtual std::optional<IntRect> GetCursorRect(ID textEditorId) = 0;

        /// The active editor's caret in absolute screen coordinates (through any
        /// zoom/pan), e.g. to place the IME candidate window. Empty when no editor
        /// is active.
        virtual std::optional<IntRect> GetCaretScreenRect() const = 0;
    };
} // namespace GUI
