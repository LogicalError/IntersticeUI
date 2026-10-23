#pragma once

#include <gui_core/text/gui_text_system.h>
#include <gui_core/gui_id.h>
#include <gui_core/gui_hash.h>

#include <algorithm>
#include <climits>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <cstdio>
#include <cstdlib>

namespace interstice
{
    /// Stub implementation of ITextSystem for testing without platform dependencies
    class StubTextSystem : public ITextSystem
    {
    public:
        StubTextSystem(
            int32_t defaultCharWidth = 8,
            int32_t defaultLineHeight = 16,
            int32_t defaultIconSize = 16)
            : m_defaultCharWidth(defaultCharWidth)
            , m_defaultLineHeight(defaultLineHeight)
            , m_defaultIconSize(defaultIconSize)
        {
        }

        // -------------------- Initialization --------------------

        bool Initialize(GraphicsContext* /*graphicsContext*/, StyleManager* /*styleManager*/,
                        TextAtlasManager* /*textAtlasManager*/,
                        GUIStateManager* /*guiStateManager*/, IClipboard* /*clipboard*/) override
        {
            return true;
        }

        void Shutdown() override
        {
            m_registry.clear();
            m_elementIdToTextIdMap.clear();
            m_pendingEdits.clear();
            m_editableElementIds.clear();
            m_activeEditor = ID::Invalid();
        }

        void Update() override
        {
            // No-op for stub
        }
        void BeginFrame(int2 /*screenMousePos*/, MouseButtons /*mouseButtons*/, uint32_t /*timeMs*/) override
        {
            // No-op for stub
        }

        // -------------------- Font Registration --------------------

        void SetFontDirectory(std::string_view /*directory*/) override {}
        void SetLanguage(std::string_view /*languageTag*/) override {}

        FontID RegisterFontFamily(std::string_view familyName, std::span<const std::string_view> /*faceFiles*/) override
        {
            // Stub: no actual font loading.
            return FontIDFromName(familyName);
        }

        // -------------------- Text Registration --------------------

        TextID RegisterText(ID elementId, IconID /*iconId*/, std::string_view text, interstice::StyleRef styleIndex, interstice::ElementState state) override
        {
            uint64_t key = Hash::Combine(Hash::Calculate(elementId), Hash::Calculate(text), Hash::Calculate(styleIndex));
            
            if (auto it = m_elementIdToTextIdMap.find(key); it != m_elementIdToTextIdMap.end())
                return it->second;

            TextMeasurement base = MeasureTextInternal(text, styleIndex);

            RegisteredText entry;
            entry.elementId = elementId;
            entry.text = text;
            entry.styleIndex = styleIndex;
            entry.state = state;
            entry.baseMeasurement = base;

            uint32_t newIndex = static_cast<uint32_t>(m_registry.size() + 1);
            m_registry.push_back(entry);
            
            TextID textId{newIndex};
            m_elementIdToTextIdMap[key] = textId;
            m_elementTextIdMap[Hash::Calculate(elementId)] = textId;
            return textId;
        }

        TextID RegisterEditableText(ID elementId, std::string& text, TextEditBehavior behavior, interstice::StyleRef styleIndex, interstice::ElementState state, bool& wasModified) override
        {
            m_editableElementIds.insert(elementId);
            ApplyTextEditUpdate(elementId, text, behavior, wasModified);
            return RegisterText(elementId, IconID::Invalid, text, styleIndex, state);
        }

        // Edits the number as text, parsing it back when the edit is applied.
        TextID RegisterEditableNumber(ID elementId, NumericType type, NumericValue& value, TextEditBehavior behavior, StyleRef styleIndex, ElementState state, bool& wasModified) override
        {
            std::string text = NumberToText(type, value);
            const TextID textId = RegisterEditableText(elementId, text, behavior, styleIndex, state, wasModified);
            if (wasModified)
                wasModified = TextToNumber(text, type, value);
            return textId;
        }


