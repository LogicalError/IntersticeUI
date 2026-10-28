// TransitionManager: a state change mid-transition carries on from what is on screen.

#include <gtest/gtest.h>

#include <gui_controls/gui_transition_manager.h>
#include <gui_core/style/gui_style.h>

namespace tachyon
{
    namespace
    {
        constexpr VisualBoxStyleIndex kBox = VisualBoxStyleIndex(1);
        const ID kElement = [] { ID id; id.value.integer = 1; return id; }();
        constexpr float kDuration = TransitionManager::kDefaultDuration;

        // How much of `state`'s style the blend shows: 0 none of it, 1 only it.
        float Amount(const TransitionState& r, ElementState state)
        {
            const uint32_t style = ComputeGPUStyleIndex(kBox, state);
            float amount = 0.0f;
            if (r.fromStyleIndex == style) amount += 1.0f - r.blendT;
            if (r.toStyleIndex == style)   amount += r.blendT;
            return amount;
        }

        TEST(TransitionManager, AStateChangeBlendsToTheNewState)
        {
            TransitionManager transitions;
            transitions.OnStateChange(kElement, ElementState::None, ElementState::Hover, 0.0f);
            EXPECT_FLOAT_EQ(Amount(transitions.Resolve(kElement, kBox, ElementState::Hover, 0.0f), ElementState::Hover), 0.0f);
            const float halfway = Amount(transitions.Resolve(kElement, kBox, ElementState::Hover, kDuration / 2), ElementState::Hover);
            EXPECT_GT(halfway, 0.0f);
            EXPECT_LT(halfway, 1.0f);
            EXPECT_FLOAT_EQ(Amount(transitions.Resolve(kElement, kBox, ElementState::Hover, kDuration), ElementState::Hover), 1.0f);
        }

        // Hovering out before the hover blend finished runs back from where it got to,
        // instead of starting from the full hover style.
        TEST(TransitionManager, ReversingMidBlendCarriesOnFromWhereItIs)
        {
            TransitionManager transitions;
            transitions.OnStateChange(kElement, ElementState::None, ElementState::Hover, 0.0f);
            const float now = kDuration * 0.3f;
            const float shown = Amount(transitions.Resolve(kElement, kBox, ElementState::Hover, now), ElementState::Hover);
            ASSERT_GT(shown, 0.0f);
            ASSERT_LT(shown, 0.5f);

            transitions.OnStateChange(kElement, ElementState::Hover, ElementState::None, now);
            EXPECT_NEAR(Amount(transitions.Resolve(kElement, kBox, ElementState::None, now), ElementState::Hover), shown, 1e-5f)
                << "no jump when it turns around";

            // It runs back down, and is done sooner than a full blend.
            const float later = Amount(transitions.Resolve(kElement, kBox, ElementState::None, now + kDuration * 0.1f), ElementState::Hover);
            EXPECT_LT(later, shown);
            EXPECT_FLOAT_EQ(Amount(transitions.Resolve(kElement, kBox, ElementState::None, now + kDuration * 0.3f), ElementState::Hover), 0.0f);
        }

        // In and out several times quickly: never a jump.
        TEST(TransitionManager, RepeatedReversalsNeverJump)
        {
            TransitionManager transitions;
            ElementState state = ElementState::None;
            float now = 0.0f;
            float shown = 0.0f;
            for (int i = 0; i < 6; ++i)
            {
                const ElementState next = state == ElementState::None ? ElementState::Hover : ElementState::None;
                transitions.OnStateChange(kElement, state, next, now);
                EXPECT_NEAR(Amount(transitions.Resolve(kElement, kBox, next, now), ElementState::Hover), shown, 1e-5f) << "reversal " << i;
                state = next;
                now += kDuration * 0.35f;
                shown = Amount(transitions.Resolve(kElement, kBox, state, now), ElementState::Hover);
            }
        }

        // A third state mid-blend can only start from one of the two styles: the one the
        // blend is closer to.
        TEST(TransitionManager, AThirdStateStartsFromTheNearerStyle)
        {
            const ElementState pressed = ElementState::Hover | ElementState::Active;
            for (const float at : { 0.2f, 0.8f })
            {
                TransitionManager transitions;
                transitions.OnStateChange(kElement, ElementState::None, ElementState::Hover, 0.0f);
                const float now = kDuration * at;
                transitions.OnStateChange(kElement, ElementState::Hover, pressed, now);
                const TransitionState r = transitions.Resolve(kElement, kBox, pressed, now);
                const ElementState nearer = at < 0.5f ? ElementState::None : ElementState::Hover;
                EXPECT_FLOAT_EQ(Amount(r, nearer), 1.0f) << "at " << at;
            }
        }
    }
}
