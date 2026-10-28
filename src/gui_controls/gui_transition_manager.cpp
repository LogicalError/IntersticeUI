#include <gui_controls/gui_transition_manager.h>

#include <gui_core/style/gui_style.h>
#include <gui_core/style/gui_style_ref.h>

#include <algorithm>

namespace tachyon
{
    namespace
    {
        // Smoothstep: eases in and out, and Ease(1 - t) == 1 - Ease(t).
        float Ease(float t) { return t * t * (3.0f - 2.0f * t); }
    }

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
            TransitionEntry& running = it->second;
            // Already heading there: don't restart.
            if (running.toState == toState)
                return;
            const float progress = std::clamp((elapsedSecs - running.startTime) / running.duration, 0.0f, 1.0f);
            if (progress < 1.0f && toState == running.fromState)
            {
                // Back where it came from (out again before the hover finished): run the
                // blend backwards from where it is. The easing is symmetric, so starting
                // at 1 - progress shows the same mix.
                running = TransitionEntry
                {
                    .fromState = running.toState,
                    .toState   = toState,
                    .startTime = elapsedSecs - (1.0f - progress) * running.duration,
                    .duration  = running.duration
                };
                return;
            }
            // A blend is between two styles only, so a third state starts from the one
            // the blend is closer to.
            fromState = Ease(progress) < 0.5f ? running.fromState : running.toState;
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

        r.fromStyleIndex = ComputeGPUStyleIndex(base, e.fromState);
        r.toStyleIndex   = ComputeGPUStyleIndex(base, e.toState);
        r.blendT         = Ease(std::max(rawT, 0.0f));
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
