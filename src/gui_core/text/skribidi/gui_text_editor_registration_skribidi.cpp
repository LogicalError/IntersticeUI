#include <cmath>

#include <gui_core/style/gui_style_manager.h>

#include <gui_core/state/gui_state_manager.h>

#include <gui_core/text/gui_text_atlas_manager.h>
#include <gui_core/text/gui_clipboard.h>

#include <gui_core/text/skribidi/gui_text_system_skribidi.h>
#include <gui_core/text/skribidi/internal/gui_skribidi_context.h>

#include <gui_core/graphics/gui_graphics_context.h>
#include <gui_core/graphics/gui_renderer.h>

#include <gui_core/windows/gui_os_window_manager.h>

#include <skribidi/skb_rich_layout.h>

// Editable text, rich text and number registration, plus text get/apply.

namespace interstice
{
    TextID SkribidiTextSystem::RegisterEditableText(ID elementId, std::string& text, TextEditBehavior behavior, interstice::StyleRef styleIndex, interstice::ElementState state, bool& wasModified)
    {
        if (!m_skribidiContext)
            return TextID::Invalid;

        // Track this element as a text editor
        m_textEditorElements.insert(Hash::Calculate(elementId));
        m_textEditorNumericModes[Hash::Calculate(elementId)] = NumericType::None;
        if ((behavior & TextEditBehavior::ReadOnly) != TextEditBehavior::None)
            m_readOnlyEditorElements.insert(Hash::Calculate(elementId));

        // Update active editor behavior if this is the focused editor
        if (m_skribidiContext->guiStateManager &&
            m_skribidiContext->guiStateManager->TextEditorFocus().GetFocusedID() == elementId)
        {
            m_skribidiContext->active_is_multiline = (behavior & TextEditBehavior::Multiline) != TextEditBehavior::None;
            m_skribidiContext->active_continuous_update = (behavior & TextEditBehavior::ContinuousUpdate) != TextEditBehavior::None;
            m_skribidiContext->active_accepts_tabs = (behavior & TextEditBehavior::AcceptTabs) != TextEditBehavior::None;
            m_skribidiContext->active_read_only = (behavior & TextEditBehavior::ReadOnly) != TextEditBehavior::None;
        }

        m_skribidiContext->text_edit_behavior = behavior;

        ApplyTextEditUpdate(elementId, text, wasModified);
        const TextID textId = RegisterText(elementId, IconID::Invalid, text, styleIndex, state);
        // The active editor follows its element's current text ID (it changes
        // when the text cache is cleared).
        if (m_skribidiContext->active_element_id == elementId)
            m_skribidiContext->active_text_id = textId;
        return textId;
    }

    TextID SkribidiTextSystem::RegisterEditableRichText(ID elementId, const std::span<ContentRun>& runs, TextEditBehavior behavior,
                                                        interstice::StyleRef styleIndex, interstice::ElementState state, bool& wasModified)
    {
        if (!m_skribidiContext)
            return TextID::Invalid;

        // Track this element as a (non-numeric) text editor, so it gets an editor for selection.
        m_textEditorElements.insert(Hash::Calculate(elementId));
        m_textEditorNumericModes[Hash::Calculate(elementId)] = NumericType::None;
        if ((behavior & TextEditBehavior::ReadOnly) != TextEditBehavior::None)
            m_readOnlyEditorElements.insert(Hash::Calculate(elementId));

        if (m_skribidiContext->guiStateManager &&
            m_skribidiContext->guiStateManager->TextEditorFocus().GetFocusedID() == elementId)
        {
            m_skribidiContext->active_is_multiline = (behavior & TextEditBehavior::Multiline) != TextEditBehavior::None;
            m_skribidiContext->active_continuous_update = (behavior & TextEditBehavior::ContinuousUpdate) != TextEditBehavior::None;
            m_skribidiContext->active_accepts_tabs = (behavior & TextEditBehavior::AcceptTabs) != TextEditBehavior::None;
            m_skribidiContext->active_read_only = (behavior & TextEditBehavior::ReadOnly) != TextEditBehavior::None;
        }
        m_skribidiContext->text_edit_behavior = behavior;

        // Reuse the rich-text path for the static (unfocused) layout, links and hit-testing.
        TextID textId = RegisterRichText(elementId, runs, styleIndex, state);

        // The editor's text is the concatenation of the runs' text (the selectable / copyable
        // content); record link runs as codepoint ranges into it for editor styling.
        if (auto* entry = m_skribidiContext->GetRegisteredText(textId))
            entry->text = build_editor_text(runs, entry->linkRanges);

        wasModified = false;
        return textId;
    }

