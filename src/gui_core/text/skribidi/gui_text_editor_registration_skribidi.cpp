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

namespace tachyon
{
    TextID SkribidiTextSystem::RegisterEditableText(ID elementId, std::string& text, TextEditBehavior behavior, tachyon::StyleRef styleIndex, tachyon::ElementState state, bool& wasModified,
                                                    const ITextValidator* validator)
    {
        if (!m_skribidiContext)
            return TextID::Invalid;

        // Track this element as a text editor
        m_textEditorElements.insert(Hash::Calculate(elementId));
        m_textEditorValidators[Hash::Calculate(elementId)] = validator;
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
            m_skribidiContext->active_validator = validator;
        }

        m_skribidiContext->text_edit_behavior = behavior;

        ApplyTextEditUpdate(elementId, text, wasModified);
        const TextID textId = RegisterText(elementId, IconID::Invalid, text, styleIndex, state);
        // The active editor follows its element's current text ID (it changes
        // when the text cache is cleared).
        if (ActiveElementID() == elementId)
            m_skribidiContext->active_text_id = textId;
        return textId;
    }

    TextID SkribidiTextSystem::RegisterEditableRichText(ID elementId, const std::span<ContentRun>& runs, TextEditBehavior behavior,
                                                        tachyon::StyleRef styleIndex, tachyon::ElementState state, bool& wasModified)
    {
        if (!m_skribidiContext)
            return TextID::Invalid;

        // Track this element as a (non-numeric) text editor, so it gets an editor for selection.
        m_textEditorElements.insert(Hash::Calculate(elementId));
        m_textEditorValidators[Hash::Calculate(elementId)] = nullptr;
        if ((behavior & TextEditBehavior::ReadOnly) != TextEditBehavior::None)
            m_readOnlyEditorElements.insert(Hash::Calculate(elementId));

        if (m_skribidiContext->guiStateManager &&
            m_skribidiContext->guiStateManager->TextEditorFocus().GetFocusedID() == elementId)
        {
            m_skribidiContext->active_is_multiline = (behavior & TextEditBehavior::Multiline) != TextEditBehavior::None;
            m_skribidiContext->active_continuous_update = (behavior & TextEditBehavior::ContinuousUpdate) != TextEditBehavior::None;
            m_skribidiContext->active_accepts_tabs = (behavior & TextEditBehavior::AcceptTabs) != TextEditBehavior::None;
            m_skribidiContext->active_read_only = (behavior & TextEditBehavior::ReadOnly) != TextEditBehavior::None;
            m_skribidiContext->active_validator = nullptr;
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

    std::string* SkribidiTextSystem::ValueEditText(ID elementId, bool& started)
    {
        started = false;
        if (!m_skribidiContext || !m_skribidiContext->guiStateManager || elementId == ID::Invalid() ||
            m_skribidiContext->guiStateManager->TextEditorFocus().GetFocusedID() != elementId)
            return nullptr;
        if (m_valueEditElementId != elementId)
        {
            m_valueEditElementId = elementId;
            m_valueEditText.clear();
            started = true;
        }
        return &m_valueEditText;
    }

    bool SkribidiTextSystem::ApplyTextEditUpdate(ID elementId, std::string& currentText, bool& wasModified)
    {
        if (!m_skribidiContext || elementId == ID::Invalid())
            return false;

        uint64_t key = Hash::Calculate(elementId);
        auto it = m_elementIdToTextIdMap.find(key);
        if (it == m_elementIdToTextIdMap.end())
            return false;

        // Text an undo, redo or drop set: it replaces the field's text and any edit in progress.
        if (auto external = m_externalText.find(key); external != m_externalText.end())
        {
            std::string text = std::move(external->second);
            m_externalText.erase(external);
            std::string discarded;
            (void)ConsumeTextEditUpdate(it->second, discarded);
            if (m_skribidiContext->pending_text_prev_id == it->second)
                m_skribidiContext->pending_text_prev_id = TextID::Invalid;
            UpdateText(it->second, text);
            m_skribidiContext->ResetLayout(it->second);
            // A focused field's editor still shows the old text: let it be made anew.
            if (m_skribidiContext->editor != nullptr && m_skribidiContext->editing_text_id == it->second
                && EditorText(m_skribidiContext->editor) != text)
            {
                skb_editor_destroy(m_skribidiContext->editor);
                m_skribidiContext->editor = nullptr;
                m_skribidiContext->editing_text_id = TextID::Invalid;
            }
            const bool changed = currentText != text;
            currentText = std::move(text);
            wasModified = changed;
            return changed;
        }

        // Text committed when focus left this editor (SwitchTextEditor captured it).
        bool committedOnBlur = false;
        if (m_skribidiContext->pending_text_prev_id == it->second)
        {
            auto entry = m_skribidiContext->GetRegisteredText(it->second);
            if (entry != nullptr && entry->text != currentText)
            {
                PushTextEdit(elementId, currentText, entry->text);
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

        PushTextEdit(elementId, currentText, updatedText);
        currentText = std::move(updatedText);
        wasModified = true;
        return true;
    }

    namespace
    {
        bool IsContinuationByte(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }

        int32_t CodepointCount(std::string_view text)
        {
            int32_t count = 0;
            for (char c : text)
                count += IsContinuationByte(c) ? 0 : 1;
            return count;
        }

        // The part of `to` that differs from `from` (what they share at the start and the end
        // left out), as codepoint offsets into `to`.
        EditSelection ChangedSpan(std::string_view from, std::string_view to)
        {
            const size_t shorter = std::min(from.size(), to.size());
            size_t prefix = 0;
            while (prefix < shorter && from[prefix] == to[prefix])
                prefix++;
            while (prefix > 0 && prefix < to.size() && IsContinuationByte(to[prefix]))
                prefix--;
            size_t suffix = 0;
            while (suffix < shorter - prefix && from[from.size() - 1 - suffix] == to[to.size() - 1 - suffix])
                suffix++;
            while (suffix > 0 && IsContinuationByte(to[to.size() - suffix]))
                suffix--;
            return { CodepointCount(to.substr(0, prefix)), CodepointCount(to.substr(0, to.size() - suffix)) };
        }
    }

    void SkribidiTextSystem::PushTextEdit(ID elementId, const std::string& before, const std::string& after)
    {
        if (!m_undoHistory || before == after)
            return;

        // The selections the field had before and after the edit. Without them (an edit made
        // some other way), from the text: the text an undo brings back selected, the caret
        // after what a redo changes.
        EditSelection selectionBefore = ChangedSpan(after, before);
        EditSelection selectionAfter  = ChangedSpan(before, after);
        selectionAfter.anchor = selectionAfter.caret;
        SkribidiContext* ctx = m_skribidiContext;
        if (ctx->edit_selection_element == elementId)
        {
            selectionBefore = ctx->edit_selection_before;
            const bool editing = ctx->editor != nullptr && ctx->editing_text_id == GetTextID(elementId);
            selectionAfter = editing ? EditorSelection(ctx->editor) : ctx->edit_selection_after;
        }

        const uint64_t key = Hash::Calculate(elementId);
        m_undoHistory->Push({ .description = "Edit text",
                              .undo = [this, key, elementId, before, selectionBefore] { m_externalText[key] = before; FocusChangedField(elementId, selectionBefore); },
                              .redo = [this, key, elementId, after, selectionAfter]   { m_externalText[key] = after;  FocusChangedField(elementId, selectionAfter); },
                              .mergeKey = key });
    }

    void SkribidiTextSystem::FocusChangedField(ID elementId, EditSelection selection)
    {
        if (!m_skribidiContext || !m_skribidiContext->guiStateManager)
            return;
        m_skribidiContext->pending_selection_element = elementId;
        m_skribidiContext->pending_selection         = selection;
        auto& textFocus = m_skribidiContext->guiStateManager->TextEditorFocus();
        if (textFocus.GetPrevFocusedID() != elementId)
            m_skipNextSelectAllId = elementId;   // the switch to it would select all
        m_skribidiContext->guiStateManager->Interaction().SetFocusID(elementId);
        textFocus.Set(elementId);
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
