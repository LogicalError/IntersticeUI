#include <gui_controls/gui_transition_manager.h>

#include <gui_core/graphics/gui_renderer.h>
#include <gui_core/style/gui_style.h>
#include <gui_core/style/gui_style_ref.h>

#include <algorithm>

namespace tachyon
{
    uint32_t ComputeGPUStyleIndex(VisualBoxStyleIndex base, ElementState state)
    {
        return GUIRenderer::StyleIndex(base, state);
    }

    TransitionTiming TransitionManager::TimingFor(ElementState fromState, ElementState toState)
    {
        const ElementState changed = (fromState ^ toState) & ElementState::VisualMask;
        auto has = [](ElementState s, ElementState bit) { return (s & bit) != ElementState::None; };
        if (has(changed, ElementState::Disabled))
            return {};
        if (has(changed, ElementState::Active))
            return has(toState, ElementState::Active) ? TransitionTiming{} : kRelease;
        if (has(changed, ElementState::Hover))
            return has(toState, ElementState::Hover) ? kHoverIn : kHoverOut;
        if (has(changed, ElementState::Focused))
            return kFocus;
        return {};
    }

    void TransitionManager::OnStateChange(ID id, ElementState fromState, ElementState toState, float elapsedSecs)
    {
        OnStateChange(id, fromState, toState, TimingFor(fromState, toState), elapsedSecs);
    }

    void TransitionManager::OnStateChange(ID id, ElementState fromState, ElementState toState,
                                          TransitionTiming timing, float elapsedSecs)
    {
        if (!m_enabled)
            return;
        if (timing.duration <= 0.0f || m_reducedMotion)
        {
            m_transitions.erase(id);   // instant: shows toState from now on
            return;
        }

        auto it = m_transitions.find(id);
        if (it != m_transitions.end())
        {
            const TransitionEntry& running = it->second;
            // Already heading there: don't restart.
            if (running.toState == toState)
                return;
            const float progress = std::clamp((elapsedSecs - running.startTime) / running.timing.duration, 0.0f, 1.0f);
            const float shown    = running.timing.easing(progress);   // share of running.toState
            if (progress < 1.0f && toState == running.fromState)
            {
                // Back where it came from (out again before the hover finished): carry on
                // from the mix it shows, in the new timing.
                const float startAt = timing.easing.TimeAt(1.0f - shown);
                it->second = TransitionEntry
                {
                    .fromState = running.toState,
                    .toState   = toState,
                    .startTime = elapsedSecs - startAt * timing.duration,
                    .timing    = timing
                };
                return;
            }
            // A blend is between two styles only, so a third state starts from the one
            // the blend is closer to.
            fromState = shown < 0.5f ? running.fromState : running.toState;
        }
        m_transitions[id] = TransitionEntry
        {
            .fromState = fromState,
            .toState   = toState,
            .startTime = elapsedSecs,
            .timing    = timing
        };
    }

    void TransitionManager::OnSelectedSeen(ID id, ElementState state, float elapsedSecs)
    {
        const bool selected = (state & ElementState::Selected) != ElementState::None;
        const auto [it, added] = m_selected.try_emplace(id, SelectedSeen{ selected, m_frame });
        if (added)
            return;
        const bool was = it->second.selected;
        it->second = SelectedSeen{ selected, m_frame };
        if (was != selected)
            OnStateChange(id, state ^ ElementState::Selected, state, kToggle, elapsedSecs);
    }

    std::optional<TransitionManager::Blend> TransitionManager::Find(ID id, float elapsedSecs) const
    {
        auto it = m_transitions.find(id);
        if (it == m_transitions.end())
            return std::nullopt;
        const TransitionEntry& e = it->second;
        const float rawT = (elapsedSecs - e.startTime) / e.timing.duration;
        if (rawT >= 1.0f)
            return std::nullopt;
        return Blend{ e.fromState, e.toState, e.timing.easing(std::max(rawT, 0.0f)) };
    }

    TransitionState TransitionManager::Resolve(ID id, VisualBoxStyleIndex base,
                                               ElementState currentState, float elapsedSecs) const
    {
        TransitionState r;
        r.toStyleIndex = r.fromStyleIndex = ComputeGPUStyleIndex(base, currentState);
        r.blendT = 1.0f;

        const std::optional<Blend> blend = Find(id, elapsedSecs);
        if (!blend)
            return r;
        r.fromStyleIndex = ComputeGPUStyleIndex(base, blend->fromState);
        r.toStyleIndex   = ComputeGPUStyleIndex(base, blend->toState);
        r.blendT         = blend->t;
        return r;
    }

    void TransitionManager::Purge(float elapsedSecs)
    {
        for (auto it = m_transitions.begin(); it != m_transitions.end(); )
        {
            const TransitionEntry& e = it->second;
            if ((elapsedSecs - e.startTime) >= e.timing.duration)
                it = m_transitions.erase(it);
            else
                ++it;
        }
        std::erase_if(m_selected, [this](const auto& seen) { return seen.second.frame != m_frame; });
        ++m_frame;
    }

} // namespace tachyon
