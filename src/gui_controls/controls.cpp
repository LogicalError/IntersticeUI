#include <gui_controls/gui_application.h>

#include <cassert>
#include <cmath>
#include <format>
#include <string>

namespace GUI
{
    // ---------------------------------------------------------------------------
    // Event handler factories
    // ---------------------------------------------------------------------------

    GUI::EventHandler GUIApplication::MakeToggleHandler(bool& toggle) { return GUI::MakeButtonHandler(guiStateManager, [this, &toggle]() { toggle = !toggle; }); }
    GUI::EventHandler GUIApplication::MakeButtonHandler(std::function<void()> onClick) { return GUI::MakeButtonHandler(guiStateManager, onClick); }
    GUI::EventHandler GUIApplication::MakeLabelHandler() { return GUI::MakeLabelHandler(guiStateManager, elementStorage); }


    // ---------------------------------------------------------------------------
    // Element registration
    // ---------------------------------------------------------------------------

    GUI::ElementRef GUIApplication::RegisterElementInternal(GUI::Element&& element, LayoutOverride&& constraints, bool updateState)
    {
        if (updateState) element.state |= guiStateManager.GetCurrentState(element.id);
        auto addRes = elementStorage.Add(std::move(element), std::move(constraints));
        VERIFY(addRes.status);
        VERIFY(windowRegistry.Add(addRes.ref));
        return addRes.ref;
    }

    GUI::ElementRef GUIApplication::RegisterElement(GUI::Element&& element, LayoutOverride&& constraints)
    {
	    return RegisterElementInternal(std::move(element), std::move(constraints), true);
    }

    GUI::ElementRef GUIApplication::RegisterElement(GUI::Element&& element, std::string_view text, LayoutOverride&& constraints)
    {
        element.state |= guiStateManager.GetCurrentState(element.id); // required for RegisterText
        element.textID = textSystem->RegisterText(element.id, text, element.styleRef, element.state);
        return RegisterElementInternal(std::move(element), std::move(constraints),
		    false); // already updated state, so skipping it in RegisterElementInternal
    }

    GUI::ElementRef GUIApplication::RegisterControl(GUI::Element&& element, std::string_view text, LayoutOverride&& constraints)
    {
        element.flags |= ElementFlags::IsControl;
        tabChainManager.RegisterTabTarget(element.id);
        return RegisterElement(std::move(element), text, std::move(constraints));
    }

    GUI::ElementRef GUIApplication::RegisterControl(GUI::Element&& element, LayoutOverride&& constraints)
    {
        element.flags |= ElementFlags::IsControl;
        tabChainManager.RegisterTabTarget(element.id);
        return RegisterElement(std::move(element), std::move(constraints));
    }

    GUI::ElementRef GUIApplication::RegisterControl(GUI::Element&& element, std::function<TextID(GUI::Element& element)> createTextID, LayoutOverride&& constraints)
    {
        element.flags |= ElementFlags::IsControl;
        element.state |= guiStateManager.GetCurrentState(element.id); // required for createTextID callback
        element.textID = createTextID(element);
        tabChainManager.RegisterTabTarget(element.id);
        return RegisterElementInternal(std::move(element), std::move(constraints),
            false); // already updated state, so skipping it in RegisterElementInternal
    }


    // ---------------------------------------------------------------------------
    // Basic widgets (Field, Label, Button)
    // ---------------------------------------------------------------------------

    bool GUIApplication::Field(ID id, std::string& value, TextEditBehavior textEditBehavior, StyleRef style, LayoutOverride&& constraints)
    {
        // Inside a node row, fill the body after the label so the node stays grid-aligned.
        if (nodeRowFieldMinWidth > 0 && !constraints.HasFixedWidth())
            constraints.FlexGrow(1).MinWidth(nodeRowFieldMinWidth);

        if (guiStateManager.Construction().IsIndeterminate())
        {
            RegisterControl(GUI::Element { .id = id, .styleRef = style, .flags = ElementFlags::DoNotMeasureText },
                [this](GUI::Element& element) { return textSystem->RegisterText(element.id, "\xe2\x80\x94", element.styleRef, element.state); },
                std::move(constraints));
            return false;
        }

        bool modified = false;
        RegisterControl(GUI::Element{ .id = id, .eventHandler = MakeTextBoxHandler(guiStateManager), .styleRef = style, .flags = ElementFlags::DoNotMeasureText },
            [this, &modified, &value, textEditBehavior](GUI::Element& element)
            {
                return textSystem->RegisterEditableText(element.id, value, textEditBehavior, element.styleRef, element.state, modified);
            },
            std::move(constraints));
        guiStateManager.SetModified(modified);
        return modified;
    }

    bool GUIApplication::SearchField(ID id, std::string& value, IconID icon, TextEditBehavior textEditBehavior, LayoutOverride&& constraints)
    {
        // The container draws the textbox chrome and takes the inner field's
        // hover/focus state (PropagatesDescendantState); the field itself is borderless.
        GUI::Element container{};
        container.id       = id;
        container.styleRef = theme.searchTextboxStyleRef;
        container.flags    = ElementFlags::PropagatesDescendantState;
        container.state    = guiStateManager.GetCurrentState(id);
        RegisterElement(std::move(container), std::move(constraints));
        VERIFY(elementStorage.BeginChildren());

        {
            const GUI::ID iconID = idManager.AllocateDynamicID("searchIcon");
            GUI::Element iconElem{};
            iconElem.id       = iconID;
            iconElem.iconID   = icon;
            iconElem.flags    = ElementFlags::NoInput;
            iconElem.state    = guiStateManager.GetCurrentState(iconID);
            iconElem.styleRef = theme.searchTextboxIconStyleRef;
            auto res = elementStorage.Add(std::move(iconElem), GUI::LayoutOverride{});
            VERIFY(res.status);
            VERIFY(windowRegistry.Add(res.ref));
        }

        const bool modified = Field(idManager.AllocateDynamicID("searchField"),
                                    value, textEditBehavior,
                                    theme.searchTextboxFieldStyleRef,
                                    GUI::LayoutOverride{}.FlexGrow(1));

        VERIFY(elementStorage.EndChildren());
        return modified;
    }