        std::string_view GetText(TextID textId) const override
        {
            if (textId == TextID::Invalid || ((uint32_t)textId) > m_registry.size())
                return "";
            return m_registry[((uint32_t)textId) - 1].text;
        }

        TextID GetTextID(ID elementId) const override
        {
            auto it = m_elementTextIdMap.find(Hash::Calculate(elementId));
            return it != m_elementTextIdMap.end() ? it->second : TextID::Invalid;
        }

        // -------------------- Text Measurement --------------------

        TextMeasurement GetTextMeasurement(TextID textId) const override
        {
            if (textId == TextID::Invalid || ((uint32_t)textId) > m_registry.size())
                return {};
            return m_registry[((uint32_t)textId) - 1].baseMeasurement;
        }

        TextMeasurement MeasureText(TextID textId, const TextWrapContext& wrapContext) const override
        {
            if (textId == TextID::Invalid || ((uint32_t)textId) > m_registry.size())
                return {};

            const auto& entry = m_registry[((uint32_t)textId) - 1];

            if (wrapContext.maxWidth == INT32_MAX)
                return entry.baseMeasurement;

            return MeasureTextInternal(entry.text, entry.styleIndex, wrapContext.maxWidth);
        }

        IconMeasurement GetIconMeasurement(IconID iconId) const override
        {
            return MeasureIcon(iconId);
        }

        IconMeasurement MeasureIcon(IconID /*iconId*/) const override
        {
            return IconMeasurement{ .width = m_defaultIconSize, .height = m_defaultIconSize };
        }

        bool IsValidIcon(IconID iconId) const override
        {
            return iconId != IconID::Invalid;
        }

        // -------------------- Text Rendering --------------------

        void RenderText(TextID /*textId*/, IntRect /*rect*/, IntRect /*innerRect*/, IntRect /*clipRect*/, float /*pixelScale*/ = 1.f,
                        const float3x2& /*screenToLayout*/ = float3x2::Identity()) override
        {
            // No-op for stub
        }

        // Any non-negative local coordinate inside a known element is "on the text"
        // (editable widgets route clicks into the editor with it).
        bool HitTestClickable(ID elementId, int32_t localX, int32_t localY) const override
        {
            if (localX < 0 || localY < 0) return false;
            // If we've ever seen this element, treat the coordinate as on-text.
            return m_elementTextIdMap.find(Hash::Calculate(elementId)) != m_elementTextIdMap.end();
        }
        // Character index for a local x, at m_defaultCharWidth per character.
        intptr_t HitTestContent(TextID textId, int32_t localX, int32_t /*localY*/) const override
        {
            if (textId == TextID::Invalid || ((uint32_t)textId) > m_registry.size())
                return 0;
            if (localX <= 0) return 0;
            const auto& entry = m_registry[((uint32_t)textId) - 1];
            int32_t idx = localX / m_defaultCharWidth;
            int32_t maxIdx = static_cast<int32_t>(entry.text.length());
            if (idx > maxIdx) idx = maxIdx;
            return static_cast<intptr_t>(idx);
        }
        void SetHoveredContent(TextID /*textId*/, intptr_t /*contentId*/) override {}

        // -------------------- Icon Registration --------------------

        IconID RegisterIconFromSVG(const char* /*name*/, const char* /*svg_data*/, size_t /*svg_size*/) override
        {
            // Unique ID per call.
            static uint32_t nextIconId = 1;
            return IconID{ nextIconId++ };
        }

        IconID RegisterIconFromSVGFile(const char* /*path*/) override
        {
            static uint32_t nextIconId = 0x10000000;
            return IconID{ nextIconId++ };
        }

        // -------------------- Icon Rendering --------------------

        void RenderIcon(IconID /*iconId*/, IntRect /*rect*/, IntRect /*clipRect*/, VisualColorStyleIndex /*tint*/) override
        {
            // No-op for stub
        }

        // -------------------- Rich Text Registration --------------------

