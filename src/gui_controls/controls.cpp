#include <gui_controls/gui_application.h>

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath>
#include <format>
#include <string>

namespace tachyon
{
    // ---------------------------------------------------------------------------
    // Event handler factories
    // ---------------------------------------------------------------------------

    tachyon::EventHandler GUIApplication::MakeToggleHandler(bool& toggle) { return tachyon::MakeButtonHandler(guiStateManager, [&toggle]() { toggle = !toggle; }); }
    tachyon::EventHandler GUIApplication::MakeButtonHandler(std::function<void()> onClick) { return tachyon::MakeButtonHandler(guiStateManager, onClick); }
    tachyon::EventHandler GUIApplication::MakeLabelHandler() { return tachyon::MakeLabelHandler(guiStateManager, elementStorage); }


    // ---------------------------------------------------------------------------
    // Element registration
    // ---------------------------------------------------------------------------

    tachyon::ElementRef GUIApplication::RegisterElementInternal(tachyon::Element&& element, LayoutOverride&& constraints, bool updateState)
    {
        if (updateState) element.state |= guiStateManager.GetCurrentState(element.id);
        auto addRes = elementStorage.Add(std::move(element), std::move(constraints));
        VERIFY(addRes.status);
        VERIFY(windowRegistry.Add(addRes.ref));
        return addRes.ref;
    }

    tachyon::ElementRef GUIApplication::RegisterElement(tachyon::Element&& element, LayoutOverride&& constraints)
    {
	    return RegisterElementInternal(std::move(element), std::move(constraints), true);
    }

    tachyon::ElementRef GUIApplication::RegisterElement(tachyon::Element&& element, std::string_view text, LayoutOverride&& constraints)
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

    void GUIApplication::BeginFieldBox(ID id, StyleRef style, LayoutOverride&& constraints)
    {
        RegisterElement(tachyon::Element{ .id = id, .styleRef = style, .flags = ElementFlags::PropagatesDescendantState },
                        std::move(constraints));
        VERIFY(elementStorage.BeginChildren());
    }

    void GUIApplication::EndFieldBox()
    {
        VERIFY(elementStorage.EndChildren());
    }

    // Tab skips a disabled control, as the mouse does.
    void GUIApplication::RegisterTabStop(ID id)
    {
        if (!guiStateManager.Construction().IsDisabled())
            tabChainManager.RegisterTabTarget(id);
    }

    tachyon::ElementRef GUIApplication::RegisterControl(tachyon::Element&& element, std::string_view text, LayoutOverride&& constraints)
    {
        element.flags |= ElementFlags::IsControl;
        ReportInvalidControlID(element.id);
        RegisterTabStop(element.id);
        return RegisterElement(std::move(element), text, std::move(constraints));
    }

    tachyon::ElementRef GUIApplication::RegisterControl(tachyon::Element&& element, LayoutOverride&& constraints)
    {
        element.flags |= ElementFlags::IsControl;
        ReportInvalidControlID(element.id);
        RegisterTabStop(element.id);
        return RegisterElement(std::move(element), std::move(constraints));
    }