    bool GUIApplication::Field(ID id, bool& value, std::string_view text, StyleRef style, LayoutOverride&& constraints)
    {
		return Toggle(id, value, text, style, std::move(constraints));
    }

    void GUIApplication::DragLabel(ID id, std::string_view title, StyleRef style, LayoutOverride&& constraints)
    {
        guiStateManager.Labels().RegisterPendingLabel(id);
        GUI::EventHandler handler;
        auto* gs = m_labelGroup->GetActiveGroup();
        if (gs)
        {
            constraints.Width(styleManager.Px(gs->labelWidth));
            style = theme.groupDragLabelStyleRef; // overflow:visible for edge dragger rendering
            if (gs->resizable)
            {
                handler = MakeGroupDraggableLabelHandler(guiStateManager, elementStorage,
                    { &gs->labelWidth, gs->minWidth, gs->maxWidth,
                      &gs->edgeHovered, &gs->edgeDragging,
                      &gs->labelTextIDs, textSystem,
                      &styleManager, theme.groupDragLabelStyleRef });
            }
            else
            {
                handler = MakeDraggableLabelHandler(guiStateManager, elementStorage);
            }
        }
        else
        {
            handler = MakeDraggableLabelHandler(guiStateManager, elementStorage);
        }
        auto ref = RegisterElement(GUI::Element{ .id = id, .eventHandler = std::move(handler), .styleRef = style, .flags = ElementFlags::IsLabel }, title,
                        std::move(constraints));
        if (gs)
        {
            if (gs->resizable)
            {
                gs->labelTextIDs.push_back(elementStorage.GetElementUnsafe(ref).textID);
                m_labelGroup->AddDragger(ref);
            }
            else
            {
                TextID tid = elementStorage.GetElementUnsafe(ref).textID;
                int32_t w = textSystem->GetTextMeasurement(tid).maxWidth;
                if (const LayoutStyle* ls = styleManager.GetLayoutStyle(style.layoutStyleIndex))
                    w += ls->padding.Horizontal() + ls->borderWidths.Horizontal();
                gs->maxLabelWidthThisFrame = std::max(gs->maxLabelWidthThisFrame, w);
            }
        }
    }

    void GUIApplication::Label(ID id, std::string_view text, StyleRef style, LayoutOverride&& constraints)
    {
        guiStateManager.Labels().RegisterPendingLabel(id);
        GUI::EventHandler handler;
        auto* gs = m_labelGroup->GetActiveGroup();
        if (gs)
        {
            constraints.Width(styleManager.Px(gs->labelWidth));
            if (gs->resizable)
            {
                style = theme.groupLabelStyleRef; // overflow:visible for edge dragger rendering
                handler = MakeGroupLabelHandler(guiStateManager, elementStorage,
                    { &gs->labelWidth, gs->minWidth, gs->maxWidth,
                      &gs->edgeHovered, &gs->edgeDragging,
                      &gs->labelTextIDs, textSystem,
                      &styleManager, theme.groupLabelStyleRef });
            }
            else
            {
                // No drag edge: keep the caller's style, since the group-label padding
                // would stretch the row and break node grid alignment.
                handler = MakeLabelHandler();
            }
        }
        else
        {
            handler = MakeLabelHandler();
        }
        auto ref = RegisterElement(GUI::Element{ .id = id, .eventHandler = std::move(handler), .styleRef = style, .flags = ElementFlags::IsLabel }, text,
                        std::move(constraints));
        if (gs)
        {
            if (gs->resizable)
            {
                gs->labelTextIDs.push_back(elementStorage.GetElementUnsafe(ref).textID);
                m_labelGroup->AddDragger(ref);
            }
            else
            {
                TextID tid = elementStorage.GetElementUnsafe(ref).textID;
                int32_t w = textSystem->GetTextMeasurement(tid).maxWidth;
                if (const LayoutStyle* ls = styleManager.GetLayoutStyle(style.layoutStyleIndex))
                    w += ls->padding.Horizontal() + ls->borderWidths.Horizontal();
                gs->maxLabelWidthThisFrame = std::max(gs->maxLabelWidthThisFrame, w);
            }
        }
    }

    void GUIApplication::Text(ID id, std::string_view text, StyleRef style, LayoutOverride&& constraints)
    {
        [[maybe_unused]]
        auto ref = RegisterElement(GUI::Element{ .id = id, .styleRef = style }, text, std::move(constraints));
    }