        TextID RegisterRichText(ID elementId, const std::span<ContentRun>& contentRuns,
                               StyleRef styleIndex, ElementState state) override
        {
            // Build concatenated text for stub
            std::string combinedText;
            for (const auto& run : contentRuns)
            {
                if (run.type == ContentRun::Text)
                    combinedText += run.text;
                else
                    combinedText += "I"; // Placeholder for icon
            }
            
            return RegisterText(elementId, IconID::Invalid, combinedText, styleIndex, state);
        }

        TextID RegisterEditableRichText(ID elementId, const std::span<ContentRun>& contentRuns,
                                        TextEditBehavior /*behavior*/, StyleRef styleIndex,
                                        ElementState state, bool& wasModified) override
        {
            m_editableElementIds.insert(elementId);
            wasModified = false;
            return RegisterRichText(elementId, contentRuns, styleIndex, state);
        }

        // -------------------- Cursor Visibility --------------------

        void EnsureCursorVisible(ID /*textEditorId*/) override {}
        std::optional<IntRect> GetCursorRect(ID /*textEditorId*/) override { return std::nullopt; }
        std::optional<IntRect> GetCaretScreenRect() const override { return std::nullopt; }

        // -------------------- Editor Input Handling --------------------

        // Routes typed characters (KeyAction::KeyChar, as InjectText sends them) and
        // editing keys into the active editor's pending buffer.
        void OnKeyboard(ID /*windowId*/, KeyCode key, int /*repeat*/, KeyModifier /*mods*/, KeyAction action) override
        {
            if (m_activeEditor == ID::Invalid()) return;
            auto& pending = m_pendingEdits[Hash::Calculate(m_activeEditor)];
            if (action == KeyAction::KeyChar)
            {
                unsigned int codepoint = static_cast<unsigned int>(key);
                if (codepoint < 32 && codepoint != '\t') return;
                if (codepoint > 127) return;
                pending.inserts += static_cast<char>(codepoint);
            }
            else if (action == KeyAction::KeyDown)
            {
                if (key == KeyCode::Backspace) pending.backspaces++;
                else if (key == KeyCode::Delete) pending.deletes++;
                else if (key == KeyCode::Return || key == KeyCode::KeyPadEnter) pending.commit = true;
            }
        }
        void OnMouseMove(ID /*windowId*/, int2 /*screenPos*/, MouseButtons /*button*/, KeyModifier /*mods*/) override {}
        void OnMouseButton(ID /*windowId*/, int2 /*screenPos*/, MouseButtons /*button*/, KeyModifier /*mods*/, MouseAction /*action*/) override {}
        void OnMouseScroll(ID /*windowId*/, int2 /*screenPos*/, float2 /*scrollOffset*/, KeyModifier /*mods*/) override {}
        void OnTextComposition(ID /*windowId*/, std::string_view /*text*/, int32_t /*cursor*/) override {}

        void SetActiveTextEditor(ID elementId) override
        {
            m_activeEditor = elementId;
        }

        bool DoesTextEditorAcceptTabs(ID /*elementId*/) const override
        {
            return false; // Stub doesn't support tab-as-text
        }

        bool IsTextEditor(ID elementId) const override
        {
            // Any element that called RegisterEditableText is an editor.
            return m_editableElementIds.count(elementId) > 0;
        }

        void RequestSelectAll(ID /*elementId*/) override {}

        TextEditUpdate ConsumeTextEditUpdate(TextID /*textId*/, std::string& /*outText*/) override
        {
            return {};
        }

        bool UpdateText(TextID textId, std::string_view text) override
        {
            if (textId != TextID::Invalid || ((uint32_t)textId) > m_registry.size())
                return false;

            auto& entry = m_registry[((uint32_t)textId) - 1];
            if (entry.text == text)
                return false;

            uint64_t oldKey = entry.GetHashCode();
            auto it = m_elementIdToTextIdMap.find(oldKey);
            if (it != m_elementIdToTextIdMap.end() && it->second == textId)
                m_elementIdToTextIdMap.erase(it);

            entry.text = std::string(text);
            entry.baseMeasurement = MeasureTextInternal(entry.text, entry.styleIndex);

            uint64_t newKey = entry.GetHashCode();
            m_elementIdToTextIdMap[newKey] = textId;
            return true;
        }

