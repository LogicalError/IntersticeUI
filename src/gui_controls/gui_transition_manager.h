#pragma once

#include <gui_controls/gui_easing.h>
#include <gui_core/gui_id.h>

#include <optional>
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

    /// How long a change of state takes, and how it eases. A duration of 0 is instant.
    struct TransitionTiming
    {
        float       duration = 0.0f;              ///< seconds
        CubicBezier easing   = Easing::EaseOut;

        bool operator==(const TransitionTiming&) const = default;
    };

    /// Flat GPU style index for a base pool index and state. Must match vk_renderer.cpp and
    /// StyleManager::GetVisualBoxStyle (VisualMask, Disabled collapsing).
    uint32_t ComputeGPUStyleIndex(VisualBoxStyleIndex base, ElementState state);

    /// Tracks in-progress visual state transitions per element ID. Times are seconds on the
    /// application's elapsed-time clock.
    class TransitionManager
    {
    public:
        // Each kind of change has its own timing; the ones not listed (press, selection,
        // disabled) are instant.
        static constexpr TransitionTiming kHoverIn  { 0.070f, Easing::EaseOut };
        static constexpr TransitionTiming kHoverOut { 0.140f, Easing::EaseOut };
        static constexpr TransitionTiming kRelease  { 0.120f, Easing::EaseOut };
        static constexpr TransitionTiming kFocus    { 0.100f, Easing::EaseOut };
        static constexpr TransitionTiming kToggle   { 0.120f, Easing::EaseInOut }; ///< a switch, check box or radio turning on or off

        /// The timing of a change from `fromState` to `toState`. When several states change
        /// at once, the press counts first, then the hover, then the focus.
        static TransitionTiming TimingFor(ElementState fromState, ElementState toState);

        /// A running transition: the states it blends between, and how far it is (eased).
        struct Blend
        {
            ElementState fromState;
            ElementState toState;
            float        t; ///< 0 = fully fromState, 1 = fully toState
        };

        /// Starts (or retargets) the element's transition, timed by TimingFor.
        void OnStateChange(ID id, ElementState fromState, ElementState toState, float elapsedSecs);
        /// As above, with its own timing.
        void OnStateChange(ID id, ElementState fromState, ElementState toState,
                           TransitionTiming timing, float elapsedSecs);

        /// For an element that animates its Selected flip (ElementFlags::AnimatesSelected), each
        /// frame it's built: when Selected differs from the frame before, the change runs with
        /// kToggle. An element that wasn't built the frame before starts without one.
        void OnSelectedSeen(ID id, ElementState state, float elapsedSecs);

        /// The element's running transition, or nothing when it has none.
        std::optional<Blend> Find(ID id, float elapsedSecs) const;

        /// The style indices and blend for the element's box `base` in `currentState`;
        /// fromStyleIndex == toStyleIndex and blendT == 1 when not transitioning.
        TransitionState Resolve(ID id, VisualBoxStyleIndex base,
                                ElementState currentState, float elapsedSecs) const;

        /// Once a frame, after the build: removes transitions that have ended and forgets the
        /// Selected of elements that weren't built this frame.
        void Purge(float elapsedSecs);

        /// Removes all active transitions, for instance after a reorder invalidates ID mappings.
        void Clear() { m_transitions.clear(); m_selected.clear(); }

        /// Test/debug seam: when disabled, states snap instantly (deterministic rendering).
        /// Disabling drops the active transitions.
        void SetEnabled(bool enabled) { m_enabled = enabled; if (!enabled) Clear(); }
        /// True unless SetEnabled(false) was called.
        bool IsEnabled() const { return m_enabled; }

        /// Reduced motion: every change is instant. Turning it on drops the active transitions.
        void SetReducedMotion(bool reduced) { m_reducedMotion = reduced; if (reduced) m_transitions.clear(); }
        bool IsReducedMotion() const { return m_reducedMotion; }

    private:
        struct TransitionEntry
        {
            ElementState     fromState;
            ElementState     toState;
            float            startTime; ///< elapsedSecs when this transition started
            TransitionTiming timing;
        };
        struct SelectedSeen
        {
            bool     selected;
            uint32_t frame;     ///< the frame it was last built in
        };

        std::unordered_map<ID, TransitionEntry> m_transitions;
        std::unordered_map<ID, SelectedSeen>    m_selected;
        uint32_t m_frame         = 0;
        bool     m_enabled       = true;
        bool     m_reducedMotion = false;
    };

    /// @}
} // namespace tachyon