    void GUIApplication::SelectableText(ID id, const std::span<ContentRun>& runs,
                                        std::function<void(intptr_t)> onLinkActivate,
                                        StyleRef style, LayoutOverride&& constraints)
    {
        // Per-element press position, so a press+release without movement reads as a click
        // (activate the link under it) while a drag is a text selection.
        struct PressState { int2 pressPos {}; };

        GUI::EventHandler handler =
            [this, id, onLinkActivate = std::move(onLinkActivate)](GUI::ElementEvent&& ev) -> bool
        {
            // The editor's TextID isn't known when this handler is built (it's created by the
            // createTextID callback below), so resolve it at event time.
            const TextID textID = textSystem->GetTextID(id);
            const int2 localPos = (ev.rect != nullptr)
                ? ev.LocalCursor(elementStorage) - ev.rect->innerRect.position
                : ev.state.screenPos;
            auto& store = guiStateManager.GetElementDataStore();
            switch (ev.type)
            {
                case GUI::EventType::MouseMove:
                    if (textID != TextID::Invalid)
                        textSystem->SetHoveredContent(textID, textSystem->HitTestContent(textID, localPos.x, localPos.y));
                    break;
                case GUI::EventType::MouseDown:
                    if (ev.state.LeftPressed())
                    {
                        // A press on a link only activates it (on release); elsewhere, focus the editor.
                        const intptr_t hit = (textID != TextID::Invalid)
                            ? textSystem->HitTestContent(textID, localPos.x, localPos.y) : 0;
                        if (hit == 0)
                        {
                            guiStateManager.Interaction().SetFocusID(id);
                            guiStateManager.TextEditorFocus().Set(id);
                        }
                        store.GetOrCreate<PressState>(id).pressPos = ev.state.screenPos;
                        return true;
                    }
                    break;
                case GUI::EventType::MouseUp:
                    if (onLinkActivate && textID != TextID::Invalid)
                        if (auto* press = store.Get<PressState>(id))
                        {
                            const int2 d = ev.state.screenPos - press->pressPos;
                            const int32_t slop = styleManager.Px(kDragStartDistance);
                            if (d.x * d.x + d.y * d.y < slop * slop) // a click, not a drag
                            {
                                const intptr_t hit = textSystem->HitTestContent(textID, localPos.x, localPos.y);
                                if (hit != 0) onLinkActivate(hit);
                            }
                        }
                    break;
                case GUI::EventType::KeyDown:
                case GUI::EventType::KeyUp:
                case GUI::EventType::TextInput:
                    // Keep keyboard focus while focused so Ctrl+C reaches the editor.
                    if (guiStateManager.Interaction().GetFocusedID() == id && !ev.state.consumed)
                        return true;
                    break;
                default:
                    break;
            }
            return false;
        };

        bool modified = false;
        RegisterControl(GUI::Element{ .id = id, .eventHandler = std::move(handler), .styleRef = style },
            [this, &modified, &runs](GUI::Element& element)
            {
                return textSystem->RegisterEditableRichText(element.id, runs,
                    TextEditBehavior::ReadOnly | TextEditBehavior::Multiline,
                    element.styleRef, element.state, modified);
            },
            std::move(constraints));
    }

    void GUIApplication::Image(ID id, ImageIndex image, StyleRef style, LayoutOverride&& constraints)
    {
        // Drawn as the element's backgroundImage; sized by style + constraints. Non-interactive.
        GUI::Element el{ .id = id, .styleRef = style };
        el.backgroundImage = image;
        [[maybe_unused]]
        auto ref = RegisterElement(std::move(el), std::move(constraints));
    }

    void GUIApplication::Image(ID id, ImageIndex image, uint2 sourceSize, StyleRef style, LayoutOverride&& constraints)
    {
        // Largest source-aspect rectangle that fits the requested box. An axis
        // the caller left unconstrained falls back to the source pixel extent.
        const int32_t boxW = constraints.HasFixedWidth()  ? constraints.width.value  : int32_t(sourceSize.x);
        const int32_t boxH = constraints.HasFixedHeight() ? constraints.height.value : int32_t(sourceSize.y);
        int32_t w = boxW, h = boxH;
        if (sourceSize.x > 0 && sourceSize.y > 0 && boxW > 0 && boxH > 0)
        {
            if (int64_t(sourceSize.x) * boxH <= int64_t(sourceSize.y) * boxW)   // height-limited
            {
                h = boxH;
                w = int32_t((int64_t(boxH) * sourceSize.x + sourceSize.y / 2) / sourceSize.y);
            }
            else                                                                // width-limited
            {
                w = boxW;
                h = int32_t((int64_t(boxW) * sourceSize.y + sourceSize.x / 2) / sourceSize.x);
            }
        }
        Image(id, image, style, GUI::LayoutOverride::FixedSize(w, h));
    }

    bool GUIApplication::Button(ID id, IconID icon, std::string_view text, std::function<void()> onClick, StyleRef style, LayoutOverride&& constraints)
    {
        auto* gs = m_buttonGroup->GetActiveButton();
        if (gs) style = m_buttonGroup->NextItemStyleRef(*gs);
        auto ref = RegisterControl(
            GUI::Element{ .id = id, .eventHandler = MakeButtonHandler(onClick), .styleRef = style },
            [this, icon, text](GUI::Element& element) {
                return textSystem->RegisterText(element.id, icon, text, element.styleRef, element.state);
            },
            std::move(constraints));
        if (gs)
        {
            if (gs->itemCount == 1) gs->firstRef = ref;
            gs->lastRef = ref;
        }
        return guiStateManager.Interaction().HasBeenClicked(id);
    }