        std::string Copy() override
        {
            return {};
        }

        std::string Cut() override
        {
            return {};
        }

        void Paste(std::string_view /*text*/) override
        {
        }

        bool IsHoveringSelectedText(ID /*textEditorId*/, ID /*windowId*/, interstice::int2 /*screenSpace*/) const override
        {
            return false;
        }

        bool TryGetTextDragCursor(ID /*windowId*/, interstice::int2 /*screenSpace*/, MouseCursor& /*outCursor*/) const override
        {
            return false;
        }

        bool TryGetTextDragHoverTarget(ID /*windowId*/, interstice::int2 /*screenSpace*/, ID& /*outTargetId*/) const override
        {
            return false;
        }

        // -------------------- Cache Management --------------------

        void ClearCache() override
        {
            m_registry.clear();
            m_elementIdToTextIdMap.clear();
            m_elementTextIdMap.clear();
        }

        // -------------------- Configuration --------------------

        void SetDefaultCharWidth(int32_t width) { m_defaultCharWidth = width; }
        void SetDefaultLineHeight(int32_t height) { m_defaultLineHeight = height; }
        void SetDefaultIconSize(int32_t size) { m_defaultIconSize = size; }

    private:
        // Drain queued character / backspace / commit events into `currentText`.
        bool ApplyTextEditUpdate(ID elementId, std::string& currentText, TextEditBehavior behavior, bool& wasModified)
        {
            auto it = m_pendingEdits.find(Hash::Calculate(elementId));
            if (it == m_pendingEdits.end()) return false;
            PendingEdit& pe = it->second;

            // Mirror the real text system's contract: with ContinuousUpdate
            // the bound string tracks every change; with None the edits stay
            // buffered in the editor until a commit (Enter) flushes them.
            // wasModified only fires when the bound string is written.
            const bool continuousUpdate = (behavior & TextEditBehavior::ContinuousUpdate) != TextEditBehavior::None;
            if (!pe.commit && !continuousUpdate)
                return false;

            bool textChanged = false;
            for (uint32_t i = 0; i < pe.backspaces && !currentText.empty(); ++i)
            {
                currentText.pop_back();
                textChanged = true;
            }
            for (uint32_t i = 0; i < pe.deletes && !currentText.empty(); ++i)
            {
                currentText.pop_back();
                textChanged = true;
            }
            if (!pe.inserts.empty())
            {
                currentText += pe.inserts;
                textChanged = true;
            }
            const bool committed = pe.commit;

            // Reset the pending buffer; the next frame may receive new keys.
            pe = PendingEdit{};

            const bool shouldNotify = committed || (continuousUpdate && textChanged);
            if (shouldNotify) wasModified = true;
            return shouldNotify;
        }

        static std::string NumberToText(NumericType type, const NumericValue& value)
        {
            switch (type)
            {
                case NumericType::TypeUInt8:  return std::to_string(value.value_uint8);
                case NumericType::TypeUInt16: return std::to_string(value.value_uint16);
                case NumericType::TypeUInt32: return std::to_string(value.value_uint32);
                case NumericType::TypeUInt64: return std::to_string(value.value_uint64);
                case NumericType::TypeInt8:   return std::to_string(value.value_int8);
                case NumericType::TypeInt16:  return std::to_string(value.value_int16);
                case NumericType::TypeInt32:  return std::to_string(value.value_int32);
                case NumericType::TypeInt64:  return std::to_string(value.value_int64);
                case NumericType::TypeFloat:  return std::to_string(value.value_float);
                case NumericType::TypeDouble: return std::to_string(value.value_double);
                default:                      return {};
            }
        }

