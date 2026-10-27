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


#undef SKB_CURRENT_SELECTION
#define SKB_CURRENT_SELECTION \
    skb_text_range_t { .start = {.offset = INT32_MIN, .affinity = SKB_AFFINITY_NONE}, .end = {.offset = INT32_MIN, .affinity = SKB_AFFINITY_NONE} }

#undef SKB_CURRENT_SELECTION_END
#define SKB_CURRENT_SELECTION_END \
    skb_text_position_t { .offset = INT32_MIN, .affinity = SKB_AFFINITY_NONE }
namespace tachyon
{
    struct selection_hit_test_context_t
    {
        float2 localPos = { 0.0f, 0.0f };
        bool hit = false;
    };

    static void hit_test_selection_rect(skb_rect2_t skb_rect, void* context)
    {
        auto* hitCtx = static_cast<selection_hit_test_context_t*>(context);
        if (hitCtx == nullptr || hitCtx->hit)
            return;

        const float x0 = skb_rect.x;
        const float y0 = skb_rect.y;
        const float x1 = x0 + skb_rect.width;
        const float y1 = y0 + skb_rect.height;

        if (hitCtx->localPos.x >= x0 && hitCtx->localPos.x < x1 &&
            hitCtx->localPos.y >= y0 && hitCtx->localPos.y < y1)
        {
            hitCtx->hit = true;
        }
    }

