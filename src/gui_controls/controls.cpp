#include <gui_controls/gui_application.h>

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath>
#include <format>
#include <string>

namespace interstice
{
    // ---------------------------------------------------------------------------
    // Event handler factories
    // ---------------------------------------------------------------------------

    interstice::EventHandler GUIApplication::MakeToggleHandler(bool& toggle) { return interstice::MakeButtonHandler(guiStateManager, [this, &toggle]() { toggle = !toggle; }); }
    interstice::EventHandler GUIApplication::MakeButtonHandler(std::function<void()> onClick) { return interstice::MakeButtonHandler(guiStateManager, onClick); }
    interstice::EventHandler GUIApplication::MakeLabelHandler() { return interstice::MakeLabelHandler(guiStateManager, elementStorage); }


    // ---------------------------------------------------------------------------
    // Element registration
    // ---------------------------------------------------------------------------

    interstice::ElementRef GUIApplication::RegisterElementInternal(interstice::Element&& element, LayoutOverride&& constraints, bool updateState)
    {
        if (updateState) element.state |= guiStateManager.GetCurrentState(element.id);
        auto addRes = elementStorage.Add(std::move(element), std::move(constraints));
        VERIFY(addRes.status);
        VERIFY(windowRegistry.Add(addRes.ref));
        return addRes.ref;
    }

    interstice::ElementRef GUIApplication::RegisterElement(interstice::Element&& element, LayoutOverride&& constraints)
    {
	    return RegisterElementInternal(std::move(element), std::move(constraints), true);
    }

    interstice::ElementRef GUIApplication::RegisterElement(interstice::Element&& element, std::string_view text, LayoutOverride&& constraints)
    {
        element.state |= guiStateManager.GetCurrentState(element.id); // required for RegisterText
        element.textID = textSystem->RegisterText(element.id, text, element.styleRef, element.state);
        return RegisterElementInternal(std::move(element), std::move(constraints),
		    false); // already updated state, so skipping it in RegisterElementInternal
    }

    // A control without an ID can't take input or keep state: it's drawn, nothing more.
    void GUIApplication::ReportInvalidControlID(ID id)
    {
        if (id == ID::Invalid())
            ReportMisuse("GUIApplication: a control with an invalid ID (it is drawn but takes no input and keeps no state)");
    }

    interstice::ElementRef GUIApplication::RegisterControl(interstice::Element&& element, std::string_view text, LayoutOverride&& constraints)
    {
        element.flags |= ElementFlags::IsControl;
        ReportInvalidControlID(element.id);
        tabChainManager.RegisterTabTarget(element.id);
        return RegisterElement(std::move(element), text, std::move(constraints));
    }

    interstice::ElementRef GUIApplication::RegisterControl(interstice::Element&& element, LayoutOverride&& constraints)
    {
        element.flags |= ElementFlags::IsControl;
        ReportInvalidControlID(element.id);
        tabChainManager.RegisterTabTarget(element.id);
        return RegisterElement(std::move(element), std::move(constraints));
    }

    interstice::ElementRef GUIApplication::RegisterControl(interstice::Element&& element, std::function<TextID(interstice::Element& element)> createTextID, LayoutOverride&& constraints)
    {
        element.flags |= ElementFlags::IsControl;
        ReportInvalidControlID(element.id);
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
            RegisterControl(interstice::Element { .id = id, .styleRef = style, .flags = ElementFlags::DoNotMeasureText },
                [this](interstice::Element& element) { return textSystem->RegisterText(element.id, "\xe2\x80\x94", element.styleRef, element.state); },
                std::move(constraints));
            return false;
        }

