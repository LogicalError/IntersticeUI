#include <gui_core/state/gui_tab_chain_manager.h>
#include <gui_core/state/gui_state_manager.h>


namespace GUI
{
    void TabChainManager::BeginFrame(const GUIStateManager& guiStateManager)
    {
        m_currentFocusedID = guiStateManager.Interaction().GetFocusedID();
        m_activeWindowID = guiStateManager.Interaction().GetActiveWindowID();
        m_currentWindowID = ID::Invalid();
        m_nextTabTarget = ID::Invalid();
        m_prevTabTarget = ID::Invalid();
        m_firstTabTarget = ID::Invalid();
        m_lastTabTarget = ID::Invalid();
        m_defaultFocusID = ID::Invalid();
    }

} // namespace GUI
