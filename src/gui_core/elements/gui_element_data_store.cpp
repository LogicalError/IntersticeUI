#include <gui_core/elements/gui_element_data_store.h>

namespace GUI
{
    void ElementDataStore::Touch(ID id)
    {
        auto it = m_entries.find(id);
        if (it != m_entries.end())
            it->second.touchedFrame = m_currentFrame;
    }

    void ElementDataStore::Remove(ID id)
    {
        m_entries.erase(id);
    }

    void ElementDataStore::Clear()
    {
        m_entries.clear();
    }

    void ElementDataStore::BeginFrame()
    {
        m_currentFrame++;
    }

    void ElementDataStore::EndFrame()
    {
        for (auto it = m_entries.begin(); it != m_entries.end(); )
        {
            if (it->second.touchedFrame < m_currentFrame)
                it = m_entries.erase(it);
            else
                ++it;
        }
    }
}
