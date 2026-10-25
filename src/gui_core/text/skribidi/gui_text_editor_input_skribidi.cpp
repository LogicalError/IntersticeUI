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
namespace interstice
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

    void SkribidiTextSystem::OnKeyboard(ID /*windowId*/, interstice::KeyCode key, int /*repeat*/, KeyModifier modifiers, KeyAction action)
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

        const NumericType numericMode = m_skribidiContext->active_numeric_mode;
        const bool isNumericField = (numericMode != NumericType::None);
        const bool isFloatField = (numericMode == NumericType::TypeFloat);

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

            if (isNumericField)
            {
                if (codepoint > 127)
                    return;

                const char c = static_cast<char>(codepoint);
                bool isAllowedChar = (c >= '0' && c <= '9') || (c == '+') || (c == '-');
                if (isFloatField)
                {
                    isAllowedChar = isAllowedChar || (c == '.') || (c == 'e') || (c == 'E');
                }

                if (!isAllowedChar)
                    return;
            }

            skb_editor_insert_codepoint(m_skribidiContext->editor, m_skribidiContext->temp_alloc, SKB_CURRENT_SELECTION, codepoint);
            EnsureCursorVisible(m_skribidiContext->active_element_id);
        } else
        {
            // Only process key down events
            if (action != interstice::KeyAction::KeyDown)
                return;

            if (key == interstice::KeyCode::Escape && m_textDragArmed)
            {
                EndTextDragging();
                return;
            }

            if (key == interstice::KeyCode::Return || key == interstice::KeyCode::Return2)
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
                case interstice::KeyCode::Return:
                case interstice::KeyCode::Return2:        skb_key = SKB_KEY_ENTER; break;
                case interstice::KeyCode::Left:        skb_key = SKB_KEY_LEFT; break;
                case interstice::KeyCode::Right:        skb_key = SKB_KEY_RIGHT; break;
                case interstice::KeyCode::Up:            skb_key = SKB_KEY_UP; break;
                case interstice::KeyCode::Down:        skb_key = SKB_KEY_DOWN; break;
                case interstice::KeyCode::Home:        skb_key = SKB_KEY_HOME; break;
                case interstice::KeyCode::End:            skb_key = SKB_KEY_END; break;
                case interstice::KeyCode::Backspace:
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
                case interstice::KeyCode::Delete:
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
                case interstice::KeyCode::KeyV:
                    if (readOnly) return;
                    if (skb_modifiers & SKB_MOD_CONTROL)
                    {
                        if (m_skribidiContext->clipboard != nullptr)
                            Paste(m_skribidiContext->clipboard->Get());
                    }
                    return;
                case interstice::KeyCode::KeyC:
                    if (skb_modifiers & SKB_MOD_CONTROL)
                    {
                        std::string copiedText = Copy();
                        if (!copiedText.empty() && m_skribidiContext->clipboard != nullptr)
                        {
                            m_skribidiContext->clipboard->Set(copiedText);
                        }
                    }
                    return;
                case interstice::KeyCode::KeyX:
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
                case interstice::KeyCode::KeyA:
                    if (skb_modifiers & SKB_MOD_CONTROL)
                    {
                        skb_editor_select_all(m_skribidiContext->editor);
                        EnsureCursorVisible(m_skribidiContext->active_element_id);
                    }
                    return;
                case interstice::KeyCode::KeyZ:
                    if (readOnly) return;
                    if (skb_modifiers & SKB_MOD_CONTROL)
                    {
                        if (skb_modifiers & SKB_MOD_SHIFT)
                        {
                            if (skb_editor_can_redo(m_skribidiContext->editor))
                                skb_editor_redo(m_skribidiContext->editor, m_skribidiContext->temp_alloc);
                        } else
                        {
                            if (skb_editor_can_undo(m_skribidiContext->editor))
                                skb_editor_undo(m_skribidiContext->editor, m_skribidiContext->temp_alloc);
                        }
                        EnsureCursorVisible(m_skribidiContext->active_element_id);
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
                EnsureCursorVisible(m_skribidiContext->active_element_id);
            }
        }
    }

    void SkribidiTextSystem::OnMouseMove(ID windowId, interstice::int2 screenPos, MouseButtons /*button*/, KeyModifier /*modifiers*/)
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
        interstice::float2 localPos{ static_cast<float>(screenPos.x), static_cast<float>(screenPos.y) };
        if (!TryGetActiveTextEditorLocalPoint(windowId, screenPos, localPos))
        {
            localPos.x -= m_skribidiContext->view.cx;
            localPos.y -= m_skribidiContext->view.cy;
        }

        skb_editor_process_mouse_drag(m_skribidiContext->editor, localPos.x, localPos.y);
        if (m_skribidiContext->active_element_id != ID::Invalid())
            EnsureCursorVisible(m_skribidiContext->active_element_id);
    }

    bool SkribidiTextSystem::IsHoveringSelectedText(ID textEditorId, ID windowId, interstice::int2 screenSpace) const
    {
        if (!m_skribidiContext || !m_skribidiContext->editor)
            return false;
        if (textEditorId == ID::Invalid())
            return false;
        if (textEditorId != m_skribidiContext->active_element_id)
            return false;
        if (m_skribidiContext->active_text_id != m_skribidiContext->editing_text_id)
            return false;

        const skb_text_range_t selection = skb_editor_get_current_selection(m_skribidiContext->editor);
        if (skb_editor_get_text_range_count(m_skribidiContext->editor, selection) <= 0)
            return false;

        interstice::float2 localPos{ static_cast<float>(screenSpace.x), static_cast<float>(screenSpace.y) };
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

    bool SkribidiTextSystem::TryGetTextDragHoverTarget(ID windowId, interstice::int2 screenSpace, ID& outTargetId) const
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

    bool SkribidiTextSystem::TryGetTextDragCursor(ID windowId, interstice::int2 screenSpace, MouseCursor& outCursor) const
    {
        ID targetId;
        if (!TryGetTextDragHoverTarget(windowId, screenSpace, targetId))
            return false;
        outCursor = (targetId != ID::Invalid()) ? MouseCursor::Default : MouseCursor::NotAllowed;
        return true;
    }

    bool SkribidiTextSystem::TryGetActiveTextEditorLocalPoint(ID windowId, interstice::int2 screenSpace, interstice::float2& outLocal) const
    {
        if (!m_skribidiContext)
            return false;
        if (m_skribidiContext->active_element_id == ID::Invalid())
            return false;

        return TryGetTextEditorLocalPoint(m_skribidiContext->active_element_id, windowId, screenSpace, outLocal);
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

    bool SkribidiTextSystem::TryGetTextEditorLocalPoint(ID textEditorId, ID windowId, interstice::int2 screenSpace, interstice::float2& outLocal) const
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

    ID SkribidiTextSystem::FindTextEditorAtScreenPoint(ID windowId, interstice::int2 screenSpace) const
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

    bool SkribidiTextSystem::TryGetCaretPositionInternal(ID windowId, interstice::int2 screenPos, ID targetTextEditorId, skb_editor_t*& outTargetEditor, skb_text_position_t& outPosition)
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
        
        interstice::float2 localPos{ static_cast<float>(screenPos.x), static_cast<float>(screenPos.y) };
        if (!TryGetTextEditorLocalPoint(targetTextEditorId, windowId, screenPos, localPos))
            return false;
        
        outPosition = skb_editor_hit_test(outTargetEditor, SKB_MOVEMENT_CARET, localPos.x, localPos.y);
        return true;
    }

    void SkribidiTextSystem::DropDraggedText(ID windowId, interstice::int2 screenPos, ID targetTextEditorId)
    {
        std::string movedText;
        bool droppedOnOwnTextbox = (targetTextEditorId == m_textDragSourceId);
        if (!droppedOnOwnTextbox)
        {
            const skb_text_range_t selection = skb_editor_get_current_selection(m_skribidiContext->editor);
            movedText = Cut();
            if (movedText.empty())
            {
                movedText = m_textDragPayload;
            }

            SetActiveTextEditor(targetTextEditorId);
            if (m_skribidiContext->guiStateManager)
            {
                m_skribidiContext->guiStateManager->Interaction().SetFocusID(targetTextEditorId);
                m_skribidiContext->guiStateManager->TextEditorFocus().Set(targetTextEditorId);
            }

            skb_text_position_t dropCaretPos {};
            skb_editor_t* targetEditor;
            bool hasDropCaretPos = TryGetCaretPositionInternal(windowId, screenPos, targetTextEditorId, targetEditor, dropCaretPos);
            if (hasDropCaretPos)
            {
                skb_editor_select(targetEditor, skb_text_range_t { .start = dropCaretPos, .end = dropCaretPos });
            }

            if (!movedText.empty())
            {
                m_skipNextSelectAllId = targetTextEditorId;
                Paste(movedText);
                if (hasDropCaretPos)
                {
                    skb_editor_select(targetEditor, skb_text_range_t { .start = dropCaretPos, .end = dropCaretPos });
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
                skb_editor_select(targetEditor, skb_text_range_t { .start = dropCaretPos, .end = dropCaretPos });
            }
        }

        EnsureCursorVisible(targetTextEditorId);
    }

    void SkribidiTextSystem::OnMouseButton(ID windowId, interstice::int2 screenPos, MouseButtons button, KeyModifier modifiers, MouseAction action)
    {
        if (!m_skribidiContext)
            return;

        if (action == interstice::MouseAction::MouseUp &&
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

        if (action == interstice::MouseAction::MouseUp)
        {
            if (m_textDragArmed && !m_skribidiContext->drag_text)
            {
                interstice::float2 localPos{ static_cast<float>(screenPos.x), static_cast<float>(screenPos.y) };
                if (TryGetActiveTextEditorLocalPoint(windowId, screenPos, localPos))
                {
                    const skb_text_position_t caretPos = skb_editor_hit_test(m_skribidiContext->editor, SKB_MOVEMENT_CARET, localPos.x, localPos.y);
                    skb_editor_select(m_skribidiContext->editor, skb_text_range_t { .start = caretPos, .end = caretPos });
                    EnsureCursorVisible(m_skribidiContext->active_element_id);
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

        if (action == interstice::MouseAction::MouseDown)
        {
            // Read-only editors never drag their text - pressing (even on selected text) starts
            // a new selection, so click-drag always selects instead of moving text.
            if (!m_skribidiContext->active_read_only &&
                IsHoveringSelectedText(m_skribidiContext->active_element_id, windowId, screenPos))
            {
                m_textDragSourceId = m_skribidiContext->active_element_id;
                m_textDragPayload = Copy();
                m_textDragArmed = !m_textDragPayload.empty();
                m_textDragStartScreenPos = screenPos;
                m_skribidiContext->drag_text = false;
                m_skribidiContext->editor_drag_active = false;
                return;
            }

            m_textDragArmed = false;
            m_skribidiContext->drag_text = false;

            interstice::float2 localPos{ static_cast<float>(screenPos.x), static_cast<float>(screenPos.y) };
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
            EnsureCursorVisible(m_skribidiContext->active_element_id);
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
        // Numeric fields only take ASCII digits and signs; IMEs have nothing to compose there.
        if (ctx->active_numeric_mode != NumericType::None)
            return;

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
