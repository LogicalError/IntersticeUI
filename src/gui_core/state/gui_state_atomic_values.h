#pragma once

#include <assert.h>
#include <stdint.h>

#include <gui_core/gui_id.h>
#include <gui_core/diagnostics/gui_logging.h>

// Small state values owned by GUIStateManager.

namespace interstice
{
    enum class MouseCursor : uint8_t;   // gui_core/style/gui_style.h

    /// The mouse cursor requested this frame.
    class CursorState
    {
        /// 0 = MouseCursor::Default.
        MouseCursor m_requested{};

    public:
        void        Request(MouseCursor c) { m_requested = c; }
        void        Clear()                { m_requested = {}; }
        MouseCursor Get() const            { return m_requested; }
    };

    /// The focused text editor, plus the one as of the last Commit, to detect changes.
    class TextEditorFocusState
    {
        ID m_focusedID     = ID::Invalid();
        ID m_prevFocusedID = ID::Invalid();

    public:
        ID GetFocusedID()     const { return m_focusedID;     }
        /// The focused editor as of the last Commit, when the text system switched its editor
        /// to it: the field the editor holds.
        ID GetPrevFocusedID() const { return m_prevFocusedID; }

        void Set(ID id) { m_focusedID = id; }
        void Clear()    { m_focusedID = ID::Invalid(); }
        void Commit()   { m_prevFocusedID = m_focusedID; }
    };

    /// Construction-time Push/Pop nesting read by GUIStateManager::GetCurrentState.
    /// Reset at BeginFrame, balance-checked at EndFrame.
    class ConstructionScopes
    {
        uint32_t m_disabledLevel      = 0;
        uint32_t m_selectedLevel      = 0;
        uint32_t m_indeterminateLevel = 0;

    public:
        void PushDisabled() { m_disabledLevel++; }
        void PopDisabled()  { if (m_disabledLevel > 0) m_disabledLevel--; else ReportUnmatchedPop("ConstructionScopes: PopDisabled without PushDisabled (ignored)"); }

        void PushSelected() { m_selectedLevel++; }
        void PopSelected()  { if (m_selectedLevel > 0) m_selectedLevel--; else ReportUnmatchedPop("ConstructionScopes: PopSelected without PushSelected (ignored)"); }

        void PushIndeterminate() { m_indeterminateLevel++; }
        void PopIndeterminate()  { if (m_indeterminateLevel > 0) m_indeterminateLevel--; else ReportUnmatchedPop("ConstructionScopes: PopIndeterminate without PushIndeterminate (ignored)"); }

        bool IsDisabled()      const { return m_disabledLevel      > 0; }
        bool IsSelected()      const { return m_selectedLevel      > 0; }
        bool IsIndeterminate() const { return m_indeterminateLevel > 0; }

        void Reset()            { m_disabledLevel = 0; m_selectedLevel = 0; m_indeterminateLevel = 0; }
        bool IsBalanced() const { return m_disabledLevel == 0 && m_selectedLevel == 0 && m_indeterminateLevel == 0; }

    private:
        /// A Pop without its Push is reported and ignored.
        static void ReportUnmatchedPop(const char* message)
        {
            Diagnostics::ReportMisuse(Diagnostics::MakeLogCategory("state"), message);
        }
    };
}