    void SkribidiTextSystem::OnKeyboard(ID /*windowId*/, tachyon::KeyCode key, int /*repeat*/, KeyModifier modifiers, KeyAction action)
    {
        if (!m_skribidiContext)
            return;
        if (!m_skribidiContext->editor)
            return;
        if (m_skribidiContext->active_text_id != m_skribidiContext->editing_text_id)
            return;

        // Read-only editors (selectable rich text) allow navigation, select-all and copy, but
        // block every mutation: typed characters, backspace/delete, enter, paste, cut and undo.
        const bool readOnly = m_skribidiContext->active_read_only;

        const ITextValidator* validator = m_skribidiContext->active_validator;
        if (!readOnly && action != KeyAction::KeyUp)
            NoteSelectionBeforeEdit();

        if (action == KeyAction::KeyChar)
        {
            if (readOnly)
                return;
            // Committed text replaces any composition preview still showing.
            ClearComposition();
            unsigned int codepoint = static_cast<unsigned int>(key);
            // Only insert printable characters (skip control characters)
            if (codepoint < 32)
            {
                // Allow tab only if the editor accepts tabs
                if (codepoint == '\t' && !m_skribidiContext->active_accepts_tabs)
                    return;
                if (codepoint != '\t')
                    return;
            }

            if (validator && !validator->AcceptsCharacter(codepoint))
                return;

            skb_editor_insert_codepoint(m_skribidiContext->editor, m_skribidiContext->temp_alloc, SKB_CURRENT_SELECTION, codepoint);
            EnsureCursorVisible(ActiveElementID());
        } else
        {
            // Only process key down events
            if (action != tachyon::KeyAction::KeyDown)
                return;

            if (key == tachyon::KeyCode::Escape && m_textDragArmed)
            {
                EndTextDragging();
                return;
            }

            if (key == tachyon::KeyCode::Return || key == tachyon::KeyCode::Return2)
            {
                if (!m_skribidiContext->active_is_multiline)
                {
                    m_skribidiContext->pending_text_id = m_skribidiContext->editing_text_id;
                    m_skribidiContext->pending_text_committed = true;
                    m_skribidiContext->pending_text_valid = false;
                    return;
                }
            }

            // Convert key codes to skribidi key codes and modifiers
            skb_editor_key_t skb_key = SKB_KEY_NONE;
            uint32_t skb_modifiers = SKB_MOD_NONE;

            // Map modifiers
            if ((modifiers & KeyModifier::Alt) != KeyModifier::None) skb_modifiers |= SKB_MOD_ALT;
            if ((modifiers & KeyModifier::Shift) != KeyModifier::None) skb_modifiers |= SKB_MOD_SHIFT;
            if ((modifiers & KeyModifier::Control) != KeyModifier::None) skb_modifiers |= SKB_MOD_CONTROL;
            if ((modifiers & KeyModifier::Option) != KeyModifier::None) skb_modifiers |= SKB_MOD_OPTION;
            if ((modifiers & KeyModifier::Command) != KeyModifier::None) skb_modifiers |= SKB_MOD_COMMAND;

            // Map keys
            switch (key)
            {
                case tachyon::KeyCode::Return:
                case tachyon::KeyCode::Return2:        skb_key = SKB_KEY_ENTER; break;
                case tachyon::KeyCode::Left:        skb_key = SKB_KEY_LEFT; break;
                case tachyon::KeyCode::Right:        skb_key = SKB_KEY_RIGHT; break;
                case tachyon::KeyCode::Up:            skb_key = SKB_KEY_UP; break;
                case tachyon::KeyCode::Down:        skb_key = SKB_KEY_DOWN; break;
                case tachyon::KeyCode::Home:        skb_key = SKB_KEY_HOME; break;
                case tachyon::KeyCode::End:            skb_key = SKB_KEY_END; break;
                case tachyon::KeyCode::Backspace:
                {
                    skb_key = SKB_KEY_BACKSPACE;
                    const skb_text_range_t selection = skb_editor_get_current_selection(m_skribidiContext->editor);
                    if (selection.start.offset == selection.end.offset &&
                        selection.start.affinity == selection.end.affinity)
                    {
                        int32_t textOffset = skb_editor_get_text_offset_from_text_position(m_skribidiContext->editor, selection.start);
                        if (textOffset == 0) return;
                    }
                    break;
                }
                case tachyon::KeyCode::Delete:
                {
                    skb_key = SKB_KEY_DELETE;
                    const skb_text_range_t selection = skb_editor_get_current_selection(m_skribidiContext->editor);
                    if (selection.start.offset == selection.end.offset && 
                        selection.start.affinity == selection.end.affinity)
                    {
                        int32_t textOffset = skb_editor_get_text_offset_from_text_position(m_skribidiContext->editor, selection.start);
                        int32_t textLength = skb_editor_get_text_utf8_count(m_skribidiContext->editor);
                        if (textOffset >= textLength) return;
                    }
                    break;
                }
                case tachyon::KeyCode::KeyV:
                    if (readOnly) return;
                    if (skb_modifiers & SKB_MOD_CONTROL)
                    {
                        if (m_skribidiContext->clipboard != nullptr)
                            Paste(m_skribidiContext->clipboard->Get());
                    }
                    return;
                case tachyon::KeyCode::KeyC:
                    if (skb_modifiers & SKB_MOD_CONTROL)
                    {
                        std::string copiedText = Copy();
                        if (!copiedText.empty() && m_skribidiContext->clipboard != nullptr)
                        {
                            m_skribidiContext->clipboard->Set(copiedText);
                        }
                    }
                    return;
                case tachyon::KeyCode::KeyX:
                    if (readOnly) return;
                    if (skb_modifiers & SKB_MOD_CONTROL)
                    {
                        std::string cutText = Cut();
                        if (!cutText.empty() && m_skribidiContext->clipboard != nullptr)
                        {
                            m_skribidiContext->clipboard->Set(cutText);
                        }
                    }
                    return;
                case tachyon::KeyCode::KeyA:
                    if (skb_modifiers & SKB_MOD_CONTROL)
                    {
                        skb_editor_select_all(m_skribidiContext->editor);
                        EnsureCursorVisible(ActiveElementID());
                    }
                    return;
                case tachyon::KeyCode::KeyZ:
                case tachyon::KeyCode::KeyY:
                    if (readOnly) return;
                    if (skb_modifiers & SKB_MOD_CONTROL)
                    {
                        // The field's own typing first; past that, the app's history (e.g. a
                        // text move between fields).
                        const bool redo = key == tachyon::KeyCode::KeyY || (skb_modifiers & SKB_MOD_SHIFT);
                        if (redo)
                        {
                            if (skb_editor_can_redo(m_skribidiContext->editor))
                                skb_editor_redo(m_skribidiContext->editor, m_skribidiContext->temp_alloc);
                            else if (m_undoHistory)
                                m_undoHistory->Redo();
                        } else
                        {
                            if (skb_editor_can_undo(m_skribidiContext->editor))
                                skb_editor_undo(m_skribidiContext->editor, m_skribidiContext->temp_alloc);
                            else if (m_undoHistory)
                                m_undoHistory->Undo();
                        }
                        EnsureCursorVisible(ActiveElementID());
                    }
                    return;
                default:
                    return;
            }

            if (skb_key != SKB_KEY_NONE)
            {
                // Enter/backspace/delete mutate the text - block them for read-only editors,
                // but let caret movement (arrows, home/end, with or without shift) through.
                const bool mutatingKey = (skb_key == SKB_KEY_ENTER || skb_key == SKB_KEY_BACKSPACE || skb_key == SKB_KEY_DELETE);
                if (readOnly && mutatingKey)
                    return;
                skb_editor_process_key_pressed(m_skribidiContext->editor, m_skribidiContext->temp_alloc, skb_key, skb_modifiers);
                EnsureCursorVisible(ActiveElementID());
            }
        }
    }