        bool modified = false;
        RegisterControl(interstice::Element{ .id = id, .eventHandler = MakeTextBoxHandler(guiStateManager), .styleRef = style, .flags = ElementFlags::DoNotMeasureText },
            [this, &modified, &value, textEditBehavior](interstice::Element& element)
            {
                return textSystem->RegisterEditableText(element.id, value, textEditBehavior, element.styleRef, element.state, modified);
            },
            std::move(constraints));
        guiStateManager.SetModified(modified);
        return modified;
    }

    bool GUIApplication::SearchField(ID id, std::string& value, IconID icon, TextEditBehavior textEditBehavior, LayoutOverride&& constraints, ID* outFieldID)
    {
        // The container draws the textbox chrome and takes the inner field's
        // hover/focus state (PropagatesDescendantState); the field itself is borderless.
        interstice::Element container{};
        container.id       = id;
        container.styleRef = theme.searchTextboxStyleRef;
        container.flags    = ElementFlags::PropagatesDescendantState;
        container.state    = guiStateManager.GetCurrentState(id);
        RegisterElement(std::move(container), std::move(constraints));
        VERIFY(elementStorage.BeginChildren());

        {
            const interstice::ID iconID = idManager.AllocateDynamicID("searchIcon");
            interstice::Element iconElem{};
            iconElem.id       = iconID;
            iconElem.iconID   = icon;
            iconElem.flags    = ElementFlags::NoInput;
            iconElem.state    = guiStateManager.GetCurrentState(iconID);
            iconElem.styleRef = theme.searchTextboxIconStyleRef;
            auto res = elementStorage.Add(std::move(iconElem), interstice::LayoutOverride{});
            VERIFY(res.status);
            VERIFY(windowRegistry.Add(res.ref));
        }

        const ID fieldID = idManager.AllocateDynamicID("searchField");
        if (outFieldID) *outFieldID = fieldID;
        const bool modified = Field(fieldID,
                                    value, textEditBehavior,
                                    theme.searchTextboxFieldStyleRef,
                                    interstice::LayoutOverride{}.FlexGrow(1));

        VERIFY(elementStorage.EndChildren());
        return modified;
    }

    bool GUIApplication::Field(ID id, bool& value, std::string_view text, StyleRef style, LayoutOverride&& constraints)
    {
		return Toggle(id, value, text, style, std::move(constraints));
    }

    void GUIApplication::DragLabel(ID id, std::string_view text, StyleRef style, LayoutOverride&& constraints)
    {
        guiStateManager.Labels().RegisterPendingLabel(id);
        interstice::EventHandler handler;
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
        auto ref = RegisterElement(interstice::Element{ .id = id, .eventHandler = std::move(handler), .styleRef = style, .flags = ElementFlags::IsLabel }, text,
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

    void GUIApplication::Label(ID id, std::string_view text, std::string_view caption, LayoutOverride&& constraints)
    {
        // A label container (the group label, with its width and sizer) holding the title
        // and the caption, at the top of its row.
        guiStateManager.Labels().RegisterPendingLabel(id);
        interstice::EventHandler handler = MakeLabelHandler();
        auto* gs = m_labelGroup->GetActiveGroup();
        if (gs)
        {
            constraints.Width(styleManager.Px(gs->labelWidth));
            if (gs->resizable)
                handler = MakeGroupLabelHandler(guiStateManager, elementStorage,
                    { &gs->labelWidth, gs->minWidth, gs->maxWidth,
                      &gs->edgeHovered, &gs->edgeDragging,
                      &gs->labelTextIDs, textSystem,
                      &styleManager, theme.groupLabelStyleRef });
        }
        const auto ref = RegisterElement(interstice::Element{ .id = id, .eventHandler = std::move(handler),
                                                              .styleRef = theme.groupLabelTopStyleRef, .flags = ElementFlags::IsLabel },
                                         std::move(constraints));
        VERIFY(elementStorage.BeginChildren());
        Text(idManager.AllocateDynamicID("label_title"), text, theme.groupLabelTitleStyleRef);
        Text(idManager.AllocateDynamicID("label_caption"), caption, theme.groupLabelCaptionStyleRef);
        VERIFY(elementStorage.EndChildren());
        if (gs && gs->resizable)
            m_labelGroup->AddDragger(ref);
    }

    void GUIApplication::CollectionHeader(std::string_view valueType)
    {
        BeginHorizontal(idManager.AllocateDynamicID("collection_header"), theme.collectionHeaderStyleRef, LayoutOverride{}.NoShrink());
        BeginHorizontal(idManager.AllocateDynamicID("collection_key_head"), theme.collectionHeaderCellStyleRef, LayoutOverride{}.FlexGrow(1));
        Text(idManager.AllocateDynamicID("collection_key"), "Key", theme.collectionHeaderCaptionStyleRef);
        Text(idManager.AllocateDynamicID("collection_key_type"), "string", theme.collectionHeaderTypeStyleRef);
        EndHorizontal();
        BeginHorizontal(idManager.AllocateDynamicID("collection_value_head"), theme.collectionHeaderCellStyleRef, LayoutOverride{}.FlexGrow(1));
        Text(idManager.AllocateDynamicID("collection_value"), "Value", theme.collectionHeaderCaptionStyleRef);
        Text(idManager.AllocateDynamicID("collection_value_type"), valueType, theme.collectionHeaderTypeStyleRef);
        EndHorizontal();
        Element gap{ .id = idManager.AllocateDynamicID("collection_remove_gap"), .styleRef = theme.collectionRemoveStyleRef, .flags = ElementFlags::NoInput };
        RegisterElement(std::move(gap), LayoutOverride{});   // over the remove buttons
        EndHorizontal();
    }

    void GUIApplication::Label(ID id, std::string_view text, StyleRef style, LayoutOverride&& constraints)
    {
        guiStateManager.Labels().RegisterPendingLabel(id);
        interstice::EventHandler handler;
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
        auto ref = RegisterElement(interstice::Element{ .id = id, .eventHandler = std::move(handler), .styleRef = style, .flags = ElementFlags::IsLabel }, text,
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
        auto ref = RegisterElement(interstice::Element{ .id = id, .styleRef = style }, text, std::move(constraints));
    }

    void GUIApplication::SelectableText(ID id, const std::span<ContentRun>& runs,
                                        std::function<void(intptr_t)> onLinkActivate,
                                        StyleRef style, LayoutOverride&& constraints)
    {
        // Per-element press position, so a press+release without movement reads as a click
        // (activate the link under it) while a drag is a text selection.
        struct PressState { int2 pressPos {}; };

        interstice::EventHandler handler =
            [this, id, onLinkActivate = std::move(onLinkActivate)](interstice::ElementEvent&& ev) -> bool
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
                case interstice::EventType::MouseMove:
                    if (textID != TextID::Invalid)
                        textSystem->SetHoveredContent(textID, textSystem->HitTestContent(textID, localPos.x, localPos.y));
                    break;
                case interstice::EventType::MouseDown:
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
                case interstice::EventType::MouseUp:
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
                case interstice::EventType::KeyDown:
                case interstice::EventType::KeyUp:
                case interstice::EventType::TextInput:
                    // Takes the selection keys (Ctrl+C, Ctrl+A, arrows) while focused.
                    if (guiStateManager.Interaction().GetFocusedID() == id && !ev.state.consumed)
                        return ev.type != interstice::EventType::TextInput && TextEditorUsesKey(ev.state, /*readOnly*/ true);
                    break;
                default:
                    break;
            }
            return false;
        };

        bool modified = false;
        RegisterControl(interstice::Element{ .id = id, .eventHandler = std::move(handler), .styleRef = style },
            [this, &modified, &runs](interstice::Element& element)
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
        interstice::Element el{ .id = id, .styleRef = style };
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
        Image(id, image, style, interstice::LayoutOverride::FixedSize(w, h));
    }

    bool GUIApplication::Button(ID id, IconID icon, std::string_view text, std::function<void()> onClick, StyleRef style, LayoutOverride&& constraints)
    {
        auto* gs = m_buttonGroup->GetActiveButton();
        if (gs) style = m_buttonGroup->NextItemStyleRef(*gs);
        auto ref = RegisterControl(
            interstice::Element{ .id = id, .eventHandler = MakeButtonHandler(onClick), .styleRef = style },
            [this, icon, text](interstice::Element& element) {
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

    bool GUIApplication::Breadcrumb(ID id, std::span<const std::string> segments, std::function<void(int)> onSelect, StyleRef style, LayoutOverride&& constraints)
    {
        bool clicked = false;
        if (!BeginHorizontal(id, style, std::move(constraints)))
            return false;
        // Under the trail's own ID, so the segment IDs don't depend on what came before.
        [[maybe_unused]] const bool pushed = idManager.PushParent(id);
        const int count = int(segments.size());
        for (int i = 0; i < count; ++i)
        {
            if (i > 0)
                Text(idManager.AllocateDynamicID(), "\u203A", theme.breadcrumbSeparatorStyleRef);
            const ID segmentID = idManager.AllocateDynamicID();
            if (i == count - 1)
                Text(segmentID, segments[size_t(i)], theme.breadcrumbCurrentStyleRef);
            else
                clicked = Button(segmentID, segments[size_t(i)], [onSelect, i] { if (onSelect) onSelect(i); },
                                 theme.breadcrumbSegmentStyleRef) || clicked;
        }
        if (pushed)
            (void)idManager.PopParent();
        EndHorizontal();
        return clicked;
    }

    bool GUIApplication::BeginStatusBar(ID id, StyleRef style, LayoutOverride&& constraints)
    {
        return BeginHorizontal(id, style, std::move(constraints));
    }

    void GUIApplication::EndStatusBar()
    {
        EndHorizontal();
    }

    void GUIApplication::StatusText(ID id, std::string_view text, LayoutOverride&& constraints)
    {
        Text(id, text, theme.statusTextStyleRef, std::move(constraints));
    }

    bool GUIApplication::CommandSearch(ID id, CommandSearchState& state, LayoutOverride&& constraints)
    {
        const bool ran = std::exchange(state.ranCommand, false);
        if (state.listWindowID == ID::Invalid())
            state.listWindowID = idManager.CreateStaticID();

        // Under the search's own ID, so the field keeps its ID whatever came before.
        [[maybe_unused]] const bool pushed = idManager.PushParent(id);
        const ID fieldID = idManager.AllocateDynamicID("field");

        auto& interaction = guiStateManager.Interaction();
        if (std::exchange(state.focusRequested, false))
        {
            interaction.SetFocusID(fieldID);
            guiStateManager.TextEditorFocus().Set(fieldID);
        }
        const bool focused = (interaction.GetFocusedID() == fieldID);

        // The current mode's and Default's commands whose description contains the query.
        constexpr size_t kMaxMatches = 12;
        const auto& commands = commandManager.GetCommands();
        const CommandMode mode = commandManager.GetMode();
        std::vector<CommandID> matches;
        if (focused)
        {
            const auto sameLetter = [](char a, char b)
                { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); };
            for (size_t i = 0; i < commands.size() && matches.size() < kMaxMatches; ++i)
            {
                const CommandManager::Command& c = commands[i];
                if (c.description.empty() || !c.callback || c.IsHeld())
                    continue;
                if (c.mode != CommandMode::Default && c.mode != mode)
                    continue;
                if (std::search(c.description.begin(), c.description.end(),
                                state.query.begin(), state.query.end(), sameLetter) != c.description.end())
                    matches.push_back(CommandManager::IDAt(i));
            }
        }
        const int matchCount = int(matches.size());
        state.current = std::clamp(state.current, 0, std::max(matchCount - 1, 0));

        // These run from event handlers.
        auto leave = [this, &state]
        {
            state.query.clear();
            state.current = 0;
            guiStateManager.Interaction().SetFocusID(ID::Invalid());
            guiStateManager.TextEditorFocus().Clear();
        };
        auto run = [this, &state, leave](CommandID command)
        {
            const CommandManager::Command* found = commandManager.GetCommand(command);
            if (found == nullptr)
                return;
            const std::function<void()> callback = found->callback;
            leave();
            state.ranCommand = true;
            if (callback)
                callback();
        };

        // The box: a click anywhere in it starts typing.
        interstice::Element box{};
        box.id           = id;
        box.styleRef     = theme.commandSearchStyleRef;
        box.flags        = ElementFlags::PropagatesDescendantState;
        box.state        = guiStateManager.GetCurrentState(id);
        box.eventHandler = [this, fieldID](ElementEvent&& event) -> bool
        {
            if (event.type != EventType::MouseDown || !event.state.LeftPressed())
                return false;
            guiStateManager.Interaction().SetFocusID(fieldID);
            guiStateManager.TextEditorFocus().Set(fieldID);
            return true;
        };
        RegisterElement(std::move(box), std::move(constraints));
        VERIFY(elementStorage.BeginChildren());

        RegisterElement(interstice::Element{ .id = idManager.AllocateDynamicID("icon"), .iconID = theme.searchIcon,
                                             .styleRef = theme.searchTextboxIconStyleRef, .flags = ElementFlags::NoInput },
                        LayoutOverride{});

        // Up / Down / Enter / Escape drive the list; every other key edits the query.
        const CommandID currentCommand = matchCount > 0 ? matches[size_t(state.current)] : CommandID::Invalid();
        auto textBoxHandler = MakeTextBoxHandler(guiStateManager);
        auto fieldHandler = [this, &state, textBoxHandler, matchCount, currentCommand, run, leave](ElementEvent&& event) -> bool
        {
            if (event.type == EventType::KeyDown && !event.state.consumed
                && guiStateManager.Interaction().GetFocusedID() == event.id)
            {
                switch (event.state.lastKey)
                {
                    case KeyCode::Down:
                        if (matchCount > 0) state.current = (state.current + 1) % matchCount;
                        return true;
                    case KeyCode::Up:
                        if (matchCount > 0) state.current = (state.current + matchCount - 1) % matchCount;
                        return true;
                    case KeyCode::Return:
                    case KeyCode::Return2:
                        run(currentCommand);
                        return true;
                    case KeyCode::Escape:
                        leave();
                        return true;
                    default:
                        break;
                }
            }
            return textBoxHandler(std::move(event));
        };
        bool modified = false;
        RegisterControl(interstice::Element{ .id = fieldID, .eventHandler = std::move(fieldHandler),
                                             .styleRef = theme.searchTextboxFieldStyleRef, .flags = ElementFlags::DoNotMeasureText },
            [this, &modified, &state](interstice::Element& element)
            {
                return textSystem->RegisterEditableText(element.id, state.query, TextEditBehavior::ContinuousUpdate,
                                                        element.styleRef, element.state, modified);
            },
            LayoutOverride{}.FlexGrow(1));
        if (modified)
            state.current = 0;

        if (state.query.empty() && !state.hint.empty())
            RegisterElement(interstice::Element{ .id = idManager.AllocateDynamicID("hint"), .styleRef = theme.commandSearchHintStyleRef,
                                                 .flags = ElementFlags::NoInput },
                            state.hint, LayoutOverride{}.Anchored(fieldID, AnchorPlacement::InsideLeft | AnchorPlacement::CenterVert));
        if (!state.shortcut.empty())
            RegisterElement(interstice::Element{ .id = idManager.AllocateDynamicID("key"), .styleRef = theme.commandSearchKeyStyleRef,
                                                 .flags = ElementFlags::NoInput },
                            state.shortcut, LayoutOverride{});

        VERIFY(elementStorage.EndChildren());

        // The matches, in a popup under the box that leaves the keyboard with the field.
        if (matchCount > 0)
        {
            auto position = LayoutOverride{}.Anchored(id, AnchorPlacement::OutsideBottom | AnchorPlacement::InsideLeft,
                                                      { 0, int16_t(styleManager.Px(4)) }, AutoFlipMode::FlipAvoidTarget);
            const ElementFlags flags = ElementFlags::WindowNoCaption | ElementFlags::WindowIsPopup
                                     | ElementFlags::Shadows | ElementFlags::WindowNotFocusable;
            if (BeginWindow(state.listWindowID, theme.commandSearchListStyleRef, std::move(position), flags))
            {
                const ID rowsID = idManager.AllocateRangedID(uint32_t(kMaxMatches), "rows");
                for (int i = 0; i < matchCount; ++i)
                {
                    const CommandID command = matches[size_t(i)];
                    const CommandManager::Command& c = *commandManager.GetCommand(command);
                    MenuItemRow(idManager.GetSubID(rowsID, uint32_t(i)),
                                interstice::MakeButtonHandler(guiStateManager, [run, command] { run(command); }),
                                i == state.current ? theme.commandSearchCurrentRowStyleRef : theme.menuItemRowStyleRef,
                                /*reserveIcon*/ true, c.icon, c.description,
                                c.HasShortcut() ? std::string_view(c.displayString) : std::string_view{},
                                theme.menuItemShortcutStyleRef, LayoutOverride{});
                }
            }
            EndWindow();
        }

        if (pushed)
            (void)idManager.PopParent();
        return ran;
    }

    std::string GUIApplication::PendingLabelText() const
    {
        const ID labelID = guiStateManager.Labels().GetPendingLabelID();
        if (labelID == ID::Invalid()) return {};
        const ElementRef ref = elementStorage.FindElementRefByID(labelID);
        if (!ref.IsValid()) return {};
        const TextID textID = elementStorage.GetElementUnsafe(ref).textID;
        return textID != TextID::Invalid ? std::string(textSystem->GetText(textID)) : std::string{};
    }

    bool GUIApplication::Checkbox(ID id, bool value, std::string_view text, std::function<void(bool)> onToggle, LayoutOverride&& constraints)
    {
        // Selected while checked, so the box and label restyle with it.
        if (value) guiStateManager.Construction().PushSelected();
        const interstice::ElementState state = guiStateManager.GetCurrentState(id);
        interstice::Element control{};
        control.id           = id;
        control.state        = state;
        control.styleRef     = theme.checkboxStyleRef;
        control.eventHandler = interstice::MakeButtonHandler(guiStateManager, [value, onToggle]() { onToggle(!value); });
        RegisterElement(std::move(control), std::move(constraints));
        VERIFY(elementStorage.BeginChildren());
        {
            const interstice::ElementState partState = value ? (state | interstice::ElementState::Selected) : state;
            interstice::Element box{};
            box.id       = idManager.AllocateDynamicID("checkboxBox");
            box.state    = partState;
            box.styleRef = theme.checkboxBoxStyleRef;
            box.flags    = interstice::ElementFlags::NoInput;
            box.iconID   = value ? theme.menuCheckIcon : IconID::Invalid;
            RegisterElement(std::move(box), LayoutOverride{});
            if (!text.empty())
            {
                interstice::Element label{};
                label.id       = idManager.AllocateDynamicID("checkboxLabel");
                label.state    = partState;
                label.styleRef = theme.checkboxLabelStyleRef;
                label.flags    = interstice::ElementFlags::NoInput;
                label.textID   = textSystem->RegisterText(label.id, text, label.styleRef, label.state);
                RegisterElement(std::move(label), LayoutOverride{});
            }
        }
        VERIFY(elementStorage.EndChildren());
        if (value) guiStateManager.Construction().PopSelected();
        const bool modified = guiStateManager.Interaction().HasBeenClicked(id);
        guiStateManager.SetModified(modified);
        return modified;
    }

    bool GUIApplication::Switch(ID id, bool value, std::string_view text, std::function<void(bool)> onToggle, StyleRef style, LayoutOverride&& constraints)
    {
        // Inside a node row it sits at the right, by its socket, where fields end.
        if (nodeRowFieldMinWidth > 0)
        {
            interstice::Element spacer{};
            spacer.id       = idManager.AllocateDynamicID("switchSpacer");
            spacer.styleRef = theme.emptyHorzStyleRef;
            spacer.flags    = interstice::ElementFlags::NoInput;
            RegisterElement(std::move(spacer), LayoutOverride{}.FlexGrow(1));
        }

        // Selected while on, so the track, knob and label restyle with it.
        if (value) guiStateManager.Construction().PushSelected();

        // The whole control takes the click; its parts are NoInput and share its state.
        const interstice::ElementState state = guiStateManager.GetCurrentState(id);
        interstice::Element control{};
        control.id           = id;
        control.state        = state;
        control.styleRef     = style;
        control.eventHandler = interstice::MakeButtonHandler(guiStateManager, [value, onToggle]() { onToggle(!value); });
        RegisterElement(std::move(control), std::move(constraints));
        VERIFY(elementStorage.BeginChildren());
        {
            interstice::Element track{};
            track.id       = idManager.AllocateDynamicID("switchTrack");
            track.state    = state;
            track.styleRef = theme.switchTrackStyleRef;
            track.flags    = interstice::ElementFlags::NoInput;
            const auto trackRef = RegisterElement(std::move(track), LayoutOverride{});

            // The knob, inset evenly, at the track's left when off and its right when on.
            const LayoutStyle* knobLayout = styleManager.GetLayoutStyle(theme.switchKnobStyleRef.layoutStyleIndex);
            const int knobSize = (knobLayout && knobLayout->layoutData.width != LayoutData::kAutoSize)
                               ? int(knobLayout->layoutData.width) : styleManager.Px(12);
            // Drawn as a disc in the knob style's background color.
            const interstice::ElementState knobState = value ? (state | interstice::ElementState::Selected) : state;
            const VisualBoxStyle* knobBox = styleManager.GetVisualBoxStyle(theme.switchKnobStyleRef.visualBoxStyleIndex, knobState);
            const VisualColorStyleIndex knobColor = knobBox ? knobBox->backgroundColorIndex : VisualColorStyleIndex::Invalid;
            VERIFY(elementStorage.AddCustomDraw(trackRef,
                [knobColor, knobSize, value](interstice::CustomDrawContext& ctx)
                {
                    const auto outer = ctx.ElementRect();
                    const int inset = std::max(0, (outer.h - knobSize) / 2);
                    const int x = value ? outer.x + outer.w - inset - knobSize : outer.x + inset;
                    ctx.RenderQuad({ .position = { x, outer.y + inset }, .size = { knobSize, knobSize } },
                                   knobColor, float(knobSize) * 0.5f);
                }));

            if (!text.empty())
            {
                interstice::Element label{};
                label.id       = idManager.AllocateDynamicID("switchLabel");
                label.state    = value ? (state | interstice::ElementState::Selected) : state;
                label.styleRef = theme.switchLabelStyleRef;
                label.flags    = interstice::ElementFlags::NoInput;
                label.textID   = textSystem->RegisterText(label.id, text, label.styleRef, label.state);
                RegisterElement(std::move(label), LayoutOverride{});
            }
        }
        VERIFY(elementStorage.EndChildren());

        if (value) guiStateManager.Construction().PopSelected();
        const bool modified = guiStateManager.Interaction().HasBeenClicked(id);
        guiStateManager.SetModified(modified);
        return modified;
    }

    bool GUIApplication::Toggle(ID id, bool value, IconID icon, std::string_view text, std::function<void(bool)> onToggle, StyleRef style, LayoutOverride&& constraints)
    {
	    bool currentValue = value;
        if (currentValue) guiStateManager.Construction().PushSelected();
        auto* gs = m_buttonGroup->GetActiveToggle();
        if (gs) style = m_buttonGroup->NextItemStyleRef(*gs);
        auto ref = RegisterControl(
            interstice::Element 
            { 
                .id = id, 
                .eventHandler = interstice::MakeButtonHandler(guiStateManager, 
                                                       [this, value, onToggle]() 
                                                       { 
                                                           onToggle(!value); 
                                                       }), 
                .styleRef = style 
            },
            [this, icon, text](interstice::Element& element) {
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

    bool GUIApplication::BeginLabelGroup(ID id, int32_t defaultWidth, int32_t minWidth, int32_t maxWidth, bool resizable)
    {
        m_labelGroup->BeginGroup(id, defaultWidth, minWidth, maxWidth, resizable);
        return true;
    }

    void GUIApplication::EndLabelGroup()
    {
        m_labelGroup->EndGroup();
    }

    // ---------------------------------------------------------------------------
    // Button / Toggle groups (tight horizontal pack with shared borders)
    // ---------------------------------------------------------------------------

    bool GUIApplication::BeginButtonGroup(LayoutOverride&& constraints)
    {
        interstice::ID groupID = idManager.AllocateDynamicID("button_group");
        interstice::Element wrap{};
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
        return true;
    }

    void GUIApplication::EndButtonGroup()
    {
        m_buttonGroup->EndButtonGroup();
        VERIFY(elementStorage.EndChildren());
    }

    bool GUIApplication::BeginToggleGroup(LayoutOverride&& constraints)
    {
        interstice::ID groupID = idManager.AllocateDynamicID("toggle_group");
        interstice::Element wrap{};
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
        return true;
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
        interstice::Element container
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
        const bool miss = OpenCached(id, style, signature, std::move(constraints));
        m_cachedOpen.push_back(miss);
        return miss;
    }

    bool GUIApplication::OpenCached(ID id, StyleRef style, uint64_t signature, LayoutOverride&& constraints)
    {
        // Scope the dynamic-ID counter so skipping the body on a hit can't shift later siblings' IDs.
        [[maybe_unused]] bool pushed = idManager.PushParent(id);
        assert(pushed && "BeginCached requires a valid, stable id");

        // The container itself is built every frame, so its layout and hit-testing stay live.
        interstice::Element container{ .id = id, .styleRef = style, .flags = ElementFlags::IsCachedScope };
        container.state = guiStateManager.GetCurrentState(id);
        interstice::ElementRef containerRef = RegisterElement(std::move(container), std::move(constraints));
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
            return false; // hit - the caller skips the body (EndCached is still called)
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

    // Called for every BeginCached, hit or miss.
    void GUIApplication::EndCached()
    {
        if (m_cachedOpen.empty())
        {
            ReportMisuse("GUIApplication: EndCached without BeginCached (ignored)");
            return;
        }
        const bool miss = m_cachedOpen.back();
        m_cachedOpen.pop_back();
        if (!miss)
            return;   // a hit closed its container in BeginCached
        if (m_cacheRecordStack.empty())
        {
            ReportMisuse("GUIApplication: EndCached without a matching BeginCached (ignored)");
            return;
        }
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

    bool GUIApplication::BeginToolbar(bool horizontal)
    {
        controlStylesStack.push_back(theme.controls);
        theme.controls = &theme.toolbarControls;
        if (horizontal) BeginHorizontalWrap();
        else            BeginVerticalWrap();
        return true;
    }
    void GUIApplication::EndToolbar()
    {
        if (controlStylesStack.empty())
        {
            ReportMisuse("GUIApplication: EndToolbar without BeginToolbar (ignored)");
            return;
        }
        EndArea();
        theme.controls = controlStylesStack.back();
        controlStylesStack.pop_back();
    }


    // ---------------------------------------------------------------------------
    // SliderField - Blender-style filled bar. The bar IS the textbox: dragging
    // anywhere on it updates value; a clean click swaps it for an in-place
    // numeric Field. Optional `label` is rendered as text overlay on the bar.
    // ---------------------------------------------------------------------------
    bool GUIApplication::SliderField(ID id, float& value, float minVal, float maxVal,
                                  std::string_view text, LayoutOverride&& constraints)
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
            interstice::EventHandler inner = MakeNumericalBoxHandler<float>(guiStateManager, value, cfg);
            interstice::EventHandler editHandler =
                [this, inner = std::move(inner)](interstice::ElementEvent&& event) -> bool
                {
                    if (event.type == interstice::EventType::KeyDown &&
                        (event.state.lastKey == interstice::KeyCode::Return ||
                         event.state.lastKey == interstice::KeyCode::Return2 ||
                         event.state.lastKey == interstice::KeyCode::KeyPadEnter ||
                         event.state.lastKey == interstice::KeyCode::Escape))
                    {
                        guiStateManager.Interaction().SetFocusID(interstice::ID::Invalid());
                        guiStateManager.TextEditorFocus().Clear();
                        return true;
                    }
                    return inner ? inner(std::move(event)) : false;
                };

            bool modified = false;
            RegisterControl(
                interstice::Element{
                    .id           = id,
                    .eventHandler = std::move(editHandler),
                    .styleRef     = theme.regularControls.textbox,
                    .flags        = ElementFlags::DoNotMeasureText
                },
                [this, &modified, &value, cfg](interstice::Element& element)
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

            interstice::Element track{};
            track.id           = id;
            track.state        = guiStateManager.GetCurrentState(id);
            track.styleRef     = theme.sliderFieldTrackStyleRef;
            track.eventHandler = MakeSliderHandler(guiStateManager, elementStorage, &value, trackWidth,
                                                   /*onChanged*/ nullptr, minVal, maxVal);
            const auto trackRef = RegisterElement(std::move(track),
                                                  std::move(constraints).FlexGrow(1));

            const auto bgStyle    = theme.sliderFieldBgStyleRef.visualBoxStyleIndex;
            const auto fillStyle  = theme.sliderFieldFillStyleRef.visualBoxStyleIndex;
            const auto edgeStyle  = theme.sliderFieldEdgeStyleRef.visualBoxStyleIndex;
            const auto trackState = track.state;
            auto& widthMap        = m_sliderFieldTrack->Widths();
            const LayoutStyle* edgeLayout = styleManager.GetLayoutStyle(theme.sliderFieldEdgeStyleRef.layoutStyleIndex);
            const int32_t edgeWidth = (edgeLayout && edgeLayout->layoutData.width != LayoutData::kAutoSize)
                                    ? edgeLayout->layoutData.width : styleManager.Px(2);
            [[maybe_unused]] auto cdRes = elementStorage.AddCustomDraw(trackRef,
                [bgStyle, fillStyle, edgeStyle, edgeWidth, trackState, &widthMap, id, t](interstice::CustomDrawContext& ctx)
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
                    const interstice::IntRect baseRect{ .position = { X, Y }, .size = { W, H } };

                    if (fillX < W)
                        ctx.RenderBox(baseRect,
                                      interstice::IntRect{ .position = { X + fillX, Y }, .size = { W - fillX, H } },
                                      bgStyle, trackState);
                    if (fillX > 0)
                        ctx.RenderBox(baseRect,
                                      interstice::IntRect{ .position = { X, Y }, .size = { fillX, H } },
                                      fillStyle, trackState);
                    // The fill ends in an edge line, kept inside the track.
                    if (fillX > 0 && edgeWidth > 0)
                    {
                        const int32_t edgeX = std::clamp(fillX - edgeWidth, int32_t(0), std::max(0, W - edgeWidth));
                        ctx.RenderBox(baseRect,
                                      interstice::IntRect{ .position = { X + edgeX, Y }, .size = { edgeWidth, H } },
                                      edgeStyle, trackState);
                    }
                });

            // Text overlays as flex children, centered by the track's justify-content.
            // NoInput so clicks pass through to the track's slider handler.
            VERIFY(elementStorage.BeginChildren());
            if (!text.empty())
            {
                const interstice::ID lblID = idManager.AllocateDynamicID("slider_field_label");
                const interstice::ElementState lblState = guiStateManager.GetCurrentState(lblID);
                RegisterElement(
                    interstice::Element {
                        .id       = lblID,
                        .textID   = textSystem->RegisterText(lblID, text, theme.sliderFieldTextStyleRef, lblState),
                        .styleRef = theme.sliderFieldTextStyleRef,
                        .flags    = interstice::ElementFlags::NoInput,
                        .state    = lblState
                    },
                    LayoutOverride::Auto());
            }
            {
                const interstice::ID valID = idManager.AllocateDynamicID("slider_field_value");
                const interstice::ElementState valState = guiStateManager.GetCurrentState(valID);
                char valueBuffer[64]; // fits any float with 3 decimals
                const auto formatted = std::format_to_n(valueBuffer, sizeof(valueBuffer), "{:.3f}", value);
                const std::string_view valueText(valueBuffer, size_t(formatted.out - valueBuffer));
                RegisterElement(
                    interstice::Element {
                        .id       = valID,
                        .textID   = textSystem->RegisterText(valID, valueText, theme.sliderFieldValueStyleRef, valState),
                        .styleRef = theme.sliderFieldValueStyleRef,
                        .flags    = interstice::ElementFlags::NoInput,
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
        interstice::EventHandler MakeIntSliderDrag(GUIStateManager& gsm, const ElementStorage& es,
                                            int* valuePtr, int minVal, int maxVal, int trackWidth)
        {
            const int range = maxVal - minVal;
            return [&gsm, &es, valuePtr, minVal, maxVal, range, trackWidth](interstice::ElementEvent&& event) -> bool
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
                    case interstice::EventType::MouseDown:
                        if (event.state.LeftPressed())
                        {
                            gsm.Interaction().SetFocusID(event.id);
                            gsm.TextEditorFocus().Clear();
                            store.GetOrCreate<SliderState>(event.id).draggingID = event.id;
                            setFromMouse();
                            return true;
                        }
                        break;
                    case interstice::EventType::MouseMove:
                        if (auto* s = store.Get<SliderState>(event.id); s && s->draggingID == event.id)
                        { setFromMouse(); return true; }
                        break;
                    case interstice::EventType::MouseUp:
                        if (auto* s = store.Get<SliderState>(event.id); s && s->draggingID == event.id)
                        { s->draggingID = interstice::ID::Invalid(); return true; }
                        break;
                    default: break;
                }
                return false;
            };
        }
    }

    // ---------------------------------------------------------------------------
    // Integer SliderField - same bar as the float version, drag-only (a click just focuses).
    // ---------------------------------------------------------------------------
    bool GUIApplication::SliderField(ID id, int& value, int minVal, int maxVal,
                                     std::string_view text, LayoutOverride&& constraints)
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
        interstice::Element track{};
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
            [bgStyle, fillStyle, trackState, &widthMap, id, t](interstice::CustomDrawContext& ctx)
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
                const interstice::IntRect baseRect{ .position = { X, Y }, .size = { W, H } };
                if (fillX < W)
                    ctx.RenderBox(baseRect,
                                  interstice::IntRect{ .position = { X + fillX, Y }, .size = { W - fillX, H } },
                                  bgStyle, trackState);
                if (fillX > 0)
                    ctx.RenderBox(baseRect,
                                  interstice::IntRect{ .position = { X, Y }, .size = { fillX, H } },
                                  fillStyle, trackState);
            });

        VERIFY(elementStorage.BeginChildren());
        if (!text.empty())
        {
            const interstice::ID lblID = idManager.AllocateDynamicID("slider_field_label");
            const interstice::ElementState lblState = guiStateManager.GetCurrentState(lblID);
            RegisterElement(
                interstice::Element {
                    .id       = lblID,
                    .textID   = textSystem->RegisterText(lblID, text, theme.sliderFieldTextStyleRef, lblState),
                    .styleRef = theme.sliderFieldTextStyleRef,
                    .flags    = interstice::ElementFlags::NoInput,
                    .state    = lblState
                },
                LayoutOverride::Auto());
        }
        {
            const interstice::ID valID = idManager.AllocateDynamicID("slider_field_value");
            const interstice::ElementState valState = guiStateManager.GetCurrentState(valID);
            const std::string valueText = std::format("{}", value);
            RegisterElement(
                interstice::Element {
                    .id       = valID,
                    .textID   = textSystem->RegisterText(valID, valueText, theme.sliderFieldTextStyleRef, valState),
                    .styleRef = theme.sliderFieldTextStyleRef,
                    .flags    = interstice::ElementFlags::NoInput,
                    .state    = valState
                },
                LayoutOverride::Auto());
        }
        VERIFY(elementStorage.EndChildren());

        return value != before;
    }

    // ---------------------------------------------------------------------------
    // Slider - groove + knob without value text; drag-only, integer-valued.
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

        interstice::Element track{};
        track.id           = id;
        track.state        = guiStateManager.GetCurrentState(id);
        track.styleRef     = theme.sliderStyleRef;
        track.eventHandler = MakeIntSliderDrag(guiStateManager, elementStorage,
                                               &value, minVal, maxVal, trackWidth);
        const auto trackRef = RegisterElement(std::move(track), std::move(constraints));

        const auto grooveStyle = theme.sliderGrooveStyleRef.visualBoxStyleIndex;
        const auto fillStyle   = theme.sliderFillStyleRef.visualBoxStyleIndex;
        const auto knobStyle   = theme.sliderKnobStyleRef.visualBoxStyleIndex;
        const auto trackState  = track.state;
        auto& widthMap         = m_sliderFieldTrack->Widths();
        // Groove height and knob size from their CSS.
        auto cssSize = [this](StyleRef style, bool width, int fallback) {
            const LayoutStyle* ls = styleManager.GetLayoutStyle(style.layoutStyleIndex);
            const int32_t size = ls ? (width ? ls->layoutData.width : ls->layoutData.height) : LayoutData::kAutoSize;
            return size != LayoutData::kAutoSize ? int(size) : fallback;
        };
        const int grooveH = cssSize(theme.sliderGrooveStyleRef, false, styleManager.Px(4));
        const int knobSz  = cssSize(theme.sliderKnobStyleRef, true, styleManager.Px(14));
        [[maybe_unused]] auto cdRes = elementStorage.AddCustomDraw(trackRef,
            [grooveStyle, fillStyle, knobStyle, trackState, &widthMap, id, t, grooveH, knobSz](interstice::CustomDrawContext& ctx)
            {
                const auto outer = ctx.ElementRect();
                const auto inner = ctx.ElementInnerRect();
                const int W  = std::max(0, outer.w);          // full width (matches the drag handler)
                const int Hc = std::max(0, inner.h);          // content height: respects the top padding
                if (W > 0) widthMap[id] = W;
                if (W <= 0 || Hc <= 0) return;
                const int X = outer.x - inner.x;              // outer left in inner-local coords

                // Groove: full width, centred; filled up to the knob's centre.
                const int knobX = X + static_cast<int>(std::lround(t * float(std::max(0, W - knobSz))));
                ctx.RenderBox(interstice::IntRect{ .position = { X, (Hc - grooveH) / 2 },
                                            .size     = { W, grooveH } },
                              grooveStyle, trackState);
                ctx.RenderBox(interstice::IntRect{ .position = { X, (Hc - grooveH) / 2 },
                                            .size     = { knobX + knobSz / 2 - X, grooveH } },
                              fillStyle, trackState);

                // Knob: kept inside the track.
                ctx.RenderBox(interstice::IntRect{ .position = { knobX, (Hc - knobSz) / 2 },
                                            .size     = { knobSz, knobSz } },
                              knobStyle, trackState);
            });

        return value != before;
    }
}