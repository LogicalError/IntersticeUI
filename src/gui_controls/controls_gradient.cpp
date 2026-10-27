#include <gui_controls/gui_application.h>
#include <array>
#include <string>
#include <cstdio>
#include <gui_controls/controls_color_internal.h>   // SetColorStyle (shared with controls_color.cpp)

#include <algorithm>
#include <cmath>

// Gradient controls: GradientSlider, GradientField, EmitGradientEditor.

namespace
{
    // Create/update a VisualColorStyleIndex that renders a user Gradient horizontally.
    void SetGradientPreviewStyle(tachyon::StyleManager& sm, tachyon::VisualColorStyleIndex& idx,
                                 const tachyon::Gradient& gradient)
    {
        tachyon::Gradient g = gradient;
        g.type = tachyon::GradientType::Linear;
        tachyon::ColorStyle cs = tachyon::ColorStyle::Create(g, tachyon::ColorType::LinearGradient);
        cs.SetTransformation(tachyon::float3(0, 1, 0), tachyon::float3(1, 0, 0)); // horizontal
        if (idx == tachyon::VisualColorStyleIndex::Invalid)
            idx = sm.CreateColorStyle(cs);
        else
            sm.UpdateColorStyle(idx, cs);
    }
}


namespace tachyon
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
        DragLabel(label, LayoutOverride{}.FixedWidth(40).NoShrink().NoGrow());

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
                LayoutOverride{}.FixedWidth(55).NoGrow());

        VERIFY(elementStorage.EndChildren());
    }

    // ---------------------------------------------------------------------------
    // GradientField - gradient swatch + editor popup
    // ---------------------------------------------------------------------------

    bool GUIApplication::GradientField(ID id, Gradient& value, LayoutOverride&& constraints)
    {
        if (value.stopCount > Gradient::kMaxStops)
        {
            ReportMisuse("GUIApplication: a gradient with more stops than Gradient::kMaxStops (clamped)");
            value.stopCount = Gradient::kMaxStops;
        }
        const Gradient before = value;

        // ---- Gradient swatch (inline preview bar) ----
        const ID swatchID = id;
        const std::string context = PendingLabelText();
        auto& ge = m_gradientEditorSub->State();
        // OK keeps the gradient; Cancel and the close button restore the original.
        if (ge.triggerID == swatchID && ge.closeRequest != GradientEditorState::CloseRequest::None)
        {
            if (ge.closeRequest == GradientEditorState::CloseRequest::Cancel)
                value = ge.originalGradient;
            ge.closeRequest = GradientEditorState::CloseRequest::None;
            ge.triggerID = ID::Invalid();
        }
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
        swatch.eventHandler = MakeButtonHandler([this, &value, swatchID, editorIsOpen, context]()
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
                ge.originalGradient = value;
                ge.context = context;
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

            if (BeginWindow(ge.windowID, "Gradient Editor", theme.gradientEditorStyleRef, std::move(popupPos),
                            ElementFlags::WindowCanMove | ElementFlags::WindowNoCaption | ElementFlags::Shadows))
            {
                EmitGradientEditor(value);
            }
            else
            {
                ge.triggerID = ID::Invalid();
            }
            EndWindow();
        }

        return !(value == before);
    }

    // ---------------------------------------------------------------------------
    // Gradient editor dialog: presets, the bar with its stops, the selected stop,
    // blend space and actions at the left, a stops table at the right; Cancel / OK.
    // ---------------------------------------------------------------------------

    namespace
    {
        // Built-in presets, as stop colors spread evenly.
        struct GradientPreset { std::string_view name; std::initializer_list<uint32_t> colors; bool fade = false; };
        constexpr uint32_t Rgb(uint32_t hex) { return hex; }
        const GradientPreset kGradientPresets[] = {
            { "Grayscale", { Rgb(0x000000), Rgb(0xFFFFFF) } },
            { "Sky",       { Rgb(0x0B1D51), Rgb(0x3B82F6), Rgb(0xA5F3FC) } },
            { "Heat",      { Rgb(0x1A0B00), Rgb(0xB85C10), Rgb(0xFFD27A), Rgb(0xFFFFFF) } },
            { "Viridis",   { Rgb(0x440154), Rgb(0x3B528B), Rgb(0x21918C), Rgb(0x5EC962), Rgb(0xFDE725) } },
            { "Hue",       { Rgb(0xFF0000), Rgb(0xFFFF00), Rgb(0x00FF00), Rgb(0x00FFFF), Rgb(0x0000FF), Rgb(0xFF00FF), Rgb(0xFF0000) } },
            { "Fade",      { Rgb(0x2D3BFF), Rgb(0x2D3BFF) }, true },
        };

        Gradient MakePreset(const GradientPreset& preset, ColorSpace space)
        {
            Gradient g;
            g.type = GradientType::Linear;
            g.colorSpace = space;
            const int n = int(preset.colors.size());
            int i = 0;
            for (uint32_t hex : preset.colors)
            {
                const float a = (preset.fade && i == n - 1) ? 0.0f : 1.0f;
                g.AddStop(color4f{ float((hex >> 16) & 0xFF) / 255.0f, float((hex >> 8) & 0xFF) / 255.0f, float(hex & 0xFF) / 255.0f, a },
                          n > 1 ? float(i) / float(n - 1) : 0.0f);
                ++i;
            }
            return g;
        }

        // Keeps the stops sorted by location; returns where the one that was at `index` went.
        int SortStops(Gradient& g, int index)
        {
            if (index < 0 || index >= g.stopCount) return index;
            const ColorStop tracked = g.stops[size_t(index)];
            std::stable_sort(g.stops.begin(), g.stops.begin() + g.stopCount,
                             [](const ColorStop& a, const ColorStop& b) { return a.GetOffset() < b.GetOffset(); });
            for (int i = 0; i < g.stopCount; i++)
                if (g.stops[size_t(i)] == tracked) return i;
            return index;
        }

        void RemoveStop(Gradient& g, int index)
        {
            if (g.stopCount <= 2 || index < 0 || index >= g.stopCount) return;
            for (int i = index; i < g.stopCount - 1; i++)
                g.stops[size_t(i)] = g.stops[size_t(i + 1)];
            g.stopCount--;
        }

        std::string StopHex(const ColorStop& stop)
        {
            const color4b c = stop.GetColor4b();
            char buf[24];
            if (c.a == 255) std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", c.r, c.g, c.b);
            else            std::snprintf(buf, sizeof(buf), "#%02X%02X%02X %d%%", c.r, c.g, c.b, int(std::lround(float(c.a) / 2.55f)));
            return buf;
        }

        std::string Percent(float t)
        {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%.1f%%", double(t) * 100.0);
            return buf;
        }
    }

    void GUIApplication::EmitGradientEditor(Gradient& outGradient)
    {
        if (outGradient.stopCount > Gradient::kMaxStops)
        {
            ReportMisuse("GUIApplication: a gradient with more stops than Gradient::kMaxStops (clamped)");
            outGradient.stopCount = Gradient::kMaxStops;
        }
        auto& ge = m_gradientEditorSub->State();
        auto& g  = ge.workingCopy;
        if (ge.selectedStop >= g.stopCount)
            ge.selectedStop = g.stopCount > 0 ? g.stopCount - 1 : 0;

        SetGradientPreviewStyle(styleManager, ge.previewGradientStyle, g);
        SetColorStyle(styleManager, ge.stopEdgeColor, theme.GetColor("--text-1", color4f{ 0.91f, 0.92f, 0.94f, 1.0f }));
        SetColorStyle(styleManager, ge.stopOutlineColor, color4f{ 0.0f, 0.0f, 0.0f, 0.5f });
        SetColorStyle(styleManager, ge.stopHaloColor, theme.GetColor("--accent-soft", color4f{ 0.42f, 0.61f, 1.0f, 0.16f }));

        const ColorSwatchLook look = SwatchLook();
        const auto requestClose = [this](GradientEditorState::CloseRequest request) {
            return [this, request]() { m_gradientEditorSub->State().closeRequest = request; };
        };

        // ---- Header ----
        // The header is the dialog's title bar: it drags the window.
        BeginTitleBar(idManager.AllocateDynamicID("ge_header"), theme.dialogHeaderStyleRef, LayoutOverride{}.NoShrink());
        Text(idManager.AllocateDynamicID("ge_title"), "Gradient", theme.dialogTitleStyleRef);
        Text(idManager.AllocateDynamicID("ge_context"), DialogContext(ge.context, "Gradient"), theme.dialogContextStyleRef, LayoutOverride{}.FlexGrow(1));
        Button(idManager.AllocateDynamicID("ge_close"), theme.tabCloseIcon, requestClose(GradientEditorState::CloseRequest::Cancel),
               theme.dialogCloseStyleRef);
        SetTooltip("Close without changing the gradient");
        EndTitleBar();

        // ---- Presets ----
        BeginHorizontal(idManager.AllocateDynamicID("ge_presets"), theme.gradientEditorPresetsStyleRef, LayoutOverride{}.NoShrink());
        {
            Text(idManager.AllocateDynamicID("ge_presets_label"), "Presets", theme.gradientEditorSectionLabelStyleRef);
            // A chip draws its gradient over the checkerboard; the one matching the gradient is ringed.
            auto chip = [&](ID id, const Gradient& preset, std::string_view name)
            {
                NoteSwatchClaim(id);
                auto& style = m_gradientEditorSub->ClaimSwatchStyle(id);
                SetGradientPreviewStyle(styleManager, style, preset);
                Gradient asIs = preset;
                asIs.colorSpace = g.colorSpace;
                const bool current = (asIs == g);
                const ElementRef ref = RegisterControl(Element{ .id = id,
                    .eventHandler = MakeButtonHandler([this, preset]() {
                        auto& ge = m_gradientEditorSub->State();
                        const ColorSpace space = ge.workingCopy.colorSpace;
                        ge.workingCopy = preset;
                        ge.workingCopy.colorSpace = space;
                        ge.selectedStop = 0;
                    }),
                    .styleRef = theme.gradientEditorPresetStyleRef }, LayoutOverride{});
                SetTooltip(name);
                VERIFY(elementStorage.AddCustomDraw(ref,
                    [look, style, current, accent = theme.accentColor, panel = theme.panelColor,
                     radius = float(styleManager.Px(4)), gap = styleManager.Px(2)](CustomDrawContext& ctx)
                    {
                        const IntRect r = ctx.ElementRect();
                        if (current)
                        {
                            const IntRect ring{ .position = { r.x - 2 * gap, r.y - 2 * gap }, .size = { r.w + 4 * gap, r.h + 4 * gap } };
                            const IntRect gapRect{ .position = { r.x - gap, r.y - gap }, .size = { r.w + 2 * gap, r.h + 2 * gap } };
                            ctx.RenderQuad(ring, accent, radius + float(2 * gap));
                            ctx.RenderQuad(gapRect, panel, radius + float(gap));
                        }
                        ColorSwatchLook chipLook = look;
                        chipLook.radius = radius;
                        DrawCheckerboard(ctx, r, r, chipLook);
                        ctx.RenderQuad(r, style, radius);
                    }));
            };
            const ID builtInRange = idManager.AllocateRangedID(uint32_t(std::size(kGradientPresets)), "ge_preset");
            for (uint32_t i = 0; i < uint32_t(std::size(kGradientPresets)); ++i)
                chip(idManager.GetSubID(builtInRange, i), MakePreset(kGradientPresets[i], g.colorSpace), kGradientPresets[i].name);
            if (!ge.savedPresets.empty())
            {
                const ID savedRange = idManager.AllocateRangedID(uint32_t(ge.savedPresets.size()), "ge_saved_preset");
                for (uint32_t i = 0; i < uint32_t(ge.savedPresets.size()); ++i)
                    chip(idManager.GetSubID(savedRange, i), ge.savedPresets[i], "Saved preset");
            }

            Element spacer{ .id = idManager.AllocateDynamicID("ge_presets_spacer"), .styleRef = theme.emptyHorzStyleRef, .flags = ElementFlags::NoInput };
            RegisterElement(std::move(spacer), LayoutOverride{}.FlexGrow(1));
            Button(idManager.AllocateDynamicID("ge_save_preset"), { theme.addIcon, "Save preset" }, [this]() {
                auto& ge = m_gradientEditorSub->State();
                ge.savedPresets.push_back(ge.workingCopy);
            }, theme.dialogSmallButtonStyleRef);
            SetTooltip("Keep this gradient as a preset");
        }
        EndHorizontal();

        // ---- The bar and its stops ----
        {
            const ID barID = idManager.AllocateDynamicID("ge_bar");
            const int barH    = styleManager.Px(44);
            const int stopBox = styleManager.Px(18);
            const int pointer = styleManager.Px(6);
            const int inset   = styleManager.Px(24);   // the bar's side inset, so the end stops fit
            const int removeDistance = styleManager.Px(40);

            Element bar{};
            bar.id       = barID;
            bar.styleRef = theme.gradientEditorBarStyleRef;
            bar.state    = guiStateManager.GetCurrentState(barID);
            bar.eventHandler = [this, barID, barH, stopBox, pointer, inset, removeDistance](ElementEvent&& event) -> bool
            {
                auto& ge = m_gradientEditorSub->State();
                auto& g  = ge.workingCopy;
                if (!event.rect) return false;
                const IntRect& r = event.rect->rect;
                const int barW = r.w - 2 * inset;
                if (barW <= 0) return false;
                const int2 c = event.LocalCursor(elementStorage);
                const float t = std::clamp(float(c.x - r.x - inset) / float(barW), 0.0f, 1.0f);
                const int top = r.y + (r.h - (barH + pointer + stopBox)) / 2;
                const bool belowBar = c.y >= top + barH;

                auto hitStop = [&]() -> int {
                    if (!belowBar || c.y > top + barH + pointer + stopBox) return -1;
                    for (int i = 0; i < g.stopCount; i++)
                    {
                        const int sx = r.x + inset + int(std::lround(g.stops[size_t(i)].GetOffset() * float(barW)));
                        if (std::abs(c.x - sx) <= stopBox / 2) return i;
                    }
                    return -1;
                };

                switch (event.type)
                {
                    case EventType::MouseDown:
                    {
                        if (!event.state.LeftPressed()) break;
                        guiStateManager.Interaction().SetFocusID(barID);
                        const int hit = hitStop();
                        if (hit >= 0)
                        {
                            ge.selectedStop  = hit;
                            ge.drag          = GradientEditorState::DragTarget::Stop;
                            ge.dragStopIndex = hit;
                            guiStateManager.Interaction().SetCaptureID(barID);
                            return true;
                        }
                        // A click on the bar adds a stop with the gradient's color there.
                        if (!belowBar && c.y >= top && g.stopCount < Gradient::kMaxStops)
                        {
                            color4b samples[256];
                            g.WriteTo(std::span<color4b>(samples, 256));
                            g.AddStop(samples[std::clamp(int(t * 255.0f), 0, 255)], t);
                            ge.selectedStop = SortStops(g, g.stopCount - 1);
                            return true;
                        }
                        break;
                    }
                    case EventType::MouseMove:
                        if (ge.drag == GradientEditorState::DragTarget::Stop && ge.dragStopIndex >= 0 && ge.dragStopIndex < g.stopCount)
                        {
                            g.stops[size_t(ge.dragStopIndex)].SetOffset(t);
                            ge.dragStopIndex = ge.selectedStop = SortStops(g, ge.dragStopIndex);
                            ge.dragOff = (c.y > top + barH + pointer + stopBox + removeDistance) && g.stopCount > 2;
                            return true;
                        }
                        break;
                    case EventType::MouseUp:
                        if (ge.drag != GradientEditorState::DragTarget::None)
                        {
                            // Dragged well below the bar: the stop goes.
                            if (ge.dragOff)
                            {
                                RemoveStop(g, ge.dragStopIndex);
                                ge.selectedStop = std::min(ge.dragStopIndex, g.stopCount - 1);
                            }
                            ge.drag = GradientEditorState::DragTarget::None;
                            ge.dragOff = false;
                            guiStateManager.Interaction().ReleaseCaptureID();
                            return true;
                        }
                        break;
                    case EventType::KeyDown:
                        if (event.state.lastKey == KeyCode::Delete && g.stopCount > 2)
                        {
                            RemoveStop(g, ge.selectedStop);
                            ge.selectedStop = std::min(ge.selectedStop, g.stopCount - 1);
                            return true;
                        }
                        break;
                    default: break;
                }
                return false;
            };
            const ElementRef barRef = RegisterControl(std::move(bar), LayoutOverride{});

            struct StopDraw { float t; VisualColorStyleIndex color; };
            std::array<StopDraw, Gradient::kMaxStops> stops{};
            const int stopCount = g.stopCount;
            for (int i = 0; i < stopCount; i++)
            {
                const ID stopStyleID = idManager.AllocateDynamicID("ge_stop_color");
                NoteSwatchClaim(stopStyleID);
                auto& stopStyle = m_gradientEditorSub->ClaimSwatchStyle(stopStyleID);
                SetColorStyle(styleManager, stopStyle, g.stops[size_t(i)].GetColor4f());
                stops[size_t(i)] = { g.stops[size_t(i)].GetOffset(), stopStyle };
            }
            const int selected = ge.selectedStop;
            const bool hideSelected = ge.dragOff;
            const TextID badgeText = textSystem->RegisterText(idManager.AllocateDynamicID("ge_badge_text"),
                Percent(selected < stopCount ? stops[size_t(selected)].t : 0.0f), theme.gradientEditorBadgeStyleRef, ElementState::None);
            const auto badgeSize = textSystem->GetTextMeasurement(badgeText);
            VERIFY(elementStorage.AddCustomDraw(barRef,
                [look, preview = ge.previewGradientStyle, stops, stopCount, selected, hideSelected, badgeText,
                 badgeW = badgeSize.maxWidth, badgeH = badgeSize.minHeight,
                 edgeColor = ge.stopEdgeColor, outline = ge.stopOutlineColor, halo = ge.stopHaloColor,
                 accent = theme.accentColor, barH, stopBox, pointer, inset,
                 radius = float(styleManager.Px(4)), border = styleManager.Px(2), haloW = styleManager.Px(3),
                 badgePadX = styleManager.Px(6), badgePadY = styleManager.Px(1), badgeGap = styleManager.Px(2)](CustomDrawContext& ctx)
                {
                    const IntRect r = ctx.ElementRect();
                    const int top = r.y + (r.h - (barH + pointer + stopBox)) / 2;
                    const IntRect barRect{ .position = { r.x + inset, top }, .size = { r.w - 2 * inset, barH } };
                    if (barRect.w <= 0) return;
                    ColorSwatchLook barLook = look;
                    barLook.radius = radius;
                    DrawCheckerboard(ctx, barRect, barRect, barLook);
                    ctx.RenderQuad(barRect, preview, radius);

                    // A stop: a pointer up to the bar and a square of its color, edged; the
                    // selected one in accent with a soft halo and its location under it.
                    auto drawStop = [&](int i) {
                        const bool sel = (i == selected);
                        const int x = barRect.x + int(std::lround(stops[size_t(i)].t * float(barRect.w)));
                        const int y = barRect.y + barRect.h;
                        const VisualColorStyleIndex edge = sel ? accent : edgeColor;
                        ctx.RenderTriangle({ float(x), float(y) }, { float(x + pointer), float(y + pointer) }, { float(x - pointer), float(y + pointer) }, edge);
                        const IntRect box{ .position = { x - stopBox / 2, y + pointer }, .size = { stopBox, stopBox } };
                        const IntRect around{ .position = { box.x - (sel ? haloW : 1), box.y - (sel ? haloW : 1) },
                                              .size = { box.w + 2 * (sel ? haloW : 1), box.h + 2 * (sel ? haloW : 1) } };
                        ctx.RenderQuad(around, sel ? halo : outline, radius + float(sel ? haloW : 1));
                        ctx.RenderQuad(box, edge, radius);
                        const IntRect fill{ .position = { box.x + border, box.y + border }, .size = { box.w - 2 * border, box.h - 2 * border } };
                        ColorSwatchLook stopLook = look;
                        stopLook.radius = radius - float(border);
                        stopLook.checkerSize = fill.w / 2;
                        DrawCheckerboard(ctx, fill, fill, stopLook);
                        ctx.RenderQuad(fill, stops[size_t(i)].color, radius - float(border));
                        if (sel)
                        {
                            const IntRect badge{ .position = { x - badgeW / 2 - badgePadX, box.y + box.h + badgeGap },
                                                 .size = { badgeW + 2 * badgePadX, badgeH + 2 * badgePadY } };
                            ctx.RenderQuad(badge, accent, radius);
                            ctx.RenderText(badgeText, IntRect{ .position = { badge.x + badgePadX, badge.y + badgePadY }, .size = { badgeW, badgeH } });
                        }
                    };
                    for (int i = 0; i < stopCount; i++)
                        if (i != selected) drawStop(i);
                    if (selected >= 0 && selected < stopCount && !hideSelected) drawStop(selected);
                }));
        }

        // ---- Below: the selected stop and options at the left, the stops at the right ----
        BeginHorizontal(idManager.AllocateDynamicID("ge_columns"), theme.gradientEditorColumnsStyleRef, LayoutOverride{}.NoShrink());

        BeginVertical(idManager.AllocateDynamicID("ge_left"), theme.gradientEditorColumnStyleRef, LayoutOverride{}.FlexGrow(3));   // room for the six blend spaces
        if (ge.selectedStop >= 0 && ge.selectedStop < g.stopCount)
        {
            // "Color stop  2 / 3", previous, next.
            BeginHorizontal(idManager.AllocateDynamicID("ge_stop_head"), theme.gradientEditorRowStyleRef, LayoutOverride{}.NoShrink());
            Text(idManager.AllocateDynamicID("ge_stop_title"), "Color stop", theme.gradientEditorHeadingStyleRef);
            char count[32];
            std::snprintf(count, sizeof(count), "%d / %d", ge.selectedStop + 1, int(g.stopCount));
            Text(idManager.AllocateDynamicID("ge_stop_count"), count, theme.gradientEditorCountStyleRef, LayoutOverride{}.FlexGrow(1));
            Button(idManager.AllocateDynamicID("ge_prev"), theme.chevronLeftIcon, [this]() {
                auto& ge = m_gradientEditorSub->State();
                ge.selectedStop = std::max(0, ge.selectedStop - 1);
            }, theme.dialogIconButtonStyleRef);
            SetTooltip("Previous stop");
            Button(idManager.AllocateDynamicID("ge_next"), theme.treeArrowRight, [this]() {
                auto& ge = m_gradientEditorSub->State();
                ge.selectedStop = std::min(int(ge.workingCopy.stopCount) - 1, ge.selectedStop + 1);
            }, theme.dialogIconButtonStyleRef);
            SetTooltip("Next stop");
            EndHorizontal();

            // Color, then location with duplicate and delete.
            BeginHorizontal(idManager.AllocateDynamicID("ge_color_row"), theme.gradientEditorRowStyleRef, LayoutOverride{}.NoShrink());
            Text(idManager.AllocateDynamicID("ge_color_label"), "Color", theme.gradientEditorLabelStyleRef);
            ge.selectedStopColor = g.stops[size_t(ge.selectedStop)].GetColor4f();
            if (ColorField(ge.selectedStopColor, false, LayoutOverride{}.FlexGrow(1)))
                g.stops[size_t(ge.selectedStop)].SetColor(ge.selectedStopColor);
            EndHorizontal();

            BeginHorizontal(idManager.AllocateDynamicID("ge_location_row"), theme.gradientEditorRowStyleRef, LayoutOverride{}.NoShrink());
            Text(idManager.AllocateDynamicID("ge_location_label"), "Location", theme.gradientEditorLabelStyleRef);
            BeginHorizontal(idManager.AllocateDynamicID("ge_location_box"), theme.gradientEditorLocationStyleRef, LayoutOverride{}.FlexGrow(1));
            float percent = std::round(g.stops[size_t(ge.selectedStop)].GetOffset() * 1000.0f) / 10.0f;
            if (Field(idManager.AllocateDynamicID("ge_location_value"), percent, TextEditBehavior::None, theme.gradientEditorLocationInputStyleRef,
                      NumericFieldConfig<float>{ .stepPerPixel = 0.1, .minVal = 0.0f, .maxVal = 100.0f }, LayoutOverride{}.FlexGrow(1)))
            {
                g.stops[size_t(ge.selectedStop)].SetOffset(percent / 100.0f);
                ge.selectedStop = SortStops(g, ge.selectedStop);
            }
            Text(idManager.AllocateDynamicID("ge_location_unit"), "%", theme.gradientEditorLocationUnitStyleRef);
            EndHorizontal();
            Button(idManager.AllocateDynamicID("ge_duplicate"), theme.duplicateIcon, [this]() {
                auto& ge = m_gradientEditorSub->State();
                auto& g = ge.workingCopy;
                if (g.stopCount >= Gradient::kMaxStops || ge.selectedStop >= g.stopCount) return;
                // A copy halfway to the next stop (or the previous one at the end).
                const ColorStop stop = g.stops[size_t(ge.selectedStop)];
                const float here = stop.GetOffset();
                const float there = (ge.selectedStop + 1 < g.stopCount) ? g.stops[size_t(ge.selectedStop + 1)].GetOffset()
                                  : (ge.selectedStop > 0 ? g.stops[size_t(ge.selectedStop - 1)].GetOffset() : 1.0f);
                g.AddStop(stop.GetColor4f(), (here + there) * 0.5f);
                ge.selectedStop = SortStops(g, g.stopCount - 1);
            }, theme.dialogSquareButtonStyleRef);
            SetTooltip("Duplicate the stop");
            Button(idManager.AllocateDynamicID("ge_delete"), theme.deleteIcon, [this]() {
                auto& ge = m_gradientEditorSub->State();
                RemoveStop(ge.workingCopy, ge.selectedStop);
                ge.selectedStop = std::min(ge.selectedStop, int(ge.workingCopy.stopCount) - 1);
            }, theme.dialogDeleteButtonStyleRef);
            SetTooltip("Delete the stop");
            EndHorizontal();
        }

        Element divider{ .id = idManager.AllocateDynamicID("ge_divider"), .styleRef = theme.gradientEditorDividerStyleRef, .flags = ElementFlags::NoInput };
        RegisterElement(std::move(divider), LayoutOverride{});

        // Blend space.
        BeginHorizontal(idManager.AllocateDynamicID("ge_space_row"), theme.gradientEditorRowStyleRef, LayoutOverride{}.NoShrink());
        Text(idManager.AllocateDynamicID("ge_space_label"), "Blend space", theme.gradientEditorLabelStyleRef);
        {
            struct SpaceTab { ColorSpace space; std::string_view label; };
            constexpr SpaceTab tabs[] = {
                { ColorSpace::sRGB, "sRGB" }, { ColorSpace::LinearRGB, "Linear" }, { ColorSpace::HSL, "HSL" },
                { ColorSpace::HSV, "HSV" }, { ColorSpace::Lab, "Lab" }, { ColorSpace::Oklab, "Oklab" },
            };
            BeginButtonGroup(LayoutOverride{}.NoGrow().NoShrink());
            for (const SpaceTab& tab : tabs)
            {
                const bool active = (g.colorSpace == tab.space);
                if (active) guiStateManager.Construction().PushSelected();
                const ColorSpace target = tab.space;
                Button(tab.label, [this, target]() { m_gradientEditorSub->State().workingCopy.colorSpace = target; });
                if (active) guiStateManager.Construction().PopSelected();
            }
            EndButtonGroup();
        }
        EndHorizontal();

        // Reverse, distribute evenly.
        BeginHorizontal(idManager.AllocateDynamicID("ge_actions"), theme.gradientEditorActionsStyleRef, LayoutOverride{}.NoShrink());
        Button(idManager.AllocateDynamicID("ge_reverse"), { theme.reverseIcon, "Reverse" }, [this]() {
            auto& ge = m_gradientEditorSub->State();
            auto& g = ge.workingCopy;
            for (int i = 0; i < g.stopCount; i++)
                g.stops[size_t(i)].SetOffset(1.0f - g.stops[size_t(i)].GetOffset());
            std::reverse(g.stops.begin(), g.stops.begin() + g.stopCount);
            ge.selectedStop = g.stopCount - 1 - ge.selectedStop;
        }, theme.dialogSmallButtonStyleRef);
        Button(idManager.AllocateDynamicID("ge_distribute"), { theme.distributeIcon, "Distribute evenly" }, [this]() {
            auto& g = m_gradientEditorSub->State().workingCopy;
            for (int i = 0; i < g.stopCount; i++)
                g.stops[size_t(i)].SetOffset(g.stopCount > 1 ? float(i) / float(g.stopCount - 1) : 0.0f);
        }, theme.dialogSmallButtonStyleRef);
        EndHorizontal();
        EndVertical();   // left

        // The stops table: type, swatch, value, location; a click selects.
        BeginVertical(idManager.AllocateDynamicID("ge_table"), theme.gradientEditorTableStyleRef, LayoutOverride{}.FlexGrow(2));
        {
            BeginHorizontal(idManager.AllocateDynamicID("ge_table_head"), theme.gradientEditorTableHeadStyleRef, LayoutOverride{}.NoShrink());
            Text(idManager.AllocateDynamicID("ge_th_type"), "Type", theme.gradientEditorTypeCellStyleRef);
            Element blank{ .id = idManager.AllocateDynamicID("ge_th_swatch"), .styleRef = theme.gradientEditorSwatchCellStyleRef, .flags = ElementFlags::NoInput };
            RegisterElement(std::move(blank), LayoutOverride{});
            Text(idManager.AllocateDynamicID("ge_th_value"), "Value", theme.gradientEditorHeadCellStyleRef, LayoutOverride{}.FlexGrow(1));
            Text(idManager.AllocateDynamicID("ge_th_location"), "Location", theme.gradientEditorHeadLocationStyleRef);
            EndHorizontal();

            const ID rowRange = idManager.AllocateRangedID(uint32_t(std::max<int>(1, g.stopCount)), "ge_table_row");
            for (int i = 0; i < g.stopCount; i++)
            {
                const ID rowID = idManager.GetSubID(rowRange, uint32_t(i));
                const bool sel = (i == ge.selectedStop);
                if (sel) guiStateManager.Construction().PushSelected();
                RegisterElement(Element{ .id = rowID,
                    .eventHandler = MakeButtonHandler([this, i]() { m_gradientEditorSub->State().selectedStop = i; }),
                    .styleRef = theme.gradientEditorTableRowStyleRef, .state = guiStateManager.GetCurrentState(rowID) },
                    LayoutOverride{}.NoShrink());
                VERIFY(elementStorage.BeginChildren());
                Text(idManager.AllocateDynamicID("ge_td_type"), "Color", theme.gradientEditorTypeCellStyleRef);
                const ID swID = idManager.AllocateDynamicID("ge_td_swatch");
                NoteSwatchClaim(swID);
                auto& swStyle = m_gradientEditorSub->ClaimSwatchStyle(swID);
                SetColorStyle(styleManager, swStyle, g.stops[size_t(i)].GetColor4f());
                Element sw{ .id = swID, .styleRef = theme.gradientEditorSwatchCellStyleRef, .flags = ElementFlags::NoInput };
                const ElementRef swRef = RegisterElement(std::move(sw), LayoutOverride{});
                VERIFY(elementStorage.AddCustomDraw(swRef, [look, swStyle, radius = float(styleManager.Px(3))](CustomDrawContext& ctx) {
                    const IntRect r = ctx.ElementInnerRect();
                    ColorSwatchLook l = look;
                    l.radius = radius;
                    l.checkerSize = r.h / 2;
                    DrawCheckerboard(ctx, r, r, l);
                    ctx.RenderQuad(r, swStyle, radius);
                }));
                Text(idManager.AllocateDynamicID("ge_td_value"), StopHex(g.stops[size_t(i)]), theme.gradientEditorValueCellStyleRef, LayoutOverride{}.FlexGrow(1));
                Text(idManager.AllocateDynamicID("ge_td_location"), Percent(g.stops[size_t(i)].GetOffset()), theme.gradientEditorLocationCellStyleRef);
                VERIFY(elementStorage.EndChildren());
                if (sel) guiStateManager.Construction().PopSelected();
            }
        }
        EndVertical();
        EndHorizontal();   // columns

        // ---- Footer ----
        BeginHorizontal(idManager.AllocateDynamicID("ge_footer"), theme.dialogFooterStyleRef, LayoutOverride{}.NoShrink());
        {
            char status[96];
            std::snprintf(status, sizeof(status), "%d stops \xC2\xB7 click the bar to add, drag a stop off to remove", int(g.stopCount));
            Text(idManager.AllocateDynamicID("ge_status"), status, theme.dialogStatusStyleRef, LayoutOverride{}.FlexGrow(1));
            Button(idManager.AllocateDynamicID("ge_cancel"), "Cancel", requestClose(GradientEditorState::CloseRequest::Cancel),
                   theme.dialogButtonStyleRef);
            Button(idManager.AllocateDynamicID("ge_ok"), "OK", requestClose(GradientEditorState::CloseRequest::Ok),
                   theme.dialogPrimaryButtonStyleRef);
        }
        EndHorizontal();

        outGradient = g;
    }
}