    void SkribidiTextSystem::OnMouseMove(ID windowId, tachyon::int2 screenPos, MouseButtons /*button*/, KeyModifier /*modifiers*/)
    {
        if (!m_skribidiContext)
            return;
        if (!m_skribidiContext->editor)
            return;
        if (m_skribidiContext->active_text_id != m_skribidiContext->editing_text_id)
            return;

        if (m_textDragArmed && !m_skribidiContext->drag_text)
        {
            const int32_t dragThreshold = int32_t(m_skribidiContext->Px(float(kDragStartDistance)));
            const int32_t dx = screenPos.x - m_textDragStartScreenPos.x;
            const int32_t dy = screenPos.y - m_textDragStartScreenPos.y;
            if ((dx * dx + dy * dy) >= (dragThreshold * dragThreshold))
            {
                m_skribidiContext->drag_text = true;
            }
        }

        if (!m_skribidiContext->editor_drag_active)
            return;

        // Mouse position is in screen space
        tachyon::float2 localPos{ static_cast<float>(screenPos.x), static_cast<float>(screenPos.y) };
        if (!TryGetActiveTextEditorLocalPoint(windowId, screenPos, localPos))
        {
            localPos.x -= m_skribidiContext->view.cx;
            localPos.y -= m_skribidiContext->view.cy;
        }

        skb_editor_process_mouse_drag(m_skribidiContext->editor, localPos.x, localPos.y);
        if (ActiveElementID() != ID::Invalid())
            EnsureCursorVisible(ActiveElementID());
    }

    bool SkribidiTextSystem::IsHoveringSelectedText(ID textEditorId, ID windowId, tachyon::int2 screenSpace) const
    {
        if (!m_skribidiContext || !m_skribidiContext->editor)
            return false;
        if (textEditorId == ID::Invalid())
            return false;
        if (textEditorId != ActiveElementID())
            return false;
        if (m_skribidiContext->active_text_id != m_skribidiContext->editing_text_id)
            return false;

        const skb_text_range_t selection = skb_editor_get_current_selection(m_skribidiContext->editor);
        if (skb_editor_get_text_range_count(m_skribidiContext->editor, selection) <= 0)
            return false;

        tachyon::float2 localPos{ static_cast<float>(screenSpace.x), static_cast<float>(screenSpace.y) };
        if (!TryGetActiveTextEditorLocalPoint(windowId, screenSpace, localPos))
            return false;

        selection_hit_test_context_t hitContext;
        hitContext.localPos = localPos;

        const skb_rich_layout_t* richLayout = skb_editor_get_rich_layout(m_skribidiContext->editor);
        if (richLayout == nullptr)
            return false;

        skb_rich_layout_get_text_range_bounds(richLayout, selection, hit_test_selection_rect, &hitContext);
        return hitContext.hit;
    }

