#include <gui_controls/gui_application.h>
#include <gui_controls/controls_color_internal.h>   // SetColorStyle

#include <algorithm>
#include <cctype>
#include <utility>

// MessageDialog: a modal question with verb buttons (see MessageDialogDesc).

namespace interstice
{
    namespace
    {
        struct MessageDialogState
        {
            int  choice      = -1;      // set by a button (or Esc / the close button), returned next frame
            bool detailsOpen = false;
        };

        // The letter of a key, for Alt mnemonics; 0 for other keys.
        char KeyLetter(KeyCode key)
        {
            const int k = int(key) - int(KeyCode::KeyA);
            return (k >= 0 && k <= int(KeyCode::KeyZ) - int(KeyCode::KeyA)) ? char('a' + k) : 0;
        }
    }

    int GUIApplication::MessageDialog(ID id, const MessageDialogDesc& desc)
    {
        auto& store = guiStateManager.GetElementDataStore();
        auto& state = store.GetOrCreate<MessageDialogState>(id);
        if (state.choice >= 0)
        {
            const int choice = state.choice;
            state.choice = -1;
            return choice;
        }

        const int count = int(desc.buttons.size());
        if (count == 0)
            return -1;
        const int defaultButton = std::clamp(desc.defaultButton, 0, count - 1);
        const int cancelButton  = desc.cancelButton >= 0 && desc.cancelButton < count ? desc.cancelButton : defaultButton;
        auto choose = [this, id](int index) { guiStateManager.GetElementDataStore().GetOrCreate<MessageDialogState>(id).choice = index; };

        // A modal dialog dims the window it's asked from (DrawScrims), and sits centered over
        // it (out of its layout), by its size last frame.
        const ID ownerID = windowRegistry.GetCurrentWindowID();
        m_scrims[ownerID].wanted = true;
        const IntRect ownerRect = windowManager->GetWindowRect(ownerID);
        IntRect self = windowManager->GetWindowRect(id);
        if (self.w <= 0 || self.h <= 0) self.size = { styleManager.Px(456), styleManager.Px(160) };
        const int2 at = ownerRect.position + int2{ (ownerRect.w - self.w) / 2, (ownerRect.h - self.h) / 2 };

        if (!BeginWindow(id, desc.title, theme.messageBoxStyleRef, LayoutOverride{}.Position(at),
                         ElementFlags::WindowNoCaption | ElementFlags::WindowIsModal | ElementFlags::Shadows))
        {
            EndWindow();
            return cancelButton;   // closed from outside: as cancelled
        }

        [[maybe_unused]] const bool pushed = idManager.PushParent(id);
        const ID buttonRange = idManager.AllocateRangedID(uint32_t(count), "message_box_button");
        auto buttonID = [&](int i) { return idManager.GetSubID(buttonRange, uint32_t(i)); };

        // The keyboard stays on the buttons, starting on the default one, which shows the focus ring.
        const ID focused = guiStateManager.Interaction().GetFocusedID();
        bool onAButton = false;
        for (int i = 0; i < count; ++i) onAButton |= focused == buttonID(i);
        if (!onAButton)
        {
            guiStateManager.Interaction().SetFocusID(buttonID(defaultButton));
            guiStateManager.Interaction().SetFocusVisible(true);
        }

        // The close button, top right: as Cancel.
        Button(idManager.AllocateDynamicID("message_box_close"), theme.tabCloseIcon, [choose, cancelButton] { choose(cancelButton); },
               theme.messageBoxCloseStyleRef,
               LayoutOverride{}.Absolute(styleManager.Px(8), styleManager.Px(8), PositionConstraintAxis::kAuto, PositionConstraintAxis::kAuto));

        // Severity icon | title, message and extras.
        BeginHorizontal(idManager.AllocateDynamicID("message_box_body"), theme.messageBoxBodyStyleRef, LayoutOverride{}.NoShrink());
        {
            static constexpr struct { StyleRef GUITheme::* style; IconID GUITheme::* icon; } kIcons[] = {
                { &GUITheme::messageBoxWarningStyleRef,  &GUITheme::alertIcon    },   // Warning
                { &GUITheme::messageBoxWarningStyleRef,  &GUITheme::powerIcon    },   // Quit
                { &GUITheme::messageBoxQuestionStyleRef, &GUITheme::questionIcon },   // Question
                { &GUITheme::messageBoxQuestionStyleRef, &GUITheme::infoIcon     },   // Info
                { &GUITheme::messageBoxErrorStyleRef,    &GUITheme::errorIcon    },   // Error
            };
            const auto& kind = kIcons[std::min<size_t>(size_t(desc.icon), std::size(kIcons) - 1)];
            RegisterElement(Element{ .id = idManager.AllocateDynamicID("message_box_icon"), .iconID = theme.*kind.icon,
                                     .styleRef = theme.*kind.style, .flags = ElementFlags::NoInput }, LayoutOverride{});

            BeginVertical(idManager.AllocateDynamicID("message_box_text"), theme.messageBoxTextStyleRef, LayoutOverride{}.FlexGrow(1));
            Text(idManager.AllocateDynamicID("message_box_title"), desc.title, theme.messageBoxTitleStyleRef);
            Text(idManager.AllocateDynamicID("message_box_message"), desc.message, theme.messageBoxMessageStyleRef);

            // Affected items, each to tick or untick.
            if (!desc.items.empty())
            {
                BeginVertical(idManager.AllocateDynamicID("message_box_items"), theme.messageBoxWellStyleRef, LayoutOverride{}.NoShrink());
                const ID itemRange = idManager.AllocateRangedID(uint32_t(desc.items.size()), "message_box_item");
                for (size_t i = 0; i < desc.items.size(); ++i)
                {
                    const MessageDialogItem& item = desc.items[i];
                    [[maybe_unused]] const bool itemScope = idManager.PushParent(idManager.GetSubID(itemRange, uint32_t(i)));
                    BeginHorizontal(idManager.AllocateDynamicID("row"), theme.messageBoxItemStyleRef, LayoutOverride{}.NoShrink());
                    if (item.checked)
                        Checkbox(idManager.AllocateDynamicID("check"), *item.checked, {}, [checked = item.checked](bool on) { *checked = on; });
                    if (item.color.a > 0.0f)
                    {
                        const ElementRef dot = RegisterElement(Element{ .id = idManager.AllocateDynamicID("dot"), .styleRef = theme.messageBoxItemDotStyleRef,
                                                                        .flags = ElementFlags::NoInput }, LayoutOverride{});
                        auto& dotStyle = m_colorPickerSub->ClaimSwatchStyle(idManager.GetSubID(itemRange, uint32_t(i)));
                        NoteSwatchClaim(idManager.GetSubID(itemRange, uint32_t(i)));
                        SetColorStyle(styleManager, dotStyle, item.color);
                        VERIFY(elementStorage.AddCustomDraw(dot, [color = dotStyle, radius = float(styleManager.Px(2))](CustomDrawContext& ctx)
                            { ctx.RenderQuad(ctx.ElementRect(), color, radius); }));
                    }
                    Text(idManager.AllocateDynamicID("name"), item.name, theme.messageBoxItemNameStyleRef, LayoutOverride{}.FlexGrow(1));
                    if (!item.meta.empty())
                        Text(idManager.AllocateDynamicID("meta"), item.meta, theme.messageBoxItemMetaStyleRef);
                    EndHorizontal();
                    if (itemScope) (void)idManager.PopParent();
                }
                EndVertical();
            }

            if (desc.dontAskAgain)
                Checkbox(idManager.AllocateDynamicID("message_box_dont_ask"), *desc.dontAskAgain, "Don't ask again",
                         [flag = desc.dontAskAgain](bool on) { *flag = on; }, LayoutOverride{}.NoGrow());

            // Collapsible technical details.
            if (!desc.details.empty())
            {
                Button(idManager.AllocateDynamicID("message_box_details_toggle"),
                       { state.detailsOpen ? theme.dropdownArrowIcon : theme.chevronRightIcon, "Details" },
                       [this, id] { auto& s = guiStateManager.GetElementDataStore().GetOrCreate<MessageDialogState>(id); s.detailsOpen = !s.detailsOpen; },
                       theme.messageBoxDetailsToggleStyleRef, LayoutOverride{}.NoGrow());
                if (state.detailsOpen)
                    Text(idManager.AllocateDynamicID("message_box_details"), desc.details, theme.messageBoxDetailsStyleRef);
            }
            EndVertical();
        }
        EndHorizontal();

        // Footer: left-aligned buttons, then the rest on the right. Each button's first letter is
        // its Alt mnemonic (underlined); Esc chooses the cancel button.
        std::vector<char> mnemonics(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i)
            mnemonics[size_t(i)] = desc.buttons[size_t(i)].label.empty() ? 0
                                 : char(std::tolower(static_cast<unsigned char>(desc.buttons[size_t(i)].label[0])));
        m_mnemonicStyles.clear();
        m_mnemonicStyles.reserve(size_t(count));
        BeginHorizontal(idManager.AllocateDynamicID("message_box_footer"), theme.dialogFooterStyleRef, LayoutOverride{}.NoShrink());
        auto emitButton = [&](int i)
        {
            const MessageButton& button = desc.buttons[size_t(i)];
            const StyleRef style = button.kind == MessageButtonKind::Primary ? theme.dialogPrimaryButtonStyleRef
                                 : button.kind == MessageButtonKind::Danger  ? theme.dialogDangerButtonStyleRef
                                                                             : theme.dialogButtonStyleRef;
            const ID bid = buttonID(i);
            const ElementState bstate = guiStateManager.GetCurrentState(bid);
            const VisualTextStyle* text = styleManager.GetVisualTextStyle(style.visualTextStyleIndex, bstate);
            TextStyle& underlined = m_mnemonicStyles.emplace_back(text ? text->textStyle : TextStyle{});
            underlined.underLine.thickness = uint16_t(styleManager.Px(1));
            const std::string_view label = button.label;
            ContentRun runs[2] = {
                { .type = ContentRun::Text, .text = label.substr(0, label.empty() ? 0 : 1), .textStyleOverride = &underlined },
                { .type = ContentRun::Text, .text = label.empty() ? label : label.substr(1) },
            };
            auto click = interstice::MakeButtonHandler(guiStateManager, [choose, i] { choose(i); });
            RegisterControl(Element{ .id = bid,
                .eventHandler = [click, choose, cancelButton, mnemonics](ElementEvent&& event) -> bool
                {
                    if (event.type == EventType::KeyDown)
                    {
                        if (event.state.lastKey == KeyCode::Escape) { choose(cancelButton); return true; }
                        if ((event.state.modifiers & KeyModifier::Alt) != KeyModifier::None)
                            if (const char letter = KeyLetter(event.state.lastKey))
                                for (size_t m = 0; m < mnemonics.size(); ++m)
                                    if (mnemonics[m] == letter) { choose(int(m)); return true; }
                    }
                    return click(std::move(event));
                },
                .styleRef = style },
                [this, runs](Element& e) mutable { return textSystem->RegisterRichText(e.id, std::span<ContentRun>(runs), e.styleRef, e.state); },
                LayoutOverride{});
        };
        for (int i = 0; i < count; ++i)
            if (desc.buttons[size_t(i)].alignLeft) emitButton(i);
        Text(idManager.AllocateDynamicID("message_box_spacer"), {}, theme.dialogStatusStyleRef, LayoutOverride{}.FlexGrow(1));
        for (int i = 0; i < count; ++i)
            if (!desc.buttons[size_t(i)].alignLeft) emitButton(i);
        EndHorizontal();

        if (pushed) (void)idManager.PopParent();
        EndWindow();
        return -1;
    }