    TextID SkribidiTextSystem::RegisterEditableNumber(ID elementId, NumericType type, NumericValue& value, TextEditBehavior behavior,
                                                      StyleRef styleIndex, ElementState state, bool& wasModified)
    {
        if (type == NumericType::None)
        {
            assert(false && "RegisterEditable called with NumericType::None. This likely means the type deduction failed. Check that GetNumericType is implemented for this type.");
            return TextID::Invalid;
        }

        const bool focused = m_skribidiContext && m_skribidiContext->guiStateManager &&
            (m_skribidiContext->guiStateManager->TextEditorFocus().GetFocusedID() == elementId);

        if (!focused)
        {
            const uint64_t elementKey = Hash::Calculate(elementId);
            auto& cachedDisplay = m_numericDisplayCache[elementKey];

            if (!cachedDisplay.isInitialized || 
                !EqualsNumericType(cachedDisplay.type, cachedDisplay.value, type, value))
            {
                cachedDisplay.type = type;
                cachedDisplay.value = value;
                cachedDisplay.text = format_to_text(type, value);
                cachedDisplay.isInitialized = true;
            }

            std::string text = cachedDisplay.text;
            TextID textId = RegisterEditableText(elementId, text, behavior, styleIndex, state, wasModified);
            m_textEditorNumericModes[elementKey] = type;

            // Text committed when focus left the field: parse it into the bound value.
            if (wasModified)
            {
                NumericValue parsed;
                wasModified = try_parse_text(text, type, parsed);
                if (wasModified) value = parsed;
            }
            return textId;
        }

        if (m_numericEditElementId != elementId || m_numericEditMode != type)
        {
            m_numericEditElementId = elementId;
            m_numericEditMode = type;

            // Default NaN/Infinity to 0 for editing - "nan"/"inf" aren't usable as starting text.
            bool isNonFinite = (type == NumericType::TypeFloat  && !std::isfinite(value.value_float))
                           || (type == NumericType::TypeDouble && !std::isfinite(value.value_double));
            if (isNonFinite)
            {
                if (type == NumericType::TypeFloat)  value.value_float  = 0.0f;
                if (type == NumericType::TypeDouble) value.value_double = 0.0;
            }

            m_numericParsedValueCache = value;
            m_numericEditText = format_to_text(type, value);
            m_numericParsedTextCache = m_numericEditText;
            m_numericHasParsedCache = true;
        }

        TextID textId = RegisterEditableText(elementId, m_numericEditText, behavior, styleIndex, state, wasModified);
        m_textEditorNumericModes[Hash::Calculate(elementId)] = type;

        if (m_numericHasParsedCache && 
            m_numericParsedTextCache == m_numericEditText)
        {
            assert(type == m_numericEditMode);
            if (type == m_numericEditMode) value = m_numericParsedValueCache;
            return textId;
        }

        NumericValue temp;
        if (try_parse_text(m_numericEditText, m_numericEditMode, temp))
        {
            assert(type == m_numericEditMode);
            if (type == m_numericEditMode)
            {
                m_numericParsedTextCache = m_numericEditText;
                m_numericParsedValueCache = temp;
                m_numericHasParsedCache = true;
                value = temp;
            }
        }

        return textId;
    }

    bool SkribidiTextSystem::ApplyTextEditUpdate(ID elementId, std::string& currentText, bool& wasModified)
    {
        if (!m_skribidiContext || elementId == ID::Invalid())
            return false;

        uint64_t key = Hash::Calculate(elementId);
        auto it = m_elementIdToTextIdMap.find(key);
        if (it == m_elementIdToTextIdMap.end())
            return false;

        // Text committed when focus left this editor (SetActiveTextEditor captured it).
        bool committedOnBlur = false;
        if (m_skribidiContext->pending_text_prev_id == it->second)
        {
            auto entry = m_skribidiContext->GetRegisteredText(it->second);
            if (entry != nullptr && entry->text != currentText)
            {
                currentText = entry->text;
                wasModified = committedOnBlur = true;
            }
            m_skribidiContext->pending_text_prev_id = TextID::Invalid;
        }

        std::string updatedText;
        auto update = ConsumeTextEditUpdate(it->second, updatedText);
        if (!update.HasUpdate())
            return committedOnBlur;
        
        m_skribidiContext->ResetLayout(it->second);

        bool continuousUpdate = (m_skribidiContext->text_edit_behavior & TextEditBehavior::ContinuousUpdate) != TextEditBehavior::None;
        bool shouldUpdate = update.textCommitted || (continuousUpdate && update.textChanged);
        if (!shouldUpdate)
            return committedOnBlur;

        UpdateText(it->second, updatedText);

        currentText = std::move(updatedText);
        wasModified = true;
        return true;
    }

    std::string_view SkribidiTextSystem::GetText(TextID textId) const
    {
        if (!m_skribidiContext) return "";
        auto registeredText = m_skribidiContext->GetRegisteredText(textId);
        if (registeredText == nullptr) return "";
        return registeredText->text;
    }

    TextID SkribidiTextSystem::GetTextID(ID elementId) const
    {
        auto it = m_elementIdToTextIdMap.find(Hash::Calculate(elementId));
        return it != m_elementIdToTextIdMap.end() ? it->second : TextID::Invalid;
    }


}