    bool SkribidiTextSystem::TryGetTextDragHoverTarget(ID windowId, tachyon::int2 screenSpace, ID& outTargetId) const
    {
        if (!m_skribidiContext)
            return false;
        if (!m_skribidiContext->drag_text)
            return false;
        if (m_textDragSourceId == ID::Invalid())
            return false;

        const ID targetTextEditorId = FindTextEditorAtScreenPoint(windowId, screenSpace);
        bool canDrop = targetTextEditorId != ID::Invalid() && CanDropDraggedTextInto(targetTextEditorId);
        if (canDrop && targetTextEditorId == m_textDragSourceId &&
            IsHoveringSelectedText(targetTextEditorId, windowId, screenSpace))
        {
            canDrop = false;
        }

        outTargetId = canDrop ? targetTextEditorId : ID::Invalid();
        return true;
    }

    bool SkribidiTextSystem::TryGetTextDragCursor(ID windowId, tachyon::int2 screenSpace, MouseCursor& outCursor) const
    {
        ID targetId;
        if (!TryGetTextDragHoverTarget(windowId, screenSpace, targetId))
            return false;
        outCursor = (targetId != ID::Invalid()) ? MouseCursor::Default : MouseCursor::NotAllowed;
        return true;
    }

    bool SkribidiTextSystem::TryGetActiveTextEditorLocalPoint(ID windowId, tachyon::int2 screenSpace, tachyon::float2& outLocal) const
    {
        if (!m_skribidiContext)
            return false;
        if (ActiveElementID() == ID::Invalid())
            return false;

        return TryGetTextEditorLocalPoint(ActiveElementID(), windowId, screenSpace, outLocal);
    }

    bool SkribidiTextSystem::IsActiveEditorPosOnLink(float localX, float localY) const
    {
        if (!m_skribidiContext || !m_skribidiContext->editor)
            return false;
        RegisteredText* entry = m_skribidiContext->GetRegisteredText(m_skribidiContext->active_text_id);
        if (!entry || entry->linkRanges.empty())
            return false;
        const skb_text_position_t pos = skb_editor_hit_test(m_skribidiContext->editor, SKB_MOVEMENT_CARET, localX, localY);
        const int32_t offset = skb_editor_get_text_offset_from_text_position(m_skribidiContext->editor, pos);
        for (const auto& lr : entry->linkRanges)
            if (offset >= lr.start && offset < lr.end)
                return true;
        return false;
    }

    bool SkribidiTextSystem::TryGetTextEditorLocalPoint(ID textEditorId, ID windowId, tachyon::int2 screenSpace, tachyon::float2& outLocal) const
    {
        if (!m_skribidiContext || textEditorId == ID::Invalid())
            return false;

        auto textIt = m_elementIdToTextIdMap.find(Hash::Calculate(textEditorId));
        if (textIt == m_elementIdToTextIdMap.end())
            return false;

        const RegisteredText* entry = m_skribidiContext->GetRegisteredText(textIt->second);
        if (!entry)
            return false;

        // entry->innerRect and screenToLayout are stored at render time, so the
        // OS window manager isn't needed to look up the window origin.
        (void)windowId;
        IntRect textArea = entry->innerRect;
        textArea.y += entry->editorOffsetY;
        ScreenToTextEditorLocalPoint(screenSpace, textArea, outLocal, entry->screenToLayout);
        return true;
    }