    bool GUIApplication::Toggle(ID id, bool value, IconID icon, std::string_view text, std::function<void(bool)> onToggle, StyleRef style, LayoutOverride&& constraints)
    {
	    bool currentValue = value;
        if (currentValue) guiStateManager.Construction().PushSelected();
        auto* gs = m_buttonGroup->GetActiveToggle();
        if (gs) style = m_buttonGroup->NextItemStyleRef(*gs);
        auto ref = RegisterControl(
            GUI::Element 
            { 
                .id = id, 
                .eventHandler = GUI::MakeButtonHandler(guiStateManager, 
                                                       [this, value, onToggle]() 
                                                       { 
                                                           onToggle(!value); 
                                                       }), 
                .styleRef = style 
            },
            [this, icon, text](GUI::Element& element) {
                return textSystem->RegisterText(element.id, icon, text, element.styleRef, element.state);
            },
            std::move(constraints));
        if (gs)
        {
            if (gs->itemCount == 1) gs->firstRef = ref;
            gs->lastRef = ref;
        }
        if (currentValue) guiStateManager.Construction().PopSelected();
        bool modified = guiStateManager.Interaction().HasBeenClicked(id);
        guiStateManager.SetModified(modified);
        return modified;
    }


    // ---------------------------------------------------------------------------
    // Label Groups (logical grouping for shared label width)
    // ---------------------------------------------------------------------------

    void GUIApplication::BeginLabelGroup(ID id, int32_t defaultWidth, int32_t minWidth, int32_t maxWidth, bool resizable)
    {
        m_labelGroup->BeginGroup(id, defaultWidth, minWidth, maxWidth, resizable);
    }

    void GUIApplication::EndLabelGroup()
    {
        m_labelGroup->EndGroup();
    }

    // ---------------------------------------------------------------------------
    // Button / Toggle groups (tight horizontal pack with shared borders)
    // ---------------------------------------------------------------------------

    void GUIApplication::BeginButtonGroup(LayoutOverride&& constraints)
    {
        GUI::ID groupID = idManager.AllocateDynamicID("button_group");
        GUI::Element wrap{};
        wrap.id       = groupID;
        wrap.styleRef = theme.buttonGroupStyleRef;
        wrap.state    = guiStateManager.GetCurrentState(groupID);
        RegisterElement(std::move(wrap), std::move(constraints));
        VERIFY(elementStorage.BeginChildren());
        m_buttonGroup->BeginButtonGroup({
            .firstStyleRef = theme.firstButtonStyleRef,
            .midStyleRef   = theme.midButtonStyleRef,
            .lastStyleRef  = theme.lastButtonStyleRef,
            .soloStyleRef  = theme.regularControls.button
        });
    }

    void GUIApplication::EndButtonGroup()
    {
        m_buttonGroup->EndButtonGroup();
        VERIFY(elementStorage.EndChildren());
    }

    void GUIApplication::BeginToggleGroup(LayoutOverride&& constraints)
    {
        GUI::ID groupID = idManager.AllocateDynamicID("toggle_group");
        GUI::Element wrap{};
        wrap.id       = groupID;
        wrap.styleRef = theme.toggleGroupStyleRef;
        wrap.state    = guiStateManager.GetCurrentState(groupID);
        RegisterElement(std::move(wrap), std::move(constraints));
        VERIFY(elementStorage.BeginChildren());
        m_buttonGroup->BeginToggleGroup({
            .firstStyleRef = theme.firstToggleStyleRef,
            .midStyleRef   = theme.midToggleStyleRef,
            .lastStyleRef  = theme.lastToggleStyleRef,
            .soloStyleRef  = theme.regularControls.toggle
        });
    }

    void GUIApplication::EndToggleGroup()
    {
        m_buttonGroup->EndToggleGroup();
        VERIFY(elementStorage.EndChildren());
    }

    // ---------------------------------------------------------------------------
    // Areas (grouping containers for any content)
    // ---------------------------------------------------------------------------

    bool GUIApplication::BeginArea(ID id, StyleRef style, LayoutOverride&& constraints)
    {
        GUI::Element container
        {
            .id       = id,
            .styleRef = style,
        };
        container.state = guiStateManager.GetCurrentState(id);
        RegisterElement(std::move(container), std::move(constraints));
        VERIFY(elementStorage.BeginChildren());
        return true;
    }

    void GUIApplication::EndArea()
    {
        VERIFY(elementStorage.EndChildren());
    }

    // ---------------------------------------------------------------------------
    // Cached scopes (retained render cache)
    // ---------------------------------------------------------------------------

    bool GUIApplication::BeginCached(ID id, StyleRef style, uint64_t signature, LayoutOverride&& constraints)
    {
        // Scope the dynamic-ID counter so skipping the body on a hit can't shift later siblings' IDs.
        [[maybe_unused]] bool pushed = idManager.PushParent(id);
        assert(pushed && "BeginCached requires a valid, stable id");

        // The container itself is built every frame, so its layout and hit-testing stay live.
        GUI::Element container{ .id = id, .styleRef = style, .flags = ElementFlags::IsCachedScope };
        container.state = guiStateManager.GetCurrentState(id);
        GUI::ElementRef containerRef = RegisterElement(std::move(container), std::move(constraints));
        VERIFY(elementStorage.BeginChildren());

        UICacheManager::Scope& scope = m_uiCache->GetOrCreate(id);
        scope.lastContainerRef = containerRef;
        scope.lastUsedFrame    = m_uiCache->CurrentFrame();

        RefreshCachedInputFlags();
        const UICacheManager::HitInput hitInput
        {
            .dragging   = guiStateManager.Drag().IsDragging(),
            .keyboard   = m_keyboardPending,
            .rebuildAll = m_tabPending
        };
        const bool hit = m_uiCache->EvaluateHit(scope, signature, hitInput);
        m_uiCache->RecordResult(hit);
        scope.missedThisFrame = !hit;

        if (hit)
        {
            for (ID swatchID : scope.claimedSwatches)
            {
                m_colorPickerSub->KeepSwatchAlive(swatchID);
                m_gradientEditorSub->KeepSwatchAlive(swatchID);
            }

            // Hit: leave the container empty; OnRender blits the cached primitives in its place.
            VERIFY(elementStorage.EndChildren()); // close the empty container
            [[maybe_unused]] bool popped = idManager.PopParent();
            assert(popped);
            return false; // hit — caller skips the body and EndCached
        }

        // Miss: the caller builds the body. Track the parent scope so invalidation propagates upward.
        scope.claimedSwatches.clear();
        scope.parentScopeID = m_cacheRecordStack.empty() ? ID::Invalid() : m_cacheRecordStack.back();
        scope.signature     = signature;
        m_cacheRecordStack.push_back(id);
        return true;
    }

