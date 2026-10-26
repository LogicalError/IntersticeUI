#pragma once

#include <gui_core/gui_id.h>

namespace interstice
{
    class GUIStateManager;
    
    /// Tracks the next/prev tab targets around the focused element, rebuilt each frame during
    /// construction and scoped to the active window. Without focus it falls back to the
    /// default focus ID, else the window's first target.
    class TabChainManager
    {
        ID m_currentFocusedID = ID::Invalid();
        ID m_currentWindowID = ID::Invalid();
        ID m_activeWindowID = ID::Invalid();
        
        /// Results, for the active window only.
        ID m_nextTabTarget = ID::Invalid();
        ID m_prevTabTarget = ID::Invalid();
        
        /// Construction-time tracking for the current window.
        ID m_firstTabTarget = ID::Invalid();
        ID m_lastTabTarget = ID::Invalid();
        ID m_defaultFocusID = ID::Invalid();
        
    public:
        void BeginFrame(const GUIStateManager& guiStateManager);
        
        /// The target Tab moves to when nothing has focus, e.g. a rename textbox that should
        /// take focus.
        void SetDefaultFocusID(ID defaultID) 
        { 
            m_defaultFocusID = defaultID; 
        }
        
        /// Starts collecting `windowID`'s tab targets until EndWindow; only the active window
        /// builds a chain.
        void BeginWindow(ID windowID)
        {
            m_currentWindowID = windowID;
            if (m_currentWindowID == m_activeWindowID)
            {
                m_firstTabTarget = ID::Invalid();
                m_lastTabTarget = ID::Invalid();
            }
        }
        
        /// Adds `elementID` to the current window's tab chain, in call order.
        void RegisterTabTarget(ID elementID)
        {
            // Only the active window gets a tab chain.
            if (m_currentWindowID != m_activeWindowID)
                return;

            if (m_firstTabTarget == ID::Invalid())
            {
                m_firstTabTarget = elementID;
            }
            
            if (m_lastTabTarget == m_currentFocusedID)
            {
                m_nextTabTarget = elementID;
            }
            
            if (elementID == m_currentFocusedID)
            {
                m_prevTabTarget = m_lastTabTarget;
            }
            
            m_lastTabTarget = elementID;
        }
        
        /// Ends the window begun by BeginWindow; its chain wraps around.
        void EndWindow()
        {
            if (m_currentWindowID != m_activeWindowID)
                return;
            
            // Wrap around.
            if (m_nextTabTarget == ID::Invalid() && m_firstTabTarget != ID::Invalid())
            {
                m_nextTabTarget = m_firstTabTarget;
            }
            
            if (m_prevTabTarget == ID::Invalid() && m_lastTabTarget != ID::Invalid())
            {
                m_prevTabTarget = m_lastTabTarget;
            }
        }
        
        void EndFrame()
        {
            if (m_currentFocusedID == ID::Invalid())
            {
                if (m_defaultFocusID != ID::Invalid())
                {
                    m_prevTabTarget =
                    m_nextTabTarget = m_defaultFocusID;
                } else 
                if (m_firstTabTarget != ID::Invalid())
                {
                    m_prevTabTarget =
                    m_nextTabTarget = m_firstTabTarget;
                } else
                {
                    m_prevTabTarget =
                    m_nextTabTarget = ID::Invalid();
                }
            } else
            {
                if (m_nextTabTarget == ID::Invalid())
                {
                    m_nextTabTarget = m_firstTabTarget;
                }
            }
        }
        
        ID GetNextTabTarget() const { return m_nextTabTarget; }
        ID GetPrevTabTarget() const { return m_prevTabTarget; }
        ID GetDefaultFocus() const { return m_defaultFocusID != ID::Invalid() ? m_defaultFocusID : m_firstTabTarget; }
    };

} // namespace interstice