    ID SkribidiTextSystem::FindTextEditorAtScreenPoint(ID windowId, tachyon::int2 screenSpace) const
    {
        if (!m_skribidiContext)
            return ID::Invalid();

        // Bounds-test each editor's innerRect against the point mapped into its
        // layout space. The windowId is unused: no OS window manager lookup needed.
        (void)windowId;

        // Walk registered text editors in order; last match wins (rendered on top).
        ID result = ID::Invalid();
        for (const RegisteredText& entry : m_skribidiContext->m_registry)
        {
            if (entry.textId == TextID::Invalid || !IsTextEditor(entry.elementId))   // free slot
                continue;
            const IntRect& r = entry.innerRect;
            const float2   p = entry.screenToLayout.TransformPoint(float2{ float(screenSpace.x), float(screenSpace.y) });
            if (p.x >= float(r.x) && p.x < float(r.x + r.w) &&
                p.y >= float(r.y) && p.y < float(r.y + r.h))
            {
                result = entry.elementId;
            }
        }
        return result;
    }

    void SkribidiTextSystem::EndTextDragging()
    {
        m_textDragSourceId = ID::Invalid();
        m_textDragPayload.clear();
        m_textDragArmed = false;
        m_textDragStartScreenPos = int2 { 0, 0 };
        m_skribidiContext->drag_text = false;
        m_skribidiContext->editor_drag_active = false;
    }

    bool SkribidiTextSystem::TryGetCaretPositionInternal(ID windowId, tachyon::int2 screenPos, ID targetTextEditorId, skb_editor_t*& outTargetEditor, skb_text_position_t& outPosition)
    {
        outPosition = {};

        auto textIt = m_elementIdToTextIdMap.find(Hash::Calculate(targetTextEditorId));
        if (textIt == m_elementIdToTextIdMap.end())
            return false;
        
        RegisteredText* targetEntry = m_skribidiContext->GetRegisteredText(textIt->second);
        if (!targetEntry)
            return false;

        const float targetWidth = static_cast<float>(std::max(1, targetEntry->innerRect.w));
        outTargetEditor = static_cast<skb_editor_t*>(UpdateEditorWhenNecessary(targetEntry, targetWidth));
        if (!outTargetEditor)
            return false;
        
        tachyon::float2 localPos{ static_cast<float>(screenPos.x), static_cast<float>(screenPos.y) };
        if (!TryGetTextEditorLocalPoint(targetTextEditorId, windowId, screenPos, localPos))
            return false;
        
        outPosition = skb_editor_hit_test(outTargetEditor, SKB_MOVEMENT_CARET, localPos.x, localPos.y);
        return true;
    }

    // The dropped text ends up selected: from the drop point to where the paste left the caret.
    void SkribidiTextSystem::SelectFromDropPoint(skb_editor_t* editor, skb_text_position_t dropPoint)
    {
        const skb_text_range_t afterPaste = skb_editor_get_current_selection(editor);
        skb_editor_select(editor, skb_text_range_t { .start = dropPoint, .end = afterPaste.end });
    }