        // Returns false (leaving value untouched) when the text isn't a valid number.
        static bool TextToNumber(const std::string& text, NumericType type, NumericValue& value)
        {
            const char* begin = text.c_str();
            char* end = nullptr;
            NumericValue parsed{};
            switch (type)
            {
                case NumericType::TypeUInt8:  parsed.value_uint8  = (uint8_t) std::strtoull(begin, &end, 10); break;
                case NumericType::TypeUInt16: parsed.value_uint16 = (uint16_t)std::strtoull(begin, &end, 10); break;
                case NumericType::TypeUInt32: parsed.value_uint32 = (uint32_t)std::strtoull(begin, &end, 10); break;
                case NumericType::TypeUInt64: parsed.value_uint64 =           std::strtoull(begin, &end, 10); break;
                case NumericType::TypeInt8:   parsed.value_int8   = (int8_t)  std::strtoll(begin, &end, 10);  break;
                case NumericType::TypeInt16:  parsed.value_int16  = (int16_t) std::strtoll(begin, &end, 10);  break;
                case NumericType::TypeInt32:  parsed.value_int32  = (int32_t) std::strtoll(begin, &end, 10);  break;
                case NumericType::TypeInt64:  parsed.value_int64  =           std::strtoll(begin, &end, 10);  break;
                case NumericType::TypeFloat:  parsed.value_float  =           std::strtof(begin, &end);       break;
                case NumericType::TypeDouble: parsed.value_double =           std::strtod(begin, &end);       break;
                default: return false;
            }
            if (end == begin || *end != '\0') return false;
            value = parsed;
            return true;
        }


        TextMeasurement MeasureTextInternal(std::string_view text, const interstice::StyleRef /*styleIndex*/,
                                            int32_t maxWidth = INT32_MAX) const
        {
            int32_t charWidth = m_defaultCharWidth;
            int32_t charsPerLine = std::max(1, maxWidth / charWidth);

            uint32_t lines = static_cast<uint32_t>((text.length() + charsPerLine - 1) / charsPerLine);

            int32_t widestLine = std::min<int32_t>(static_cast<int32_t>(text.length()), charsPerLine) * charWidth;
            int32_t height = static_cast<int32_t>(lines) * m_defaultLineHeight;

            return TextMeasurement{
                .minWidth = charWidth,
                .maxWidth = widestLine,
                .minHeight = m_defaultLineHeight,
                .maxHeight = height
            };
        }

        struct RegisteredText
        {
            ID elementId;
            std::string text;
            interstice::StyleRef styleIndex;
            interstice::ElementState state;
            TextMeasurement baseMeasurement;

            uint64_t GetHashCode() const
            {
                return GetHashCode(elementId, text, styleIndex, state);
            }

            static uint64_t GetHashCode(ID elementId, std::string_view text, StyleRef styleIndex, interstice::ElementState state)
            {
                return Hash::Combine(Hash::Calculate(elementId), Hash::Calculate(text),
                                     Hash::Calculate(styleIndex), Hash::Calculate(state));
            }
        };

        std::vector<RegisteredText> m_registry;
        std::unordered_map<uint64_t, TextID> m_elementIdToTextIdMap;
        std::unordered_map<uint64_t, TextID> m_elementTextIdMap;
        std::unordered_map<uint64_t, std::string> m_floatTextMap;
        std::unordered_map<uint64_t, std::string> m_intTextMap;

        struct PendingEdit
        {
            std::string inserts;
            uint32_t backspaces = 0;
            uint32_t deletes    = 0;
            bool     commit     = false;
        };
        std::unordered_map<uint64_t, PendingEdit> m_pendingEdits;

        struct IDHash { size_t operator()(ID id) const noexcept { return Hash::Calculate(id); } };
        std::unordered_set<ID, IDHash> m_editableElementIds;
        ID m_activeEditor = ID::Invalid();

        int32_t  m_defaultCharWidth;
        int32_t  m_defaultLineHeight;
        int32_t  m_defaultIconSize;
    };

} // namespace interstice
