#include <gui_controls/gui_application.h>
#include <gui_controls/controls_color_internal.h>   // SetColorStyle (shared with controls_color.cpp)

#include <algorithm>
#include <cmath>

// Gradient controls: GradientSlider, GradientField, EmitGradientEditor.

namespace
{
    // Create/update a VisualColorStyleIndex that renders a user Gradient horizontally.
    void SetGradientPreviewStyle(interstice::StyleManager& sm, interstice::VisualColorStyleIndex& idx,
                                 const interstice::Gradient& gradient)
    {
        interstice::Gradient g = gradient;
        g.type = interstice::GradientType::Linear;
        interstice::ColorStyle cs = interstice::ColorStyle::Create(g, interstice::ColorType::LinearGradient);
        cs.SetTransformation(interstice::float3(0, 1, 0), interstice::float3(1, 0, 0)); // horizontal
        if (idx == interstice::VisualColorStyleIndex::Invalid)
            idx = sm.CreateColorStyle(cs);
        else
            sm.UpdateColorStyle(idx, cs);
    }
}


namespace interstice
{
    // ---------------------------------------------------------------------------
    // Gradient slider: horizontal gradient bar + triangle indicator
    // ---------------------------------------------------------------------------

    void GUIApplication::GradientSlider(ID id, float& value, float minVal, float maxVal,
                                        std::string_view label,
                                        VisualColorStyleIndex gradientStyle,
                                        VisualColorStyleIndex triangleStyle,
                                        LayoutOverride&& constraints)
    {
        const float range = maxVal - minVal;
        if (range <= 0.0f) return;

        // Label
        DragLabel(label, LayoutOverride{}.FixedWidth(styleManager.Px(40)).NoShrink().NoGrow());

        // Outer wrapper (horizontal row: gradient bar + numeric field)
        Element wrap {};
        wrap.id       = idManager.AllocateDynamicID("gs_wrap");
        wrap.styleRef = theme.colorGradientSliderRowStyleRef;
        wrap.state    = guiStateManager.GetCurrentState(wrap.id);
        RegisterElement(std::move(wrap), std::move(constraints));
        VERIFY(elementStorage.BeginChildren());

        // ---- Gradient bar ----
        {
            Element bar {};
            bar.id       = id;
            bar.styleRef = theme.colorGradientSliderStyleRef;
            bar.state    = guiStateManager.GetCurrentState(id);

            float* valuePtr = &value;
            bar.eventHandler = [this, id, valuePtr, minVal, maxVal](ElementEvent&& event) -> bool
            {
                auto setFromMouse = [&]()
                {
                    if (!event.rect) return;
                    auto& r = event.rect->rect;
                    if (r.w <= 0) return;
                    const int32_t cursorX = event.LocalCursor(elementStorage).x;
                    float t = std::clamp(float(cursorX - r.x) / float(r.w), 0.0f, 1.0f);
                    *valuePtr = minVal + t * (maxVal - minVal);
                };
                switch (event.type)
                {
                    case EventType::MouseDown:
                        if (!event.state.LeftPressed()) break;
                        guiStateManager.Interaction().SetCaptureID(id);
                        guiStateManager.Interaction().SetFocusID(id);
                        setFromMouse();
                        return true;
                    case EventType::MouseMove:
                        if (!event.state.LeftDown()) break;
                        setFromMouse();
                        return true;
                    case EventType::MouseUp:
                        guiStateManager.Interaction().ReleaseCaptureID();
                        return true;
                    case EventType::KeyDown:
                    {
                        float step = (maxVal - minVal) * 0.01f;
                        float bigStep = (maxVal - minVal) * 0.1f;
                        switch (event.state.lastKey)
                        {
                            case KeyCode::Left:     *valuePtr = std::clamp(*valuePtr - step, minVal, maxVal); return true;
                            case KeyCode::Right:    *valuePtr = std::clamp(*valuePtr + step, minVal, maxVal); return true;
                            case KeyCode::Home:     *valuePtr = minVal; return true;
                            case KeyCode::End:      *valuePtr = maxVal; return true;
                            case KeyCode::PageUp:   *valuePtr = std::clamp(*valuePtr - bigStep, minVal, maxVal); return true;
                            case KeyCode::PageDown: *valuePtr = std::clamp(*valuePtr + bigStep, minVal, maxVal); return true;
                            default: break;
                        }
                        break;
                    }
                    default: break;
                }
                return false;
            };

            auto ref = RegisterControl(std::move(bar), LayoutOverride{}.FlexGrow(1));

            // Custom draw: gradient fill + triangle indicator hanging below the bar.
            // Relies on overflow:visible on .color-gradient-slider and its row so the triangle isn't clipped.
            float currentT = std::clamp((value - minVal) / range, 0.0f, 1.0f);
            VERIFY(elementStorage.AddCustomDraw(ref,
                [gradientStyle, triangleStyle, currentT, triSize = styleManager.Px(5)](CustomDrawContext& ctx)
                {
                    // Fill the FULL outer bar with the gradient; indicator sits just
                    // outside the outer bottom edge.
                    const auto outer = ctx.ElementRect();
                    const auto inner = ctx.ElementInnerRect();
                    const int w = outer.w;
                    const int h = outer.h;
                    if (w <= 0 || h <= 0) return;
                    const int X = outer.x - inner.x;
                    const int Y = outer.y - inner.y;

                    // Gradient fill
                    ctx.RenderQuad(IntRect{ .position = { X, Y }, .size = { w, h } }, gradientStyle);

                    // Upward-pointing triangle indicator below the bar.
                    int triX = X + int(currentT * float(w));
                    int triY = Y + h;
                    ctx.RenderTriangle(
                        { float(triX),           float(triY)           },
                        { float(triX - triSize), float(triY + triSize) },
                        { float(triX + triSize), float(triY + triSize) },
                        triangleStyle);
                }));
        }

        // ---- Numeric field ----
        Field(value, TextEditBehavior::ContinuousUpdate,
                NumericFieldConfig<float>{ .minVal = minVal, .maxVal = maxVal },
                LayoutOverride{}.FixedWidth(styleManager.Px(55)).NoGrow());

        VERIFY(elementStorage.EndChildren());
    }