    void SkribidiTextSystem::DropDraggedText(ID windowId, tachyon::int2 screenPos, ID targetTextEditorId)
    {
        std::string movedText;
        bool droppedOnOwnTextbox = (targetTextEditorId == m_textDragSourceId);
        if (!droppedOnOwnTextbox)
        {
            // One undo step covers both fields: their text before and after the move.
            const ID sourceId = m_textDragSourceId;
            const std::string sourceBefore = m_skribidiContext->editor ? EditorText(m_skribidiContext->editor) : std::string{};
            const EditSelection sourceSelection = m_skribidiContext->editor ? EditorSelection(m_skribidiContext->editor) : EditSelection{};
            movedText = Cut();
            if (movedText.empty())
            {
                movedText = m_textDragPayload;
            }
            const std::string sourceAfter = m_skribidiContext->editor ? EditorText(m_skribidiContext->editor) : sourceBefore;
            // The cut is the source's text now: leaving it isn't an edit of its own.
            if (const TextID sourceText = GetTextID(sourceId); sourceText != TextID::Invalid)
                UpdateText(sourceText, sourceAfter);
            m_externalText[Hash::Calculate(sourceId)] = sourceAfter;

            // The target takes the focus now, to paste into; switched here, so the input
            // manager sees no further change.
            if (m_skribidiContext->guiStateManager)
            {
                m_skribidiContext->guiStateManager->Interaction().SetFocusID(targetTextEditorId);
                m_skribidiContext->guiStateManager->TextEditorFocus().Set(targetTextEditorId);
            }
            SwitchTextEditor(m_textDragSourceId, targetTextEditorId);
            if (m_skribidiContext->guiStateManager)
                m_skribidiContext->guiStateManager->TextEditorFocus().Commit();

            skb_text_position_t dropCaretPos {};
            skb_editor_t* targetEditor;
            bool hasDropCaretPos = TryGetCaretPositionInternal(windowId, screenPos, targetTextEditorId, targetEditor, dropCaretPos);
            if (hasDropCaretPos)
            {
                skb_editor_select(targetEditor, skb_text_range_t { .start = dropCaretPos, .end = dropCaretPos });
            }

            const TextID targetText = GetTextID(targetTextEditorId);
            const RegisteredText* targetEntry = m_skribidiContext->GetRegisteredText(targetText);
            const std::string targetBefore = targetEntry ? targetEntry->text : std::string{};
            if (!movedText.empty())
            {
                m_skipNextSelectAllId = targetTextEditorId;
                Paste(movedText);
                if (hasDropCaretPos)
                    SelectFromDropPoint(targetEditor, dropCaretPos);
            }

            if (hasDropCaretPos)
            {
                // The paste is the target's text now, without a step in its own history (Ctrl+Z
                // there undoes the whole move).
                const std::string targetAfter = EditorText(targetEditor);
                const skb_text_range_t caret = skb_editor_get_current_selection(targetEditor);
                skb_editor_set_text_utf8(targetEditor, m_skribidiContext->temp_alloc, targetAfter.data(), int32_t(targetAfter.size()));
                skb_editor_select(targetEditor, caret);
                if (targetText != TextID::Invalid)
                    UpdateText(targetText, targetAfter);
                const uint64_t sourceKey = Hash::Calculate(sourceId), targetKey = Hash::Calculate(targetTextEditorId);
                m_externalText[targetKey] = targetAfter;
                if (m_undoHistory && (sourceBefore != sourceAfter || targetBefore != targetAfter))
                {
                    // Undo gives the focus back to the source with the dragged text selected, as
                    // the drag began; redo to the target with the caret where the drop left it.
                    const EditSelection targetSelection = EditorSelection(targetEditor);
                    m_undoHistory->EndMerge();
                    m_undoHistory->Push({ .description = "Move text",
                        .undo = [this, sourceKey, targetKey, sourceId, sourceBefore, targetBefore, sourceSelection]
                            { m_externalText[sourceKey] = sourceBefore; m_externalText[targetKey] = targetBefore;
                              FocusChangedField(sourceId, sourceSelection); },
                        .redo = [this, sourceKey, targetKey, targetId = targetTextEditorId, sourceAfter, targetAfter, targetSelection]
                            { m_externalText[sourceKey] = sourceAfter;  m_externalText[targetKey] = targetAfter;
                              FocusChangedField(targetId, targetSelection); } });
                    m_undoHistory->EndMerge();
                }
            }
        } else
        {
            skb_text_position_t dropCaretPos {};
            skb_editor_t* targetEditor;
            bool hasDropCaretPos = TryGetCaretPositionInternal(windowId, screenPos, targetTextEditorId, targetEditor, dropCaretPos);
            if (!hasDropCaretPos)
                return;
            
            const skb_text_range_t selection = skb_editor_get_current_selection(m_skribidiContext->editor);
            const int32_t selectionStart = skb_editor_get_text_offset_from_text_position(m_skribidiContext->editor, selection.start);
            const int32_t selectionEnd   = skb_editor_get_text_offset_from_text_position(m_skribidiContext->editor, selection.end);
            const int32_t dropOffset     = skb_editor_get_text_offset_from_text_position(m_skribidiContext->editor, dropCaretPos);

            dropCaretPos.offset = drop_offset_after_cut(dropOffset, selectionStart, selectionEnd);
            dropCaretPos.affinity = SKB_AFFINITY_NONE;

            movedText = Cut();
            if (movedText.empty()) { movedText = m_textDragPayload; }

            skb_editor_select(targetEditor, skb_text_range_t { .start = dropCaretPos, .end = dropCaretPos });

            if (!movedText.empty())
            {
                m_skipNextSelectAllId = targetTextEditorId;
                Paste(movedText);
                SelectFromDropPoint(targetEditor, dropCaretPos);
            }
        }

        EnsureCursorVisible(targetTextEditorId);
    }