    void GUIApplication::NoteSwatchClaim(ID swatchID)
    {
        for (ID scopeID : m_cacheRecordStack)
            m_uiCache->GetOrCreate(scopeID).claimedSwatches.push_back(swatchID);
    }

    void GUIApplication::EndCached()
    {
        assert(!m_cacheRecordStack.empty() && "EndCached without a matching BeginCached miss");
        m_cacheRecordStack.pop_back();
        VERIFY(elementStorage.EndChildren()); // close container; UICacheManager captures it at PostLayout
        [[maybe_unused]] bool popped = idManager.PopParent();
        assert(popped);
    }

    void GUIApplication::RefreshCachedInputFlags()
    {
        if (m_inputFlagsFrame == m_uiCache->CurrentFrame()) return;
        m_inputFlagsFrame = m_uiCache->CurrentFrame();
        m_keyboardPending = false;
        m_tabPending      = false;
        if (!inputManager) return;
        for (const auto& e : inputManager->GetRecordedEvents())
        {
            if (e.type == EventType::KeyDown || e.type == EventType::KeyUp || e.type == EventType::TextInput)
                m_keyboardPending = true;
            if (e.type == EventType::KeyDown && e.state.lastKey == KeyCode::Tab)
                m_tabPending = true;
        }
    }

    void GUIApplication::InvalidateCachedScopesForInput(ID overID)
    {
        if (!m_uiCache || !inputManager) return;
        RefreshCachedInputFlags();

        auto invalidateScopesAbove = [&](ID id)
        {
            for (ElementRef r = elementStorage.FindElementRefByID(id);
                 r != ElementRef::Invalid(); r = elementStorage.GetParentRef(r))
            {
                const Element* e = elementStorage.GetElementPtr(r);
                if (e && (e->flags & ElementFlags::IsCachedScope) != ElementFlags::None)
                    m_uiCache->Invalidate(e->id);
            }
        };

        if (overID != ID::Invalid())
        {
            bool underCursor = m_uiCache->IsDrawCacheEnabled();
            if (!underCursor)
                for (const auto& e : inputManager->GetRecordedEvents())
                    if (e.type == EventType::MouseScroll) { underCursor = true; break; }
            if (underCursor)
                invalidateScopesAbove(overID);
        }

        if (m_keyboardPending)
        {
            const ID focusedID = guiStateManager.Interaction().GetFocusedID();
            if (focusedID != ID::Invalid())
                invalidateScopesAbove(focusedID);
        }
    }

    void GUIApplication::Invalidate(ID id)
    {
        if (m_uiCache) m_uiCache->Invalidate(id);
    }

    void GUIApplication::InvalidatePanel(ID panelId)
    {
        auto it = m_panelContentIDs.find(panelId);
        if (it != m_panelContentIDs.end())
            Invalidate(it->second);
    }


    // ---------------------------------------------------------------------------
    // Horizontal / Vertical flex containers (BeginArea with a direction/wrap style)
    // ---------------------------------------------------------------------------

    bool GUIApplication::BeginHorizontal(ID id, StyleRef style, LayoutOverride&& constraints)     { return BeginArea(id, style, std::move(constraints)); }
    void GUIApplication::EndHorizontal()                                                          { EndArea(); }

    bool GUIApplication::BeginHorizontalWrap(ID id, StyleRef style, LayoutOverride&& constraints) { return BeginArea(id, style, std::move(constraints)); }
    void GUIApplication::EndHorizontalWrap()                                                      { EndArea(); }

    bool GUIApplication::BeginVertical(ID id, StyleRef style, LayoutOverride&& constraints)       { return BeginArea(id, style, std::move(constraints)); }
    void GUIApplication::EndVertical()                                                            { EndArea(); }

    bool GUIApplication::BeginVerticalWrap(ID id, StyleRef style, LayoutOverride&& constraints)   { return BeginArea(id, style, std::move(constraints)); }
    void GUIApplication::EndVerticalWrap()                                                        { EndArea(); }

    void GUIApplication::BeginToolbar(bool horizontal)
    {
        controlStylesStack.push_back(theme.controls);
        theme.controls = &theme.toolbarControls;
        if (horizontal) BeginHorizontalWrap();
        else            BeginVerticalWrap();
    }
    void GUIApplication::EndToolbar()
    {
        assert(!controlStylesStack.empty());
        EndArea();
        theme.controls = controlStylesStack.back();
        controlStylesStack.pop_back();
    }


