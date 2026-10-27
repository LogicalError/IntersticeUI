#pragma once

#include <gui_core/gui_id.h>

#include <stdint.h>
#include <unordered_map>

namespace tachyon
{
    /// @addtogroup app
    /// @{

    enum class ElementState : uint8_t;
    enum class VisualBoxStyleIndex : uint16_t;

    /// Pre-computed GPU style indices for a box that is mid-transition (or not).
    /// When not transitioning: fromStyleIndex == toStyleIndex and blendT == 1.0.
    struct TransitionState
    {
        uint32_t fromStyleIndex = 0;
        uint32_t toStyleIndex   = 0;
        float    blendT         = 1.0f; ///< 0 = fully "from", 1 = fully "to"
    };

    /// Flat GPU style index for a base pool index and state. Must match vk_renderer.cpp and
    /// StyleManager::GetVisualBoxStyle (VisualMask, Disabled collapsing).
    uint32_t ComputeGPUStyleIndex(VisualBoxStyleIndex base, ElementState state);

    /// Tracks in-progress visual state transitions per element ID. Times are seconds on the
    /// application's elapsed-time clock.
    class TransitionManager
    {
    public:
        static constexpr float kDefaultDuration = 0.1f; ///< transition length, seconds

        /// Starts (or retargets) the element's transition.
        void OnStateChange(ID id, ElementState fromState, ElementState toState, float elapsedSecs);

        /// The style indices and blend for the element's box `base` in `currentState`;
        /// fromStyleIndex == toStyleIndex and blendT == 1 when not transitioning.
        TransitionState Resolve(ID id, VisualBoxStyleIndex base,
                                ElementState currentState, float elapsedSecs) const;

        /// Removes entries whose transition has completed.
        void Purge(float elapsedSecs);

        /// Removes all active transitions, for instance after a reorder invalidates ID mappings.
        void Clear() { m_transitions.clear(); }

        /// Test/debug seam: when disabled, states snap instantly (deterministic rendering).
        /// Disabling drops the active transitions.
        void SetEnabled(bool enabled) { m_enabled = enabled; if (!enabled) m_transitions.clear(); }
        /// True unless SetEnabled(false) was called.
        bool IsEnabled() const { return m_enabled; }

    private:
        struct TransitionEntry
        {
            ElementState fromState;
            ElementState toState;
            float        startTime; ///< elapsedSecs when this transition started
            float        duration;  ///< seconds
        };

        std::unordered_map<ID, TransitionEntry> m_transitions;
        bool m_enabled = true;
    };

    /// @}
} // namespace tachyon