    // ---------------------------------------------------------------------------
    // GradientField — gradient swatch + editor popup
    // ---------------------------------------------------------------------------

    bool GUIApplication::GradientField(Gradient& value, LayoutOverride&& constraints)
    {
        const Gradient before = value;

        // ---- Gradient swatch (inline preview bar) ----
        ID swatchID = idManager.AllocateDynamicID("gradient_swatch");
        auto& ge = m_gradientEditorSub->State();
        const bool editorIsOpen = (ge.triggerID == swatchID); // this field's editor, not just any

        // Get or create a persistent style index for this swatch
        NoteSwatchClaim(swatchID);
        auto& swatchStyle = m_gradientEditorSub->ClaimSwatchStyle(swatchID);
        SetGradientPreviewStyle(styleManager, swatchStyle, value);

        Element swatch {};
        swatch.id              = swatchID;
        swatch.styleRef        = theme.gradientSwatchStyleRef;
        swatch.state           = guiStateManager.GetCurrentState(swatchID);
        swatch.colorStyleIndex = swatchStyle;
        swatch.eventHandler = MakeButtonHandler([this, &value, swatchID, editorIsOpen]()
        {
            auto& ge = m_gradientEditorSub->State();
            if (editorIsOpen)
            {
                ge.triggerID = ID::Invalid();
            } else
            {
                ge.triggerID = swatchID;
                if (ge.windowID == ID::Invalid())
                    ge.windowID = idManager.CreateStaticID();
                ge.workingCopy = value;
                ge.selectedStop = 0;
                ge.drag = GradientEditorState::DragTarget::None;
            }
        });

        const ElementRef swatchRef = RegisterControl(std::move(swatch), std::move(constraints));

        // The stops, as diamonds on the bar's bottom edge: light with a dark outline.
        {
            std::array<float, Gradient::kMaxStops> offsets{};
            const int stopCount = std::min(int(value.stopCount), Gradient::kMaxStops);
            for (int i = 0; i < stopCount; i++)
                offsets[size_t(i)] = value.stops[size_t(i)].GetOffset();
            const VisualColorStyleIndex fill    = styleManager.RegisterColorStyle(ColorStyle::Create(theme.GetColor("--text-1", color4f{ 0.9f, 0.9f, 0.9f, 1.0f })));
            const VisualColorStyleIndex outline = styleManager.RegisterColorStyle(ColorStyle::Create(theme.GetColor("--bg-app", color4f{ 0.05f, 0.05f, 0.05f, 1.0f })));
            VERIFY(elementStorage.AddCustomDraw(swatchRef,
                [offsets, stopCount, fill, outline, half = float(styleManager.Px(4)), edge = float(styleManager.Px(1))](CustomDrawContext& ctx)
                {
                    const auto outer = ctx.ElementRect();
                    if (outer.w <= 0 || outer.h <= 0) return;
                    auto diamond = [&ctx](float2 c, float r, VisualColorStyleIndex color) {
                        ctx.RenderTriangle({ c.x - r, c.y }, { c.x, c.y - r }, { c.x + r, c.y }, color);
                        ctx.RenderTriangle({ c.x - r, c.y }, { c.x + r, c.y }, { c.x, c.y + r }, color);
                    };
                    for (int i = 0; i < stopCount; i++)
                    {
                        // Kept inside the bar's ends so the end stops stay whole.
                        const float x = float(outer.x) + half + offsets[size_t(i)] * float(std::max(0, outer.w) - 2.0f * half);
                        const float2 center{ x, float(outer.y + outer.h) };
                        diamond(center, half + edge, outline);
                        diamond(center, half, fill);
                    }
                }));
        }

        // ---- Editor popup ----
        if (editorIsOpen && ge.windowID != ID::Invalid())
        {
            auto popupPos = LayoutOverride{}.Anchored(swatchID,
                AnchorPlacement::OutsideBottom | AnchorPlacement::InsideLeft,
                {0, int16_t(styleManager.Px(4))}, AutoFlipMode::FlipAvoidTarget);

            if (BeginWindow(ge.windowID, "Gradient Editor", std::move(popupPos), theme.gradientEditorStyleRef,
                            ElementFlags::WindowUserControlled | ElementFlags::Shadows))
            {
                EmitGradientEditor(value);
                EndWindow();
            }
            else
            {
                ge.triggerID = ID::Invalid();
            }
        }

        return !(value == before);
    }