    // ---------------------------------------------------------------------------
    // SliderField — Blender-style filled bar. The bar IS the textbox: dragging
    // anywhere on it updates value; a clean click swaps it for an in-place
    // numeric Field. Optional `label` is rendered as text overlay on the bar.
    // ---------------------------------------------------------------------------
    bool GUIApplication::SliderField(ID id, float& value, float minVal, float maxVal,
                                  std::string_view label, LayoutOverride&& constraints)
    {
        if (maxVal < minVal) std::swap(minVal, maxVal);
        value = std::clamp(value, minVal, maxVal);
        const float before = value;

        // Edit mode is entered on a clean click and exits when focus moves elsewhere.
        auto* sliderState = guiStateManager.GetElementDataStore().Get<SliderState>(id);
        bool inEditMode = sliderState && sliderState->editMode;
        if (inEditMode && guiStateManager.Interaction().GetFocusedID() != id)
        {
            sliderState->editMode = false;
            inEditMode = false;
        }

        bool textModified = false;

        if (inEditMode)
        {
            // Wrap MakeNumericalBoxHandler so Enter/Escape blur back to the slider bar.
            const NumericFieldConfig<float> cfg{ .minVal = minVal, .maxVal = maxVal };
            GUI::EventHandler inner = MakeNumericalBoxHandler<float>(guiStateManager, value, cfg);
            GUI::EventHandler editHandler =
                [this, inner = std::move(inner)](GUI::ElementEvent&& event) -> bool
                {
                    if (event.type == GUI::EventType::KeyDown &&
                        (event.state.lastKey == GUI::KeyCode::Return ||
                         event.state.lastKey == GUI::KeyCode::Return2 ||
                         event.state.lastKey == GUI::KeyCode::KeyPadEnter ||
                         event.state.lastKey == GUI::KeyCode::Escape))
                    {
                        guiStateManager.Interaction().SetFocusID(GUI::ID::Invalid());
                        guiStateManager.TextEditorFocus().Clear();
                        return true;
                    }
                    return inner ? inner(std::move(event)) : false;
                };

            bool modified = false;
            RegisterControl(
                GUI::Element{
                    .id           = id,
                    .eventHandler = std::move(editHandler),
                    .styleRef     = theme.regularControls.textbox,
                    .flags        = ElementFlags::DoNotMeasureText
                },
                [this, &modified, &value, cfg](GUI::Element& element)
                {
                    return textSystem->RegisterEditableNumber(element.id, value, TextEditBehavior::ContinuousUpdate,
                                                              element.styleRef, element.state, modified, cfg);
                },
                std::move(constraints).FlexGrow(1));
            guiStateManager.SetModified(modified);
            textModified = modified;
        } else
        {
            // ---- Display mode: split-fill bar with optional label/value text overlays. ----

            // Track width from last frame's custom draw; fallback on the first frame.
            const int kDefaultTrackWidth = styleManager.Px(120);
            int trackWidth = m_sliderFieldTrack->GetWidth(id);
            if (trackWidth <= 0)
                trackWidth = kDefaultTrackWidth;

            const float t = (maxVal > minVal) ? std::clamp((value - minVal) / (maxVal - minVal), 0.0f, 1.0f) : 0.0f;

            GUI::Element track{};
            track.id           = id;
            track.state        = guiStateManager.GetCurrentState(id);
            track.styleRef     = theme.sliderFieldTrackStyleRef;
            track.eventHandler = MakeSliderHandler(guiStateManager, elementStorage, &value, trackWidth,
                                                   /*onChanged*/ nullptr, minVal, maxVal);
            const auto trackRef = RegisterElement(std::move(track),
                                                  std::move(constraints).FlexGrow(1));

            const auto bgStyle    = theme.sliderFieldBgStyleRef.visualBoxStyleIndex;
            const auto fillStyle  = theme.sliderFieldFillStyleRef.visualBoxStyleIndex;
            const auto trackState = track.state;
            auto& widthMap        = m_sliderFieldTrack->Widths();
            [[maybe_unused]] auto cdRes = elementStorage.AddCustomDraw(trackRef,
                [bgStyle, fillStyle, trackState, &widthMap, id, t](GUI::CustomDrawContext& ctx)
                {
                    // Both halves draw the full rounded track, sub-clipped at fillX.
                    // Coords are inner-rect-local, so the outer top-left is (outer.pos - inner.pos).
                    const auto outer = ctx.ElementRect();
                    const auto inner = ctx.ElementInnerRect();
                    const int32_t W   = std::max(0, outer.w);
                    const int32_t H   = std::max(0, outer.h);
                    if (W > 0) widthMap[id] = W;
                    if (W <= 0 || H <= 0) return;
                    const int32_t X = outer.x - inner.x;
                    const int32_t Y = outer.y - inner.y;

                    const int32_t fillX = int32_t(t * float(W) + 0.5f);
                    const GUI::IntRect baseRect{ .position = { X, Y }, .size = { W, H } };

                    if (fillX < W)
                        ctx.RenderBox(baseRect,
                                      GUI::IntRect{ .position = { X + fillX, Y }, .size = { W - fillX, H } },
                                      bgStyle, trackState);
                    if (fillX > 0)
                        ctx.RenderBox(baseRect,
                                      GUI::IntRect{ .position = { X, Y }, .size = { fillX, H } },
                                      fillStyle, trackState);
                });

            // Text overlays as flex children, centered by the track's justify-content.
            // NoInput so clicks pass through to the track's slider handler.
            VERIFY(elementStorage.BeginChildren());
            if (!label.empty())
            {
                const GUI::ID lblID = idManager.AllocateDynamicID("slider_field_label");
                const GUI::ElementState lblState = guiStateManager.GetCurrentState(lblID);
                RegisterElement(
                    GUI::Element {
                        .id       = lblID,
                        .textID   = textSystem->RegisterText(lblID, label, theme.sliderFieldTextStyleRef, lblState),
                        .styleRef = theme.sliderFieldTextStyleRef,
                        .flags    = GUI::ElementFlags::NoInput,
                        .state    = lblState
                    },
                    LayoutOverride::Auto());
            }
            {
                const GUI::ID valID = idManager.AllocateDynamicID("slider_field_value");
                const GUI::ElementState valState = guiStateManager.GetCurrentState(valID);
                char valueBuffer[64]; // fits any float with 3 decimals
                const auto formatted = std::format_to_n(valueBuffer, sizeof(valueBuffer), "{:.3f}", value);
                const std::string_view valueText(valueBuffer, size_t(formatted.out - valueBuffer));
                RegisterElement(
                    GUI::Element {
                        .id       = valID,
                        .textID   = textSystem->RegisterText(valID, valueText, theme.sliderFieldTextStyleRef, valState),
                        .styleRef = theme.sliderFieldTextStyleRef,
                        .flags    = GUI::ElementFlags::NoInput,
                        .state    = valState
                    },
                    LayoutOverride::Auto());
            }
            VERIFY(elementStorage.EndChildren());
        }

        return textModified || (value != before);
    }