    void SkribidiTextSystem::OnMouseButton(ID windowId, tachyon::int2 screenPos, MouseButtons button, KeyModifier modifiers, MouseAction action)
    {
        if (!m_skribidiContext)
            return;

        if (action == tachyon::MouseAction::MouseUp &&
            (!m_skribidiContext->editor || m_skribidiContext->active_text_id != m_skribidiContext->editing_text_id))
        {
            m_textDragSourceId = ID::Invalid();
            m_textDragPayload.clear();
            m_textDragArmed = false;
            m_textDragStartScreenPos = int2 { 0, 0 };
            m_skribidiContext->drag_text = false;
            m_skribidiContext->editor_drag_active = false;
            return;
        }

        if (!m_skribidiContext->editor ||
            m_skribidiContext->active_text_id != m_skribidiContext->editing_text_id)
            return;

        if (action == tachyon::MouseAction::MouseUp)
        {
            if (m_textDragArmed && !m_skribidiContext->drag_text)
            {
                tachyon::float2 localPos{ static_cast<float>(screenPos.x), static_cast<float>(screenPos.y) };
                if (TryGetActiveTextEditorLocalPoint(windowId, screenPos, localPos))
                {
                    const skb_text_position_t caretPos = skb_editor_hit_test(m_skribidiContext->editor, SKB_MOVEMENT_CARET, localPos.x, localPos.y);
                    skb_editor_select(m_skribidiContext->editor, skb_text_range_t { .start = caretPos, .end = caretPos });
                    EnsureCursorVisible(ActiveElementID());
                }

                m_textDragSourceId = ID::Invalid();
                m_textDragPayload.clear();
                m_textDragArmed = false;
                m_textDragStartScreenPos = int2 { 0, 0 };
                m_skribidiContext->drag_text = false;
                m_skribidiContext->editor_drag_active = false;
                return;
            }

            if (m_textDragSourceId != ID::Invalid() && !m_textDragPayload.empty())
            {
                const ID targetTextEditorId = FindTextEditorAtScreenPoint(windowId, screenPos);
                if (targetTextEditorId != ID::Invalid() && CanDropDraggedTextInto(targetTextEditorId))
                {
                    const bool droppedOnOwnSelection = (targetTextEditorId == m_textDragSourceId) &&
                        IsHoveringSelectedText(targetTextEditorId, windowId, screenPos);

                    if (!droppedOnOwnSelection)
                    {
                        DropDraggedText(windowId, screenPos, targetTextEditorId);
                    }
                }
            }
            EndTextDragging();
            return;
        }

        if (button != MouseButtons::Left)
            return;

        uint32_t skb_modifiers = SKB_MOD_NONE;
        if ((modifiers & KeyModifier::Alt) != KeyModifier::None) skb_modifiers |= SKB_MOD_ALT;
        if ((modifiers & KeyModifier::Shift) != KeyModifier::None) skb_modifiers |= SKB_MOD_SHIFT;
        if ((modifiers & KeyModifier::Control) != KeyModifier::None) skb_modifiers |= SKB_MOD_CONTROL;
        if ((modifiers & KeyModifier::Option) != KeyModifier::None) skb_modifiers |= SKB_MOD_OPTION;
        if ((modifiers & KeyModifier::Command) != KeyModifier::None) skb_modifiers |= SKB_MOD_COMMAND;

        if (action == tachyon::MouseAction::MouseDown)
        {
            // Read-only editors never drag their text - pressing (even on selected text) starts
            // a new selection, so click-drag always selects instead of moving text.
            if (!m_skribidiContext->active_read_only &&
                IsHoveringSelectedText(ActiveElementID(), windowId, screenPos))
            {
                m_textDragSourceId = ActiveElementID();
                m_textDragPayload = Copy();
                m_textDragArmed = !m_textDragPayload.empty();
                m_textDragStartScreenPos = screenPos;
                m_skribidiContext->drag_text = false;
                m_skribidiContext->editor_drag_active = false;
                return;
            }

            m_textDragArmed = false;
            m_skribidiContext->drag_text = false;

            tachyon::float2 localPos{ static_cast<float>(screenPos.x), static_cast<float>(screenPos.y) };
            if (!TryGetActiveTextEditorLocalPoint(windowId, screenPos, localPos))
            {
                localPos.x -= m_skribidiContext->view.cx;
                localPos.y -= m_skribidiContext->view.cy;
            }

            // Read-only: a press on a link activates the link (handled by the control), it must
            // NOT move the caret - skip the click so the caret/selection stays put.
            if (m_skribidiContext->active_read_only && IsActiveEditorPosOnLink(localPos.x, localPos.y))
            {
                m_skribidiContext->editor_drag_active = false;
                return;
            }

            m_skribidiContext->editor_drag_active = true;
            double time = m_skribidiContext->frameTimeMs / 1000.0;
            skb_editor_process_mouse_click(m_skribidiContext->editor, localPos.x, localPos.y, skb_modifiers, time);
            EnsureCursorVisible(ActiveElementID());
        }
    }

