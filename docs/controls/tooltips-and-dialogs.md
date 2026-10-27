# Tooltips and message dialogs

## Tooltips

```cpp
void SetTooltip(ID targetID, std::string_view text);
void SetTooltip(ID targetID, IconID icon, std::string_view text);
void SetTooltip(std::string_view text);           // the element registered last
bool BeginTooltip(ID targetID);
bool BeginTooltip();                              // the element registered last
void EndTooltip();
```

`SetTooltip` shows a text (and icon) on hover. `BeginTooltip` builds any content; it
returns `true` while the tooltip shows, and `EndTooltip` is called either way. A
`GUIContent` with a tooltip sets it too. Classes: `tooltip`, `tooltip-text`.

```cpp
Button(gridIcon, [this] { showGrid = !showGrid; });
SetTooltip(showGrid ? "Grid on" : "Grid off");
```


## Message dialogs

### MessageDialog

A modal question with verb buttons, centered. Call it every frame while
asking; it returns the index of the chosen button once, in the frame after, else -1.
Esc and the close button choose `cancelButton`; Alt and a button's first letter choose
that button.

```cpp
int MessageDialog(ID id, const MessageDialogDesc& desc);
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
