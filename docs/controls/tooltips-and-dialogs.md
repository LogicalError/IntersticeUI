# Tooltips and message dialogs

## Tooltips

```cpp
void SetTooltip(ID targetID, std::string_view text);
void SetTooltip(ID targetID, IconID icon, std::string_view text);
void SetTooltip(std::string_view text);           // the control just drawn
bool BeginTooltip(ID targetID);
bool BeginTooltip();                              // the control just drawn
void EndTooltip();
```

`SetTooltip` shows a text (and icon) on hover, or right away when Tab moves the focus to
the target. `BeginTooltip` builds any content; it returns `true` while the tooltip shows,
and `EndTooltip` is called either way. A `GUIContent` with a tooltip sets it too. Classes:
`tooltip`, `tooltip-text`.

Without a target, the tooltip goes on the control just drawn: its outer element, whatever
parts it drew after it (a checkbox's label, a slider's value), and not a popup it opened. A
control made of several (a color field's swatch, hex and alpha fields, a breadcrumb's
segments) takes their states, so its tooltip shows over each of them. When the hovered
element and a control around it both have one, the first declared shows: a control's,
set right after it, over its row's.

```cpp
Button(gridIcon, [this] { showGrid = !showGrid; });
SetTooltip(showGrid ? "Grid on" : "Grid off");
```


## Message dialogs

### MessageDialog

A modal question with verb buttons, centered. Call it every frame while
asking; it returns the index of the chosen button once, in the frame after, else -1.
Esc and the close button choose `cancelButton`; Alt and a button's first letter choose
that button, wherever the focus is in the dialog. The default button has the focus first;
Tab goes through all the dialog's controls (the close button, the checkboxes, Details) and
the focus stays in the dialog.

```cpp
int MessageDialog(ID id, const MessageDialogDesc& desc, StyleRef style);
```

`MessageDialogDesc` fields: `icon` (`MessageIcon::Warning`, `Quit`, `Question`, `Info`,
`Error`), `title` (a question), `message`, `buttons` (a span of `MessageButton`:
`label`, `kind` = `Secondary`, `Primary` or `Danger`, `alignLeft`), `defaultButton`,
`cancelButton` (-1: the default one), `items` (affected items with an optional color dot,
note and checkbox), `dontAskAgain` (a `bool*`; shows the checkbox when set) and
`details` (collapsible technical text).

Classes: `message-box` and the `message-box-*` classes; the buttons use
`dialog-button`, `dialog-button-primary`, `dialog-button-danger`.

```cpp
static constexpr MessageButton kButtons[] = {
    { .label = "Cancel" },
    { .label = "Quit", .kind = MessageButtonKind::Danger },
};
const int choice = MessageDialog(quitDialogID, MessageDialogDesc{
    .icon = MessageIcon::Quit, .title = "Quit?", .message = "Unsaved edits will be lost.",
    .buttons = kButtons, .defaultButton = 0, .cancelButton = 0 });
if (choice == 1) applicationQuit = true;
if (choice >= 0) asking = false;
```

The color picker and gradient editor are the library's other dialogs; they open from
`ColorField` and `GradientField`.

---

[← Menus](menus.md) · [Controls](../controls.md) · [Lists and reorderable groups →](lists.md)