    void SkribidiTextSystem::OnTextComposition(ID /*windowId*/, std::string_view text, int32_t cursor)
    {
        SkribidiContext* ctx = m_skribidiContext;
        if (!ctx || !ctx->editor || ctx->active_text_id != ctx->editing_text_id || ctx->active_read_only)
            return;
        if (text.empty())
        {
            ClearComposition();
            return;
        }
        // E.g. number fields take digits and signs only: nothing for an IME to compose.
        if (ctx->active_validator && !ctx->active_validator->AcceptsComposition())
            return;
        NoteSelectionBeforeEdit();

        std::vector<uint32_t> utf32(size_t(skb_utf8_to_utf32_count(text.data(), int32_t(text.size()))));
        skb_utf8_to_utf32(text.data(), int32_t(text.size()), utf32.data(), int32_t(utf32.size()));
        skb_editor_set_composition_utf32(ctx->editor, ctx->temp_alloc, utf32.data(), int32_t(utf32.size()),
                                         std::clamp(cursor, 0, int32_t(utf32.size())));
        ctx->composing = true;
    }

    void SkribidiTextSystem::ClearComposition()
    {
        SkribidiContext* ctx = m_skribidiContext;
        if (!ctx || !ctx->composing)
            return;
        ctx->composing = false;
        if (ctx->editor)
            skb_editor_clear_composition(ctx->editor, ctx->temp_alloc);
    }

    void SkribidiTextSystem::OnMouseScroll(ID /*windowId*/, int2 /*sreenPos*/, float2 /*scrollOffset*/, KeyModifier /*modifiers*/)
    {
        if (!m_skribidiContext)
            return;
    }

    // -------------------- Cache Management --------------------

}
