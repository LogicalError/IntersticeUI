#pragma once

#include <assert.h>
#include <stdint.h>

#include <gui_core/gui_id.h>

// Small state values owned by GUIStateManager.

namespace interstice
{
    enum class MouseCursor : uint8_t;   // defined in gui_style.h

    // Per-frame requested cursor.
    class CursorState
    {
        // 0 = MouseCursor::Default.
        MouseCursor m_requested{};

    public:
        void        Request(MouseCursor c) { m_requested = c; }
        void        Clear()                { m_requested = {}; }
        MouseCursor Get() const            { return m_requested; }
    };

    // Focused text editor, plus last frame's for detecting transitions.
    class TextEditorFocusState
    {
        ID m_focusedID     = ID::Invalid();
        ID m_prevFocusedID = ID::Invalid();

    public:
        ID GetFocusedID()     const { return m_focusedID;     }
        ID GetPrevFocusedID() const { return m_prevFocusedID; }

        void Set(ID id) { m_focusedID = id; }
        void Clear()    { m_focusedID = ID::Invalid(); }
        void Commit()   { m_prevFocusedID = m_focusedID; }
    };

    // Construction-time Push/Pop nesting read by GUIStateManager::GetCurrentState.
    // Reset at BeginFrame, balance-checked at EndFrame.
    class ConstructionScopes
    {
        uint32_t m_disabledLevel      = 0;
        uint32_t m_selectedLevel      = 0;
        uint32_t m_indeterminateLevel = 0;

    public:
        void PushDisabled() { m_disabledLevel++; }
        void PopDisabled()  { assert(m_disabledLevel > 0); if (m_disabledLevel > 0) m_disabledLevel--; }

        void PushSelected() { m_selectedLevel++; }
        void PopSelected()  { assert(m_selectedLevel > 0); if (m_selectedLevel > 0) m_selectedLevel--; }

        void PushIndeterminate() { m_indeterminateLevel++; }
        void PopIndeterminate()  { assert(m_indeterminateLevel > 0); if (m_indeterminateLevel > 0) m_indeterminateLevel--; }

        bool IsDisabled()      const { return m_disabledLevel      > 0; }
        bool IsSelected()      const { return m_selectedLevel      > 0; }
        bool IsIndeterminate() const { return m_indeterminateLevel > 0; }

        void Reset()            { m_disabledLevel = 0; m_selectedLevel = 0; m_indeterminateLevel = 0; }
        bool IsBalanced() const { return m_disabledLevel == 0 && m_selectedLevel == 0 && m_indeterminateLevel == 0; }
    };
}