    void GUIApplication::DrawScrims(float now)
    {
        // Each dimmed window's scrim fades in (or out, after its dialog closed) over 120 ms,
        // eased out, drawn over everything in the window.
        constexpr float kFadeSecs = 0.12f;
        const float step = m_scrimTime < 0.0f ? 0.0f : std::max(0.0f, now - m_scrimTime) / kFadeSecs;
        m_scrimTime = now;
        for (auto it = m_scrims.begin(); it != m_scrims.end(); )
        {
            ScrimFade& scrim = it->second;
            const bool wanted = std::exchange(scrim.wanted, false);
            scrim.amount = std::clamp(scrim.amount + (wanted ? step : -step), 0.0f, 1.0f);
            const ElementRef owner = windowRegistry.GetWindowRef(it->first);
            if ((scrim.amount <= 0.0f && !wanted) || !elementStorage.IsValidRef(owner))
            {
                if (scrim.color != VisualColorStyleIndex::Invalid)
                    styleManager.DestroyColorStyle(scrim.color);
                it = m_scrims.erase(it);
                continue;
            }
            const float eased = 1.0f - (1.0f - scrim.amount) * (1.0f - scrim.amount) * (1.0f - scrim.amount);
            if (eased > 0.0f)
            {
                SetColorStyle(styleManager, scrim.color, color4f{ theme.scrim.r / 255.0f, theme.scrim.g / 255.0f,
                                                                  theme.scrim.b / 255.0f, theme.scrim.a / 255.0f * eased });
                VERIFY(elementStorage.AddCustomDrawAfter(owner, [color = scrim.color](CustomDrawContext& ctx)
                    { ctx.RenderQuad(ctx.ElementRect(), color); }));
            }
            ++it;
        }
    }

    bool GUIApplication::CloseRequested(ID windowID)
    {
        const bool fromTitleBar = m_closeRequests.erase(windowID) > 0;
        const bool fromOS       = windowManager->TakeCloseRequest(windowID);
        return fromTitleBar || fromOS;
    }
}
