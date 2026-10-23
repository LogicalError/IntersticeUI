#include <gui_core/layout/gui_constraints.h>

namespace interstice
{
    AnchorPlacement FlipHorizontal(AnchorPlacement p)
    {
        auto v = GetVerticalAxis(p);
        auto h = FlipAxis(GetHorizontalAxis(p));
        return MakeAnchorPlacement(h, v);
    }

    AnchorPlacement FlipVertical(AnchorPlacement p)
    {
        auto h = GetHorizontalAxis(p);
        auto v = FlipAxis(GetVerticalAxis(p));
        return MakeAnchorPlacement(h, v);
    }

    void PositionConstraintAxis::Apply(IntRect& rect, int axis, const IntRect& targetRect) const
    {
        int32_t& pos  = axis == 0 ? rect.x : rect.y;
        int32_t  size = axis == 0 ? rect.w : rect.h;
        int32_t  tPos = axis == 0 ? targetRect.x : targetRect.y;
        int32_t  tSize= axis == 0 ? targetRect.w : targetRect.h;

        switch (type)
        {
            case PositionType::Relative:
            {
                // Offsets shift the laid-out box: a far offset (right / bottom) moves it back.
                if (near != kAuto)
                    pos += near;
                else if (far != kAuto)
                    pos -= far;
                break;
            }

            case PositionType::Sticky:
            {
                // `near` is the sticky threshold, applied at the scroll-cascade clamp
                // in gui_layout_engine.cpp.
                break;
            }

            case PositionType::Fixed:
            case PositionType::Absolute:
            {
                // Insets from the target's edges: near from its start, far from its end.
                if (near != kAuto)
                    pos = tPos + near;
                else if (far != kAuto)
                    pos = tPos + tSize - size - far;
                else
                    pos = tPos;
                break;
            }

            case PositionType::Centered:
            {
                pos = tPos + ((tSize - size) / 2);
                break;
            }

            case PositionType::Anchored:
            {
                switch (anchorPlacement)
                {
                    default: break;
                    case AnchorPlacementAxis::OutsideMin: pos = near + tPos - size; break;
                    case AnchorPlacementAxis::CenterMin:  pos = near + tPos - (size / 2); break;
                    case AnchorPlacementAxis::InsideMin:   pos = near + tPos; break;
                    case AnchorPlacementAxis::Center:      pos = near + tPos + ((tSize - size) / 2); break;
                    case AnchorPlacementAxis::InsideMax:   pos = near + tPos + tSize - size; break;
                    case AnchorPlacementAxis::CenterMax:   pos = near + tPos + tSize - (size / 2); break;
                    case AnchorPlacementAxis::OutsideMax:  pos = near + tPos + tSize; break;
                }
                break;
            }
        }
    }

} // namespace interstice
