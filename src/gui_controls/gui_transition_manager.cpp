#include <gui_controls/gui_transition_manager.h>

#include <gui_core/style/gui_style.h>
#include <gui_core/style/gui_style_ref.h>

namespace tachyon
{
    uint32_t ComputeGPUStyleIndex(VisualBoxStyleIndex base, ElementState state)
    {
        if (base == VisualBoxStyleIndex::Invalid) return 0;
        state &= ElementState::VisualMask;
        if ((state & ElementState::Disabled) != ElementState::None)
            state = ElementState::Disabled;
        return (((int)base - 1) * Style::kStateCount) + (int)state;
    }

    void TransitionManager::OnStateChange(ID id, ElementState fromState, ElementState toState, float elapsedSecs)
    {
        if (!m_enabled)
            return;

        auto it = m_transitions.find(id);
        if (it != m_transitions.end())
        {
            // Already heading there: don't restart.
            if (it->second.toState == toState)
                return;
            // Otherwise start from the running transition's target, so the visual
            // doesn't snap backward. Style indices are discrete, so this can't resume
            // mid-blend (#65).
            fromState = it->second.toState;
        }
        m_transitions[id] = TransitionEntry
        {
            .fromState = fromState,
            .toState   = toState,
            .startTime = elapsedSecs,
            .duration  = kDefaultDuration
        };
    }

    TransitionState TransitionManager::Resolve(ID id, VisualBoxStyleIndex base,
                                               ElementState currentState, float elapsedSecs) const
    {
        TransitionState r;
        r.toStyleIndex = r.fromStyleIndex = ComputeGPUStyleIndex(base, currentState);
        r.blendT = 1.0f;

        auto it = m_transitions.find(id);
        if (it == m_transitions.end())
            return r;

        const TransitionEntry& e = it->second;
        float rawT = (elapsedSecs - e.startTime) / e.duration;
        if (rawT >= 1.0f)
            return r;

        // Smoothstep easing
        float t = rawT * rawT * (3.0f - 2.0f * rawT);

        r.fromStyleIndex = ComputeGPUStyleIndex(base, e.fromState);
        r.toStyleIndex   = ComputeGPUStyleIndex(base, e.toState);
        r.blendT         = t;
        return r;
    }

    void TransitionManager::Purge(float elapsedSecs)
    {
        for (auto it = m_transitions.begin(); it != m_transitions.end(); )
        {
            const TransitionEntry& e = it->second;
            if ((elapsedSecs - e.startTime) >= e.duration)
                it = m_transitions.erase(it);
            else
                ++it;
        }
    }

} // namespace tachyon