    namespace
    {
        // Drag handler for the integer Slider / SliderField. Writes the caller's int
        // (must be stable storage) from the cursor X, rounded to a whole number.
        GUI::EventHandler MakeIntSliderDrag(GUIStateManager& gsm, const ElementStorage& es,
                                            int* valuePtr, int minVal, int maxVal, int trackWidth)
        {
            const int range = maxVal - minVal;
            return [&gsm, &es, valuePtr, minVal, maxVal, range, trackWidth](GUI::ElementEvent&& event) -> bool
            {
                auto& store = gsm.GetElementDataStore();
                auto setFromMouse = [&]()
                {
                    int leftX = event.rect ? event.rect->rect.position.x : 0;
                    int w     = event.rect ? event.rect->rect.w : trackWidth;
                    if (w <= 0) w = 1;
                    int cx    = event.rect ? event.LocalCursor(es).x : event.state.screenPos.x;
                    float tt  = std::clamp(float(cx - leftX) / float(w), 0.0f, 1.0f);
                    int nv    = std::clamp(minVal + int(std::lround(tt * float(range))), minVal, maxVal);
                    if (nv != *valuePtr) *valuePtr = nv;
                };
                switch (event.type)
                {
                    case GUI::EventType::MouseDown:
                        if (event.state.LeftPressed())
                        {
                            gsm.Interaction().SetFocusID(event.id);
                            gsm.TextEditorFocus().Clear();
                            store.GetOrCreate<SliderState>(event.id).draggingID = event.id;
                            setFromMouse();
                            return true;
                        }
                        break;
                    case GUI::EventType::MouseMove:
                        if (auto* s = store.Get<SliderState>(event.id); s && s->draggingID == event.id)
                        { setFromMouse(); return true; }
                        break;
                    case GUI::EventType::MouseUp:
                        if (auto* s = store.Get<SliderState>(event.id); s && s->draggingID == event.id)
                        { s->draggingID = GUI::ID::Invalid(); return true; }
                        break;
                    default: break;
                }
                return false;
            };
        }
    }

