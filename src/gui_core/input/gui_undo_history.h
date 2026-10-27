#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tachyon
{
    /// @addtogroup input
    /// @{

    /// The app's undo history: steps that each undo and redo one change. Text fields push
    /// their committed edits here, and so can the app's own editors; Undo / Redo walk it.
    class UndoHistory
    {
    public:
        struct Step
        {
            std::string           description;   ///< "Edit text", "Move node"
            std::function<void()> undo;
            std::function<void()> redo;
            /// Steps with the same non-zero key merge while merging is open (e.g. the typing
            /// in one field while it has the focus): the first's undo, the latest's redo.
            uint64_t              mergeKey = 0;
        };

        /// Adds a step after the current one, dropping any undone steps (or merges it into the
        /// last step, see Step::mergeKey). Does not run it.
        void Push(Step step)
        {
            m_steps.resize(m_current);
            if (m_mergeOpen && step.mergeKey != 0 && !m_steps.empty() && m_steps.back().mergeKey == step.mergeKey)
            {
                m_steps.back().redo = std::move(step.redo);
                return;
            }
            m_steps.push_back(std::move(step));
            m_current   = m_steps.size();
            m_mergeOpen = true;
        }

        /// The next push starts a step of its own (e.g. when a field loses the focus).
        void EndMerge() { m_mergeOpen = false; }

        bool CanUndo() const { return m_current > 0; }
        bool CanRedo() const { return m_current < m_steps.size(); }

        void Undo()
        {
            if (!CanUndo()) return;
            m_mergeOpen = false;
            if (const auto& undo = m_steps[--m_current].undo) undo();
        }

        void Redo()
        {
            if (!CanRedo()) return;
            m_mergeOpen = false;
            if (const auto& redo = m_steps[m_current++].redo) redo();
        }

        size_t           UndoCount() const { return m_current; }
        size_t           RedoCount() const { return m_steps.size() - m_current; }
        std::string_view UndoDescription() const { return CanUndo() ? std::string_view(m_steps[m_current - 1].description) : std::string_view{}; }
        std::string_view RedoDescription() const { return CanRedo() ? std::string_view(m_steps[m_current].description) : std::string_view{}; }

        void Clear()
        {
            m_steps.clear();
            m_current   = 0;
            m_mergeOpen = false;
        }

    private:
        std::vector<Step> m_steps;
        size_t            m_current   = 0;       ///< steps before it are done, from it on undone
        bool              m_mergeOpen = false;
    };
    /// @}
}