    tachyon::ElementRef GUIApplication::RegisterControl(tachyon::Element&& element, std::function<TextID(tachyon::Element& element)> createTextID, LayoutOverride&& constraints)
    {
        element.flags |= ElementFlags::IsControl;
        ReportInvalidControlID(element.id);
        element.state |= guiStateManager.GetCurrentState(element.id); // required for createTextID callback
        element.textID = createTextID(element);
        RegisterTabStop(element.id);
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
            constraints.FlexGrow(1).MinWidth(Pixels{ nodeRowFieldMinWidth });

        if (guiStateManager.Construction().IsIndeterminate())
        {
            RegisterControl(tachyon::Element { .id = id, .styleRef = style, .flags = ElementFlags::DoNotMeasureText },
                [this](tachyon::Element& element) { return textSystem->RegisterText(element.id, "\xe2\x80\x94", element.styleRef, element.state); },
                std::move(constraints));
            return false;
        }

        bool modified = false;
        RegisterControl(tachyon::Element{ .id = id, .eventHandler = MakeTextBoxHandler(guiStateManager), .styleRef = style, .flags = ElementFlags::DoNotMeasureText },
            [this, &modified, &value, textEditBehavior](tachyon::Element& element)
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
        tachyon::Element container{};
        container.id       = id;
        container.styleRef = theme.searchTextboxStyleRef;
        container.flags    = ElementFlags::PropagatesDescendantState;
        container.state    = guiStateManager.GetCurrentState(id);
        RegisterElement(std::move(container), std::move(constraints));
        VERIFY(elementStorage.BeginChildren());

        {
            const tachyon::ID iconID = idManager.AllocateDynamicID("searchIcon");
            tachyon::Element iconElem{};
            iconElem.id       = iconID;
            iconElem.iconID   = icon;
            iconElem.flags    = ElementFlags::NoInput;
            iconElem.state    = guiStateManager.GetCurrentState(iconID);
            iconElem.styleRef = theme.searchTextboxIconStyleRef;
            auto res = elementStorage.Add(std::move(iconElem), tachyon::LayoutOverride{});
            VERIFY(res.status);
            VERIFY(windowRegistry.Add(res.ref));
        }

        const ID fieldID = idManager.AllocateDynamicID("searchField");
        if (outFieldID) *outFieldID = fieldID;
        const bool modified = Field(fieldID,
                                    value, textEditBehavior,
                                    theme.searchTextboxFieldStyleRef,
                                    tachyon::LayoutOverride{}.FlexGrow(1));

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
        tachyon::EventHandler handler;
        auto* gs = m_labelGroup->GetActiveGroup();
        if (gs)
        {
            constraints.Width(CssPx(gs->labelWidth));
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
        auto ref = RegisterElement(tachyon::Element{ .id = id, .eventHandler = std::move(handler), .styleRef = style, .flags = ElementFlags::IsLabel }, text,
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
        tachyon::EventHandler handler = MakeLabelHandler();
        auto* gs = m_labelGroup->GetActiveGroup();
        if (gs)
        {
            constraints.Width(CssPx(gs->labelWidth));
            if (gs->resizable)
                handler = MakeGroupLabelHandler(guiStateManager, elementStorage,
                    { &gs->labelWidth, gs->minWidth, gs->maxWidth,
                      &gs->edgeHovered, &gs->edgeDragging,
                      &gs->labelTextIDs, textSystem,
                      &styleManager, theme.groupLabelStyleRef });
        }
        const auto ref = RegisterElement(tachyon::Element{ .id = id, .eventHandler = std::move(handler),
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
        tachyon::EventHandler handler;
        auto* gs = m_labelGroup->GetActiveGroup();
        if (gs)
        {
            constraints.Width(CssPx(gs->labelWidth));
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
        auto ref = RegisterElement(tachyon::Element{ .id = id, .eventHandler = std::move(handler), .styleRef = style, .flags = ElementFlags::IsLabel }, text,
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
        auto ref = RegisterElement(tachyon::Element{ .id = id, .styleRef = style }, text, std::move(constraints));
    }

    void GUIApplication::SelectableText(ID id, const std::span<ContentRun>& runs,
                                        std::function<void(intptr_t)> onLinkActivate,
                                        StyleRef style, LayoutOverride&& constraints)
    {
        // Per-element press position, so a press+release without movement reads as a click
        // (activate the link under it) while a drag is a text selection.
        struct PressState { int2 pressPos {}; };

        tachyon::EventHandler handler =
            [this, id, onLinkActivate = std::move(onLinkActivate)](tachyon::ElementEvent&& ev) -> bool
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
                case tachyon::EventType::MouseMove:
                    if (textID != TextID::Invalid)
                        textSystem->SetHoveredContent(textID, textSystem->HitTestContent(textID, localPos.x, localPos.y));
                    break;
                case tachyon::EventType::MouseDown:
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
                case tachyon::EventType::MouseUp:
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
                case tachyon::EventType::KeyDown:
                case tachyon::EventType::KeyUp:
                case tachyon::EventType::TextInput:
                    // Takes the selection keys (Ctrl+C, Ctrl+A, arrows) while focused.
                    if (guiStateManager.Interaction().GetFocusedID() == id && !ev.state.consumed)
                        return ev.type != tachyon::EventType::TextInput && TextEditorUsesKey(ev.state, /*readOnly*/ true);
                    break;
                default:
                    break;
            }
            return false;
        };

        bool modified = false;
        RegisterControl(tachyon::Element{ .id = id, .eventHandler = std::move(handler), .styleRef = style },
            [this, &modified, &runs](tachyon::Element& element)
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
        tachyon::Element el{ .id = id, .styleRef = style };
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
        Image(id, image, style, tachyon::LayoutOverride::FixedSize(Pixels{ w }, Pixels{ h }));
    }

    bool GUIApplication::Button(ID id, const GUIContent& content, std::function<void()> onClick, StyleRef style, LayoutOverride&& constraints)
    {
        const IconID           icon = content.icon;
        const std::string_view text = content.text;
        ShowContentTooltip(id, content);
        auto* gs = m_buttonGroup->GetActiveButton();
        if (gs) style = m_buttonGroup->NextItemStyleRef(*gs);
        auto ref = RegisterControl(
            tachyon::Element{ .id = id, .eventHandler = MakeButtonHandler(onClick), .styleRef = style },
            [this, icon, text](tachyon::Element& element) {
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

    namespace
    {
        // A text field with a suggestion list: Up / Down move through it, and Enter and Escape
        // go to the list first (when they return true they're used); other keys edit the text.
        EventHandler MakeSuggestionFieldHandler(GUIStateManager& guiStateManager, std::function<void(int)> move,
                                                std::function<bool()> enter, std::function<bool()> escape,
                                                std::function<void()> onPress = {})
        {
            return [&guiStateManager, textBox = MakeTextBoxHandler(guiStateManager),
                    move = std::move(move), enter = std::move(enter), escape = std::move(escape),
                    onPress = std::move(onPress)](ElementEvent&& event) -> bool
            {
                if (event.type == EventType::MouseDown && event.state.LeftPressed() && onPress)
                    onPress();
                if (event.type == EventType::KeyDown && !event.state.consumed
                    && guiStateManager.Interaction().GetFocusedID() == event.id)
                {
                    switch (event.state.lastKey)
                    {
                        case KeyCode::Down:    move(+1); return true;
                        case KeyCode::Up:      move(-1); return true;
                        case KeyCode::Return:
                        case KeyCode::Return2: if (enter()) return true; break;
                        case KeyCode::Escape:  if (escape()) return true; break;
                        default: break;
                    }
                }
                return textBox(std::move(event));
            };
        }

        bool ContainsIgnoringCase(std::string_view text, std::string_view part)
        {
            const auto sameLetter = [](char a, char b)
                { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); };
            return std::search(text.begin(), text.end(), part.begin(), part.end(), sameLetter) != text.end();
        }
    }

    void GUIApplication::SuggestionPopup(ID windowID, ID anchorID, std::span<const SuggestionRow> rows, int current,
                                         bool reserveIcon, int32_t minWidth, std::function<void(int)> onPick)
    {
        if (rows.empty())
            return;
        auto position = LayoutOverride{}.Anchored(anchorID, AnchorPlacement::OutsideBottom | AnchorPlacement::InsideLeft,
                                                  { 0, int16_t(styleManager.Px(4)) }, AutoFlipMode::FlipAvoidTarget);
        if (minWidth > 0)
            position.MinWidth(Pixels{ minWidth });
        const ElementFlags flags = ElementFlags::WindowNoCaption | ElementFlags::WindowIsPopup
                                 | ElementFlags::Shadows | ElementFlags::WindowNotFocusable;
        if (BeginWindow(windowID, theme.commandSearchListStyleRef, std::move(position), flags))
        {
            const ID rowsID = idManager.AllocateRangedID(uint32_t(kMaxSuggestions), "rows");
            for (int i = 0; i < int(rows.size()) && i < kMaxSuggestions; ++i)
            {
                const SuggestionRow& row = rows[size_t(i)];
                MenuItemRow(idManager.GetSubID(rowsID, uint32_t(i)),
                            tachyon::MakeButtonHandler(guiStateManager, [onPick, i] { onPick(i); }),
                            i == current ? theme.commandSearchCurrentRowStyleRef : theme.menuItemRowStyleRef,
                            reserveIcon, row.icon, row.text, row.trailing, theme.menuItemShortcutStyleRef, LayoutOverride{});
            }
        }
        EndWindow();
    }

    bool GUIApplication::AutocompleteField(ID id, std::string& value, std::span<const std::string_view> suggestions,
                                           LayoutOverride&& constraints)
    {
        AutocompleteState& state = m_autocomplete[id];
        if (state.listWindowID == ID::Invalid())
            state.listWindowID = idManager.CreateStaticID();
        const bool picked = std::exchange(state.picked, false);

        // The suggestions that contain the text, once something is typed; none when it's all
        // there is.
        const bool focused = guiStateManager.Interaction().GetFocusedID() == id;
        std::vector<int> matches;
        if (focused && !state.dismissed && !value.empty())
        {
            for (int i = 0; i < int(suggestions.size()) && int(matches.size()) < kMaxSuggestions; ++i)
                if (ContainsIgnoringCase(suggestions[size_t(i)], value))
                    matches.push_back(i);
            if (matches.size() == 1 && suggestions[size_t(matches[0])] == value)
                matches.clear();
        }
        const int count = int(matches.size());
        state.current = std::clamp(state.current, -1, count - 1);

        // These run from event handlers, after the caller's suggestions may be gone: copies.
        std::vector<std::string> matchTexts;
        matchTexts.reserve(matches.size());
        for (const int i : matches)
            matchTexts.emplace_back(suggestions[size_t(i)]);
        auto pick = [&state, &value, matchTexts](int row)
        {
            if (row < 0 || row >= int(matchTexts.size()))
                return;
            value         = matchTexts[size_t(row)];
            state.picked  = true;
            state.dismissed = true;
            state.current = -1;
        };
        auto move = [&state, count](int step)
        {
            if (count == 0) return;
            // -1 (nothing) sits between the last row and the first.
            state.current = (state.current + 1 + step + (count + 1)) % (count + 1) - 1;
        };
        auto enter  = [&state, pick, count] { if (count == 0 || state.current < 0) return false; pick(state.current); return true; };
        auto escape = [&state, count] { if (count == 0) return false; state.dismissed = true; state.current = -1; return true; };
        auto open   = [&state] { state.dismissed = false; };
        if (count > 0)
            CloseOnPressOutside(id, state.listWindowID, [&state] { state.dismissed = true; state.current = -1; });

        // The text system also reports a commit that didn't change the text (an arrow key in a
        // one-line field), so compare instead.
        const std::string before = value;
        bool modified = false;
        const ElementRef fieldRef = RegisterControl(
            tachyon::Element{ .id = id, .eventHandler = MakeSuggestionFieldHandler(guiStateManager, move, enter, escape, open),
                              .styleRef = theme.controls->textbox, .flags = ElementFlags::DoNotMeasureText },
            [this, &modified, &value](tachyon::Element& element)
            {
                return textSystem->RegisterEditableText(element.id, value, TextEditBehavior::ContinuousUpdate,
                                                        element.styleRef, element.state, modified);
            },
            std::move(constraints));
        const bool typed = modified && value != before;
        if (typed)
        {
            state.dismissed = false;
            state.current   = -1;
        }
        if (fieldRef.IsValid())
            [[maybe_unused]] auto added = elementStorage.AddPostLayout(fieldRef,
                [&state](PostLayoutContext& ctx) { state.width = ctx.ElementRect().w; });

        [[maybe_unused]] const bool pushed = idManager.PushParent(id);
        std::vector<SuggestionRow> rows;
        rows.reserve(matches.size());
        for (const int i : matches)
            rows.push_back({ .text = suggestions[size_t(i)] });
        SuggestionPopup(state.listWindowID, id, rows, state.current, /*reserveIcon*/ false, state.width, pick);
        if (pushed)
            (void)idManager.PopParent();

        guiStateManager.SetModified(typed || picked);
        return typed || picked;
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
        const auto& commands = commandManager.GetCommands();
        const CommandMode mode = commandManager.GetMode();
        std::vector<CommandID> matches;
        if (focused)
        {
            for (size_t i = 0; i < commands.size() && int(matches.size()) < kMaxSuggestions; ++i)
            {
                const CommandManager::Command& c = commands[i];
                if (c.description.empty() || !c.callback || c.IsHeld())
                    continue;
                if (c.mode != CommandMode::Default && c.mode != mode)
                    continue;
                if (ContainsIgnoringCase(c.description, state.query))
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
        // A press outside the box and the list leaves the search, also one on what takes no
        // focus (a panel's background), which would leave the field focused.
        if (focused)
            CloseOnPressOutside(id, state.listWindowID, leave);
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
        tachyon::Element box{};
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

        RegisterElement(tachyon::Element{ .id = idManager.AllocateDynamicID("icon"), .iconID = theme.searchIcon,
                                             .styleRef = theme.searchTextboxIconStyleRef, .flags = ElementFlags::NoInput },
                        LayoutOverride{});

        // Up / Down / Enter / Escape drive the list; every other key edits the query.
        const CommandID currentCommand = matchCount > 0 ? matches[size_t(state.current)] : CommandID::Invalid();
        auto fieldHandler = MakeSuggestionFieldHandler(guiStateManager,
            [&state, matchCount](int step) { if (matchCount > 0) state.current = (state.current + step + matchCount) % matchCount; },
            [run, currentCommand] { run(currentCommand); return true; },
            [leave] { leave(); return true; });
        bool modified = false;
        RegisterControl(tachyon::Element{ .id = fieldID, .eventHandler = std::move(fieldHandler),
                                             .styleRef = theme.searchTextboxFieldStyleRef, .flags = ElementFlags::DoNotMeasureText },
            [this, &modified, &state](tachyon::Element& element)
            {
                return textSystem->RegisterEditableText(element.id, state.query, TextEditBehavior::ContinuousUpdate,
                                                        element.styleRef, element.state, modified);
            },
            LayoutOverride{}.FlexGrow(1));
        if (modified)
            state.current = 0;

        if (state.query.empty() && !state.hint.empty())
            RegisterElement(tachyon::Element{ .id = idManager.AllocateDynamicID("hint"), .styleRef = theme.commandSearchHintStyleRef,
                                                 .flags = ElementFlags::NoInput },
                            state.hint, LayoutOverride{}.Anchored(fieldID, AnchorPlacement::InsideLeft | AnchorPlacement::CenterVert));
        if (!state.shortcut.empty())
            RegisterElement(tachyon::Element{ .id = idManager.AllocateDynamicID("key"), .styleRef = theme.commandSearchKeyStyleRef,
                                                 .flags = ElementFlags::NoInput },
                            state.shortcut, LayoutOverride{});

        VERIFY(elementStorage.EndChildren());

        // The matches, in a popup under the box that leaves the keyboard with the field.
        std::vector<SuggestionRow> rows;
        rows.reserve(matches.size());
        for (const CommandID command : matches)
        {
            const CommandManager::Command& c = *commandManager.GetCommand(command);
            rows.push_back({ c.icon, c.description, c.HasShortcut() ? std::string_view(c.displayString) : std::string_view{} });
        }
        SuggestionPopup(state.listWindowID, id, rows, state.current, /*reserveIcon*/ true, /*minWidth*/ 0,
                        [run, matches](int row) { run(matches[size_t(row)]); });

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
        const tachyon::ElementState state = guiStateManager.GetCurrentState(id);
        tachyon::Element control{};
        control.id           = id;
        control.state        = state;
        control.styleRef     = theme.checkboxStyleRef;
        control.eventHandler = tachyon::MakeButtonHandler(guiStateManager, [value, onToggle]() { onToggle(!value); });
        RegisterControl(std::move(control), std::move(constraints));
        VERIFY(elementStorage.BeginChildren());
        {
            const tachyon::ElementState partState = value ? (state | tachyon::ElementState::Selected) : state;
            tachyon::Element box{};
            box.id       = idManager.AllocateDynamicID("checkboxBox");
            box.state    = partState;
            box.styleRef = theme.checkboxBoxStyleRef;
            box.flags    = tachyon::ElementFlags::NoInput;
            box.iconID   = value ? theme.menuCheckIcon : IconID::Invalid;
            RegisterElement(std::move(box), LayoutOverride{});
            if (!text.empty())
            {
                tachyon::Element label{};
                label.id       = idManager.AllocateDynamicID("checkboxLabel");
                label.state    = partState;
                label.styleRef = theme.checkboxLabelStyleRef;
                label.flags    = tachyon::ElementFlags::NoInput;
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
            tachyon::Element spacer{};
            spacer.id       = idManager.AllocateDynamicID("switchSpacer");
            spacer.styleRef = theme.emptyHorzStyleRef;
            spacer.flags    = tachyon::ElementFlags::NoInput;
            RegisterElement(std::move(spacer), LayoutOverride{}.FlexGrow(1));
        }

        // Selected while on, so the track, knob and label restyle with it.
        if (value) guiStateManager.Construction().PushSelected();

        // The whole control takes the click; its parts are NoInput and share its state.
        const tachyon::ElementState state = guiStateManager.GetCurrentState(id);
        tachyon::Element control{};
        control.id           = id;
        control.state        = state;
        control.styleRef     = style;
        control.eventHandler = tachyon::MakeButtonHandler(guiStateManager, [value, onToggle]() { onToggle(!value); });
        RegisterControl(std::move(control), std::move(constraints));
        VERIFY(elementStorage.BeginChildren());
        {
            tachyon::Element track{};
            track.id       = idManager.AllocateDynamicID("switchTrack");
            track.state    = state;
            track.styleRef = theme.switchTrackStyleRef;
            track.flags    = tachyon::ElementFlags::NoInput;
            const auto trackRef = RegisterElement(std::move(track), LayoutOverride{});

            // The knob, inset evenly, at the track's left when off and its right when on.
            const LayoutStyle* knobLayout = styleManager.GetLayoutStyle(theme.switchKnobStyleRef.layoutStyleIndex);
            const int knobSize = (knobLayout && knobLayout->layoutData.width != LayoutData::kAutoSize)
                               ? int(knobLayout->layoutData.width) : styleManager.Px(12);
            // Drawn as a disc in the knob style's background color.
            const tachyon::ElementState knobState = value ? (state | tachyon::ElementState::Selected) : state;
            const VisualBoxStyle* knobBox = styleManager.GetVisualBoxStyle(theme.switchKnobStyleRef.visualBoxStyleIndex, knobState);
            const VisualColorStyleIndex knobColor = knobBox ? knobBox->backgroundColorIndex : VisualColorStyleIndex::Invalid;
            VERIFY(elementStorage.AddCustomDraw(trackRef,
                [knobColor, knobSize, value](tachyon::CustomDrawContext& ctx)
                {
                    const auto outer = ctx.ElementRect();
                    const int inset = std::max(0, (outer.h - knobSize) / 2);
                    const int x = value ? outer.x + outer.w - inset - knobSize : outer.x + inset;
                    ctx.RenderQuad({ .position = { x, outer.y + inset }, .size = { knobSize, knobSize } },
                                   knobColor, float(knobSize) * 0.5f);
                }));

            if (!text.empty())
            {
                tachyon::Element label{};
                label.id       = idManager.AllocateDynamicID("switchLabel");
                label.state    = value ? (state | tachyon::ElementState::Selected) : state;
                label.styleRef = theme.switchLabelStyleRef;
                label.flags    = tachyon::ElementFlags::NoInput;
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

    bool GUIApplication::Toggle(ID id, bool value, const GUIContent& content, std::function<void(bool)> onToggle, StyleRef style, LayoutOverride&& constraints)
    {
        const IconID           icon = content.icon;
        const std::string_view text = content.text;
        ShowContentTooltip(id, content);
	    bool currentValue = value;
        if (currentValue) guiStateManager.Construction().PushSelected();
        auto* gs = m_buttonGroup->GetActiveToggle();
        if (gs) style = m_buttonGroup->NextItemStyleRef(*gs);
        auto ref = RegisterControl(
            tachyon::Element 
            { 
                .id = id, 
                .eventHandler = tachyon::MakeButtonHandler(guiStateManager, 
                                                       [value, onToggle]() 
                                                       { 
                                                           onToggle(!value); 
                                                       }), 
                .styleRef = style 
            },
            [this, icon, text](tachyon::Element& element) {
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

    bool GUIApplication::BeginLabelGroup(ID id, CssPx defaultWidth, CssPx minWidth, CssPx maxWidth, bool resizable)
    {
        m_labelGroup->BeginGroup(id, defaultWidth.value, minWidth.value, maxWidth.value, resizable);
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
        tachyon::ID groupID = idManager.AllocateDynamicID("button_group");
        tachyon::Element wrap{};
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
        tachyon::ID groupID = idManager.AllocateDynamicID("toggle_group");
        tachyon::Element wrap{};
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
        tachyon::Element container
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
        tachyon::Element container{ .id = id, .styleRef = style, .flags = ElementFlags::IsCachedScope };
        container.state = guiStateManager.GetCurrentState(id);
        tachyon::ElementRef containerRef = RegisterElement(std::move(container), std::move(constraints));
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
        if (!m_uiCache) return;
        // Requests made during layout, now that this frame's capture is done.
        for (const ID id : std::exchange(m_cacheRebuildRequests, {}))
            InvalidateCachedScopesAbove(id);
        if (!inputManager) return;
        RefreshCachedInputFlags();

        auto invalidateScopesAbove = [this](ID id) { InvalidateCachedScopesAbove(id); };

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

    void GUIApplication::InvalidateCachedScopesAbove(ID id)
    {
        if (!m_uiCache) return;
        for (ElementRef r = elementStorage.FindElementRefByID(id);
             r != ElementRef::Invalid(); r = elementStorage.GetParentRef(r))
        {
            const Element* e = elementStorage.GetElementPtr(r);
            if (e && (e->flags & ElementFlags::IsCachedScope) != ElementFlags::None)
                m_uiCache->Invalidate(e->id);
        }
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
            tachyon::EventHandler inner = MakeNumericalBoxHandler<float>(guiStateManager, value, cfg);
            tachyon::EventHandler editHandler =
                [this, inner = std::move(inner)](tachyon::ElementEvent&& event) -> bool
                {
                    if (event.type == tachyon::EventType::KeyDown &&
                        (event.state.lastKey == tachyon::KeyCode::Return ||
                         event.state.lastKey == tachyon::KeyCode::Return2 ||
                         event.state.lastKey == tachyon::KeyCode::KeyPadEnter ||
                         event.state.lastKey == tachyon::KeyCode::Escape))
                    {
                        guiStateManager.Interaction().SetFocusID(tachyon::ID::Invalid());
                        guiStateManager.TextEditorFocus().Clear();
                        return true;
                    }
                    return inner ? inner(std::move(event)) : false;
                };

            bool modified = false;
            RegisterControl(
                tachyon::Element{
                    .id           = id,
                    .eventHandler = std::move(editHandler),
                    .styleRef     = theme.regularControls.textbox,
                    .flags        = ElementFlags::DoNotMeasureText
                },
                [this, &modified, &value, cfg](tachyon::Element& element)
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

            tachyon::Element track{};
            track.id           = id;
            track.state        = guiStateManager.GetCurrentState(id);
            track.styleRef     = theme.sliderFieldTrackStyleRef;
            track.eventHandler = MakeSliderHandler(guiStateManager, elementStorage, &value, trackWidth,
                                                   /*onChanged*/ nullptr, minVal, maxVal);
            const auto trackRef = RegisterControl(std::move(track),
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
                [bgStyle, fillStyle, edgeStyle, edgeWidth, trackState, &widthMap, id, t](tachyon::CustomDrawContext& ctx)
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
                    const tachyon::IntRect baseRect{ .position = { X, Y }, .size = { W, H } };

                    if (fillX < W)
                        ctx.RenderBox(baseRect,
                                      tachyon::IntRect{ .position = { X + fillX, Y }, .size = { W - fillX, H } },
                                      bgStyle, trackState);
                    if (fillX > 0)
                        ctx.RenderBox(baseRect,
                                      tachyon::IntRect{ .position = { X, Y }, .size = { fillX, H } },
                                      fillStyle, trackState);
                    // The fill ends in an edge line, kept inside the track.
                    if (fillX > 0 && edgeWidth > 0)
                    {
                        const int32_t edgeX = std::clamp(fillX - edgeWidth, int32_t(0), std::max(0, W - edgeWidth));
                        ctx.RenderBox(baseRect,
                                      tachyon::IntRect{ .position = { X + edgeX, Y }, .size = { edgeWidth, H } },
                                      edgeStyle, trackState);
                    }
                });

            // Text overlays as flex children, centered by the track's justify-content.
            // NoInput so clicks pass through to the track's slider handler.
            VERIFY(elementStorage.BeginChildren());
            if (!text.empty())
            {
                const tachyon::ID lblID = idManager.AllocateDynamicID("slider_field_label");
                const tachyon::ElementState lblState = guiStateManager.GetCurrentState(lblID);
                RegisterElement(
                    tachyon::Element {
                        .id       = lblID,
                        .textID   = textSystem->RegisterText(lblID, text, theme.sliderFieldTextStyleRef, lblState),
                        .styleRef = theme.sliderFieldTextStyleRef,
                        .flags    = tachyon::ElementFlags::NoInput,
                        .state    = lblState
                    },
                    LayoutOverride::Auto());
            }
            {
                const tachyon::ID valID = idManager.AllocateDynamicID("slider_field_value");
                const tachyon::ElementState valState = guiStateManager.GetCurrentState(valID);
                char valueBuffer[64]; // fits any float with 3 decimals
                const auto formatted = std::format_to_n(valueBuffer, sizeof(valueBuffer), "{:.3f}", value);
                const std::string_view valueText(valueBuffer, size_t(formatted.out - valueBuffer));
                RegisterElement(
                    tachyon::Element {
                        .id       = valID,
                        .textID   = textSystem->RegisterText(valID, valueText, theme.sliderFieldValueStyleRef, valState),
                        .styleRef = theme.sliderFieldValueStyleRef,
                        .flags    = tachyon::ElementFlags::NoInput,
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
        tachyon::EventHandler MakeIntSliderDrag(GUIStateManager& gsm, const ElementStorage& es,
                                            int* valuePtr, int minVal, int maxVal, int trackWidth)
        {
            const int range = maxVal - minVal;
            return [&gsm, &es, valuePtr, minVal, maxVal, range, trackWidth](tachyon::ElementEvent&& event) -> bool
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
                    case tachyon::EventType::MouseDown:
                        if (event.state.LeftPressed())
                        {
                            gsm.Interaction().SetFocusID(event.id);
                            gsm.TextEditorFocus().Clear();
                            store.GetOrCreate<SliderState>(event.id).draggingID = event.id;
                            setFromMouse();
                            return true;
                        }
                        break;
                    case tachyon::EventType::MouseMove:
                        if (auto* s = store.Get<SliderState>(event.id); s && s->draggingID == event.id)
                        { setFromMouse(); return true; }
                        break;
                    case tachyon::EventType::MouseUp:
                        if (auto* s = store.Get<SliderState>(event.id); s && s->draggingID == event.id)
                        { s->draggingID = tachyon::ID::Invalid(); return true; }
                        break;
                    case tachyon::EventType::KeyDown:
                    {
                        // Arrows step 1, Page Up / Down a tenth of the range, Home / End to its ends.
                        if (gsm.Interaction().GetFocusedID() != event.id || event.state.consumed)
                            break;
                        const int bigStep = std::max(1, range / 10);
                        int step = 0;
                        switch (event.state.lastKey)
                        {
                            case tachyon::KeyCode::Left:  case tachyon::KeyCode::Down:     step = -1;       break;
                            case tachyon::KeyCode::Right: case tachyon::KeyCode::Up:       step = +1;       break;
                            case tachyon::KeyCode::PageDown:                               step = -bigStep; break;
                            case tachyon::KeyCode::PageUp:                                 step = +bigStep; break;
                            case tachyon::KeyCode::Home: *valuePtr = minVal; return true;
                            case tachyon::KeyCode::End:  *valuePtr = maxVal; return true;
                            default: break;
                        }
                        if (step == 0)
                            break;
                        *valuePtr = std::clamp(*valuePtr + step, minVal, maxVal);
                        return true;
                    }
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
        tachyon::Element track{};
        track.id       = id;
        track.state    = guiStateManager.GetCurrentState(id);
        track.styleRef = theme.sliderFieldTrackStyleRef;
        track.eventHandler = MakeIntSliderDrag(guiStateManager, elementStorage,
                                               valuePtr, minVal, maxVal, trackWidth);
        const auto trackRef = RegisterControl(std::move(track), std::move(constraints).FlexGrow(1));

        const auto bgStyle    = theme.sliderFieldBgStyleRef.visualBoxStyleIndex;
        const auto fillStyle  = theme.sliderFieldFillStyleRef.visualBoxStyleIndex;
        const auto trackState = track.state;
        auto& widthMap        = m_sliderFieldTrack->Widths();
        [[maybe_unused]] auto cdRes = elementStorage.AddCustomDraw(trackRef,
            [bgStyle, fillStyle, trackState, &widthMap, id, t](tachyon::CustomDrawContext& ctx)
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
                const tachyon::IntRect baseRect{ .position = { X, Y }, .size = { W, H } };
                if (fillX < W)
                    ctx.RenderBox(baseRect,
                                  tachyon::IntRect{ .position = { X + fillX, Y }, .size = { W - fillX, H } },
                                  bgStyle, trackState);
                if (fillX > 0)
                    ctx.RenderBox(baseRect,
                                  tachyon::IntRect{ .position = { X, Y }, .size = { fillX, H } },
                                  fillStyle, trackState);
            });

        VERIFY(elementStorage.BeginChildren());
        if (!text.empty())
        {
            const tachyon::ID lblID = idManager.AllocateDynamicID("slider_field_label");
            const tachyon::ElementState lblState = guiStateManager.GetCurrentState(lblID);
            RegisterElement(
                tachyon::Element {
                    .id       = lblID,
                    .textID   = textSystem->RegisterText(lblID, text, theme.sliderFieldTextStyleRef, lblState),
                    .styleRef = theme.sliderFieldTextStyleRef,
                    .flags    = tachyon::ElementFlags::NoInput,
                    .state    = lblState
                },
                LayoutOverride::Auto());
        }
        {
            const tachyon::ID valID = idManager.AllocateDynamicID("slider_field_value");
            const tachyon::ElementState valState = guiStateManager.GetCurrentState(valID);
            const std::string valueText = std::format("{}", value);
            RegisterElement(
                tachyon::Element {
                    .id       = valID,
                    .textID   = textSystem->RegisterText(valID, valueText, theme.sliderFieldTextStyleRef, valState),
                    .styleRef = theme.sliderFieldTextStyleRef,
                    .flags    = tachyon::ElementFlags::NoInput,
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

        tachyon::Element track{};
        track.id           = id;
        track.state        = guiStateManager.GetCurrentState(id);
        track.styleRef     = theme.sliderStyleRef;
        track.eventHandler = MakeIntSliderDrag(guiStateManager, elementStorage,
                                               &value, minVal, maxVal, trackWidth);
        const auto trackRef = RegisterControl(std::move(track), std::move(constraints));

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
            [grooveStyle, fillStyle, knobStyle, trackState, &widthMap, id, t, grooveH, knobSz](tachyon::CustomDrawContext& ctx)
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
                ctx.RenderBox(tachyon::IntRect{ .position = { X, (Hc - grooveH) / 2 },
                                            .size     = { W, grooveH } },
                              grooveStyle, trackState);
                ctx.RenderBox(tachyon::IntRect{ .position = { X, (Hc - grooveH) / 2 },
                                            .size     = { knobX + knobSz / 2 - X, grooveH } },
                              fillStyle, trackState);

                // Knob: kept inside the track.
                ctx.RenderBox(tachyon::IntRect{ .position = { knobX, (Hc - knobSz) / 2 },
                                            .size     = { knobSz, knobSz } },
                              knobStyle, trackState);
            });

        return value != before;
    }
}