    // ---------------------------------------------------------------------------
    // EmitGradientEditor — popup content for the gradient editor
    // ---------------------------------------------------------------------------

    void GUIApplication::EmitGradientEditor(Gradient& outGradient)
    {
        auto& ge = m_gradientEditorSub->State();
        auto& g  = ge.workingCopy;

        // Clamp selectedStop to valid range
        if (ge.selectedStop >= g.stopCount)
            ge.selectedStop = g.stopCount > 0 ? g.stopCount - 1 : 0;

        // Update the preview gradient style
        SetGradientPreviewStyle(styleManager, ge.previewGradientStyle, g);

        // Update marker styles
        SetColorStyle(styleManager, ge.stopMarkerColor, color4f{0.3f, 0.3f, 0.3f, 1.0f});
        SetColorStyle(styleManager, ge.stopMarkerSelected, color4f{1.0f, 1.0f, 1.0f, 1.0f});

        // ---- Gradient preview bar + stop handles ----
        {
            ID barID = idManager.AllocateDynamicID("ge_bar");
            Element bar {};
            bar.id       = barID;
            bar.styleRef = theme.gradientEditorBarStyleRef;
            bar.state    = guiStateManager.GetCurrentState(barID);

            bar.eventHandler = [this, barID](ElementEvent&& event) -> bool
            {
                auto& ge = m_gradientEditorSub->State();
                auto& g  = ge.workingCopy;

                auto localT = [&](float& t) -> bool
                {
                    if (!event.rect) return false;
                    auto& r = event.rect->rect;
                    if (r.w <= 0) return false;
                    const int32_t cursorX = event.LocalCursor(elementStorage).x;
                    t = std::clamp(float(cursorX - r.x) / float(r.w), 0.0f, 1.0f);
                    return true;
                };

                auto hitTestStop = [&](int& hitIndex) -> bool
                {
                    if (!event.rect) return false;
                    auto& r = event.rect->rect;
                    if (r.w <= 0) return false;
                    const int32_t cursorX = event.LocalCursor(elementStorage).x;
                    float mx = float(cursorX - r.x);
                    for (int i = 0; i < g.stopCount; i++)
                    {
                        float sx = g.stops[i].GetOffset() * float(r.w);
                        if (std::abs(mx - sx) < styleManager.Px(6.0f))
                        {
                            hitIndex = i;
                            return true;
                        }
                    }
                    return false;
                };

                switch (event.type)
                {
                    case EventType::MouseDown:
                    {
                        if (!event.state.LeftPressed()) break;
                        int hit = -1;
                        if (hitTestStop(hit))
                        {
                            ge.selectedStop = hit;
                            ge.drag = GradientEditorState::DragTarget::Stop;
                            ge.dragStopIndex = hit;
                            guiStateManager.Interaction().SetCaptureID(barID);
                            guiStateManager.Interaction().SetFocusID(barID);
                            return true;
                        }
                        // Click on empty space → add new stop
                        float t;
                        if (localT(t) && g.stopCount < Gradient::kMaxStops)
                        {
                            // Sample color from gradient at click position
                            color4b samples[256];
                            g.WriteTo(std::span<color4b>(samples, 256));
                            int sampleIdx = std::clamp(int(t * 255.0f), 0, 255);
                            color4b newColor = samples[sampleIdx];

                            g.AddStop(newColor, t);
                            ge.selectedStop = g.stopCount - 1;
                            guiStateManager.Interaction().SetFocusID(barID);
                            return true;
                        }
                        break;
                    }
                    case EventType::MouseMove:
                    {
                        if (ge.drag == GradientEditorState::DragTarget::Stop &&
                            ge.dragStopIndex >= 0 && ge.dragStopIndex < g.stopCount)
                        {
                            float t;
                            if (localT(t))
                            {
                                // Remember the stop being dragged
                                auto draggedStop = g.stops[ge.dragStopIndex];
                                draggedStop.SetOffset(t);
                                g.stops[ge.dragStopIndex] = draggedStop;

                                // Sort stops by offset, track the dragged one
                                auto stopColor = draggedStop.GetColor4b();
                                auto stopOffset = t;
                                std::sort(g.stops.begin(), g.stops.begin() + g.stopCount,
                                    [](const ColorStop& a, const ColorStop& b) {
                                        return a.GetOffset() < b.GetOffset();
                                    });
                                // Find the dragged stop after sort
                                for (int i = 0; i < g.stopCount; i++)
                                {
                                    if (g.stops[i].GetColor4b() == stopColor &&
                                        std::abs(g.stops[i].GetOffset() - stopOffset) < 0.0001f)
                                    {
                                        ge.dragStopIndex = i;
                                        ge.selectedStop = i;
                                        break;
                                    }
                                }
                            }
                            return true;
                        }
                        break;
                    }
                    case EventType::MouseUp:
                    {
                        if (ge.drag != GradientEditorState::DragTarget::None)
                        {
                            ge.drag = GradientEditorState::DragTarget::None;
                            guiStateManager.Interaction().ReleaseCaptureID();
                            return true;
                        }
                        break;
                    }
                    case EventType::KeyDown:
                    {
                        if (event.state.lastKey == KeyCode::Delete &&
                            g.stopCount > 2 &&
                            ge.selectedStop >= 0 && ge.selectedStop < g.stopCount)
                        {
                            // Remove selected stop by shifting
                            for (int i = ge.selectedStop; i < g.stopCount - 1; i++)
                                g.stops[i] = g.stops[i + 1];
                            g.stopCount--;
                            if (ge.selectedStop >= g.stopCount)
                                ge.selectedStop = g.stopCount - 1;
                            return true;
                        }
                        break;
                    }
                    default: break;
                }
                return false;
            };

            auto barRef = RegisterControl(std::move(bar), LayoutOverride{}.FixedHeight(styleManager.Px(30)));

            // Custom draw: gradient fill + stop handle triangles
            int stopCount = g.stopCount;
            int selectedIdx = ge.selectedStop;
            std::array<float, Gradient::kMaxStops> offsets {};
            std::array<color4b, Gradient::kMaxStops> stopColors {};
            for (int i = 0; i < stopCount; i++)
            {
                offsets[i] = g.stops[i].GetOffset();
                stopColors[i] = g.stops[i].GetColor4b();
            }
            auto previewStyle = ge.previewGradientStyle;
            auto markerColor = ge.stopMarkerColor;
            auto markerSelected = ge.stopMarkerSelected;

            VERIFY(elementStorage.AddCustomDraw(barRef,
                [previewStyle, markerColor, markerSelected, triSize = styleManager.Px(6), tail = styleManager.Px(2),
                    stopCount, selectedIdx, offsets, stopColors](CustomDrawContext& ctx)
                {
                    // Fill the FULL outer bar; stop markers sit just outside the
                    // outer bottom edge.
                    const auto outer = ctx.ElementRect();
                    const auto inner = ctx.ElementInnerRect();
                    const int w = outer.w;
                    const int h = outer.h;
                    if (w <= 0 || h <= 0) return;
                    const int X = outer.x - inner.x;
                    const int Y = outer.y - inner.y;

                    // Gradient fill
                    ctx.RenderQuad(IntRect{ .position = { X, Y }, .size = { w, h } }, previewStyle);

                    // Stop handle triangles below the bar
                    for (int i = 0; i < stopCount; i++)
                    {
                        int tx = X + int(offsets[i] * float(w));
                        int ty = Y + h;
                        auto style = (i == selectedIdx) ? markerSelected : markerColor;
                        ctx.RenderTriangle(
                            { float(tx),           float(ty)               },
                            { float(tx - triSize), float(ty + triSize + tail) },
                            { float(tx + triSize), float(ty + triSize + tail) },
                            style);
                    }
                }));
        }

        // ---- Selected stop controls ----
        if (ge.selectedStop >= 0 && ge.selectedStop < g.stopCount)
        {
            ge.selectedStopColor = g.stops[ge.selectedStop].GetColor4f();

            DragLabel("Color");
            if (ColorField(ge.selectedStopColor, false, LayoutOverride{}.BreakAfter()))
            {
                g.stops[ge.selectedStop].SetColor(ge.selectedStopColor);
            }

            ge.selectedStopPosition = g.stops[ge.selectedStop].GetOffset();
            if (Field(ge.selectedStopPosition, TextEditBehavior::ContinuousUpdate,
                        NumericFieldConfig<float>{ .stepPerPixel = 0.001, .minVal = 0.0f, .maxVal = 1.0f },
                        LayoutOverride{}.BreakAfter()))
            {
                g.stops[ge.selectedStop].SetOffset(ge.selectedStopPosition);
                // Re-sort stops by offset
                auto selColor = g.stops[ge.selectedStop].GetColor4b();
                auto selOffset = ge.selectedStopPosition;
                std::sort(g.stops.begin(), g.stops.begin() + g.stopCount,
                    [](const ColorStop& a, const ColorStop& b) {
                        return a.GetOffset() < b.GetOffset();
                    });
                for (int i = 0; i < g.stopCount; i++)
                {
                    if (g.stops[i].GetColor4b() == selColor &&
                        std::abs(g.stops[i].GetOffset() - selOffset) < 0.0001f)
                    {
                        ge.selectedStop = i;
                        break;
                    }
                }
            }
        }

        // ---- Color space tabs ----
        {
            struct TabDef { ColorSpace space; std::string_view label; };
            constexpr TabDef tabs[] = {
                { ColorSpace::sRGB,      "sRGB" },
                { ColorSpace::LinearRGB, "Lin"  },
                { ColorSpace::HSL,       "HSL"  },
                { ColorSpace::HSV,       "HSV"  },
                { ColorSpace::Lab,       "Lab"  },
                { ColorSpace::Oklab,     "OkL"  },
            };

            Element tabRow {};
            tabRow.id       = idManager.AllocateDynamicID("ge_tab_row");
            tabRow.styleRef = theme.gradientEditorTabRowStyleRef;
            tabRow.state    = guiStateManager.GetCurrentState(tabRow.id);
            RegisterElement(std::move(tabRow), LayoutOverride{}.BreakAfter());
            VERIFY(elementStorage.BeginChildren());

            for (auto& tab : tabs)
            {
                bool isActive = (g.colorSpace == tab.space);
                if (isActive) guiStateManager.Construction().PushSelected();

                ColorSpace targetSpace = tab.space;
                Button(tab.label, [this, targetSpace]()
                {
                    m_gradientEditorSub->State().workingCopy.colorSpace = targetSpace;
                });

                if (isActive) guiStateManager.Construction().PopSelected();
            }

            VERIFY(elementStorage.EndChildren());
        }

        // Write working copy back to output every frame
        outGradient = g;
    }
}