    // ---------------------------------------------------------------------------
    // Integer SliderField — same bar as the float version, drag-only (a click just focuses).
    // ---------------------------------------------------------------------------
    bool GUIApplication::SliderField(ID id, int& value, int minVal, int maxVal,
                                     std::string_view label, LayoutOverride&& constraints)
    {
        if (maxVal < minVal) std::swap(minVal, maxVal);
        value = std::clamp(value, minVal, maxVal);
        const int before = value;
        const int range  = maxVal - minVal;

        const int kDefaultTrackWidth = styleManager.Px(120);
        int trackWidth = m_sliderFieldTrack->GetWidth(id);
        if (trackWidth <= 0) trackWidth = kDefaultTrackWidth;

        const float t = (range > 0)
            ? std::clamp(float(value - minVal) / float(range), 0.0f, 1.0f) : 0.0f;

        int* valuePtr = &value; // caller-owned, stable storage for the end-of-frame write
        GUI::Element track{};
        track.id       = id;
        track.state    = guiStateManager.GetCurrentState(id);
        track.styleRef = theme.sliderFieldTrackStyleRef;
        track.eventHandler = MakeIntSliderDrag(guiStateManager, elementStorage,
                                               valuePtr, minVal, maxVal, trackWidth);
        const auto trackRef = RegisterElement(std::move(track), std::move(constraints).FlexGrow(1));

        const auto bgStyle    = theme.sliderFieldBgStyleRef.visualBoxStyleIndex;
        const auto fillStyle  = theme.sliderFieldFillStyleRef.visualBoxStyleIndex;
        const auto trackState = track.state;
        auto& widthMap        = m_sliderFieldTrack->Widths();
        [[maybe_unused]] auto cdRes = elementStorage.AddCustomDraw(trackRef,
            [bgStyle, fillStyle, trackState, &widthMap, id, t](GUI::CustomDrawContext& ctx)
            {
                const auto outer = ctx.ElementRect();
                const auto inner = ctx.ElementInnerRect();
                const int32_t W = std::max(0, outer.w);
                const int32_t H = std::max(0, outer.h);
                if (W > 0) widthMap[id] = W;
                if (W <= 0 || H <= 0) return;
                const int32_t X = outer.x - inner.x;
                const int32_t Y = outer.y - inner.y;
                const int32_t fillX = int32_t(t * float(W) + 0.5f);
                const GUI::IntRect baseRect{ .position = { X, Y }, .size = { W, H } };
                if (fillX < W)
                    ctx.RenderBox(baseRect,
                                  GUI::IntRect{ .position = { X + fillX, Y }, .size = { W - fillX, H } },
                                  bgStyle, trackState);
                if (fillX > 0)
                    ctx.RenderBox(baseRect,
                                  GUI::IntRect{ .position = { X, Y }, .size = { fillX, H } },
                                  fillStyle, trackState);
            });

        VERIFY(elementStorage.BeginChildren());
        if (!label.empty())
        {
            const GUI::ID lblID = idManager.AllocateDynamicID("slider_field_label");
            const GUI::ElementState lblState = guiStateManager.GetCurrentState(lblID);
            RegisterElement(
                GUI::Element {
                    .id       = lblID,
                    .textID   = textSystem->RegisterText(lblID, label, theme.sliderFieldTextStyleRef, lblState),
                    .styleRef = theme.sliderFieldTextStyleRef,
                    .flags    = GUI::ElementFlags::NoInput,
                    .state    = lblState
                },
                LayoutOverride::Auto());
        }
        {
            const GUI::ID valID = idManager.AllocateDynamicID("slider_field_value");
            const GUI::ElementState valState = guiStateManager.GetCurrentState(valID);
            const std::string valueText = std::format("{}", value);
            RegisterElement(
                GUI::Element {
                    .id       = valID,
                    .textID   = textSystem->RegisterText(valID, valueText, theme.sliderFieldTextStyleRef, valState),
                    .styleRef = theme.sliderFieldTextStyleRef,
                    .flags    = GUI::ElementFlags::NoInput,
                    .state    = valState
                },
                LayoutOverride::Auto());
        }
        VERIFY(elementStorage.EndChildren());

        return value != before;
    }

    // ---------------------------------------------------------------------------
    // Slider — groove + knob without value text; drag-only, integer-valued.
    // ---------------------------------------------------------------------------
    bool GUIApplication::Slider(ID id, int& value, int minVal, int maxVal,
                                LayoutOverride&& constraints)
    {
        if (maxVal < minVal) std::swap(minVal, maxVal);
        value = std::clamp(value, minVal, maxVal);
        const int before = value;
        const int range  = maxVal - minVal;

        const int kDefaultTrackWidth = styleManager.Px(120);
        int trackWidth = m_sliderFieldTrack->GetWidth(id);
        if (trackWidth <= 0) trackWidth = kDefaultTrackWidth;

        const float t = (range > 0)
            ? std::clamp(float(value - minVal) / float(range), 0.0f, 1.0f) : 0.0f;

        GUI::Element track{};
        track.id           = id;
        track.state        = guiStateManager.GetCurrentState(id);
        track.styleRef     = theme.sliderStyleRef;
        track.eventHandler = MakeIntSliderDrag(guiStateManager, elementStorage,
                                               &value, minVal, maxVal, trackWidth);
        const auto trackRef = RegisterElement(std::move(track), std::move(constraints));

        const auto grooveStyle = theme.sliderGrooveStyleRef.visualBoxStyleIndex;
        const auto knobStyle   = theme.sliderKnobStyleRef.visualBoxStyleIndex;
        const auto trackState  = track.state;
        auto& widthMap         = m_sliderFieldTrack->Widths();
        [[maybe_unused]] auto cdRes = elementStorage.AddCustomDraw(trackRef,
            [grooveStyle, knobStyle, trackState, &widthMap, id, t,
             grooveMin = styleManager.Px(3), grooveMax = styleManager.Px(6),
             knobMin = styleManager.Px(6), knobInset = styleManager.Px(2)](GUI::CustomDrawContext& ctx)
            {
                const auto outer = ctx.ElementRect();
                const auto inner = ctx.ElementInnerRect();
                const int W  = std::max(0, outer.w);          // full width (matches the drag handler)
                const int Hc = std::max(0, inner.h);          // content height: respects the top padding
                if (W > 0) widthMap[id] = W;
                if (W <= 0 || Hc <= 0) return;
                const int X = outer.x - inner.x;              // outer left in inner-local coords

                // Groove: full width, centred in the content area (below the top padding).
                const int grooveH = std::clamp(Hc / 4, grooveMin, grooveMax);
                ctx.RenderBox(GUI::IntRect{ .position = { X, (Hc - grooveH) / 2 },
                                            .size     = { W, grooveH } },
                              grooveStyle, trackState);

                // Knob: slightly smaller than the content height, kept inside the track.
                const int knobSz = std::max(knobMin, Hc - knobInset);
                const int knobX  = X + static_cast<int>(std::lround(t * float(std::max(0, W - knobSz))));
                ctx.RenderBox(GUI::IntRect{ .position = { knobX, (Hc - knobSz) / 2 },
                                            .size     = { knobSz, knobSz } },
                              knobStyle, trackState);
            });

        return value != before;
    }
}