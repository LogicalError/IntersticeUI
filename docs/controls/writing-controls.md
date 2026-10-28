# Writing a control

What every control and field must do, and what to decide for each optional feature. A
control that skips one of these behaves differently from the others, and in a UI that is
a bug: the user learns how controls work from the ones they already know. The checklist
holds for the library's own controls and for ones an application adds.

The [Common conventions](conventions.md) page describes the API shape this builds on.

## Must have

**Identity**

- [ ] Overloads with an `ID` and with the call site as the ID (`std::source_location`), and
      with and without a `StyleRef`; the ID-and-style overload does the work. See
      [Common conventions](conventions.md).
- [ ] Parts of the control (an icon, a caption, a box around it) get IDs under the control's
      own (`idManager.PushParent(id)`), so they stay the same whatever is drawn before it.
- [ ] Registered with `RegisterControl`, not `RegisterElement`: that marks it as a control,
      puts it in the Tab order and reports an invalid ID. Parts that aren't controls
      (captions, icons, tracks) are `RegisterElement` with `ElementFlags::NoInput`, so the
      mouse reaches the control.

**Keyboard**

- [ ] Reached by Tab and Shift+Tab, in the order it is drawn, unless disabled.
- [ ] Everything the mouse does can be done with the keyboard once it has the focus:
      Space and Enter activate (`IsActivationKey`), arrows step a value or move a
      highlight, Escape cancels. Use the same keys as the controls like it: a slider's
      arrows, Page Up / Down and Home / End; a list's Up / Down.
- [ ] A key it uses returns `true` from the handler, so it doesn't also reach the app's
      shortcuts; a key it doesn't use returns `false`.
- [ ] A text field takes only the keys the text editor uses (`MakeTextBoxHandler`,
      `TextEditorUsesKey`).

**Focus**

- [ ] A click (or a press that starts a drag) gives it the focus:
      `Interaction().SetFocusID(id)`, and `TextEditorFocus()` set for a text field, cleared
      otherwise.
- [ ] Shows the focus. Its CSS class has a `:focus` rule (a border in `--accent`), or is in
      the `:focus-visible` ring list in `styles.css` (`box-shadow: var(--focus-ring)`); a
      full-width row or tab uses the inset outline instead. `KeyboardFocusTest` fails for a
      Tab stop that doesn't.
- [ ] If its border is drawn by a box around it (a field with a caption or a unit), the box
      has `ElementFlags::PropagatesDescendantState` (`BeginFieldBox` in the library) and the
      `:hover`, `:focus` and `:focus-visible` rules, so it shows the field's states.

**States**

- [ ] Hover, active (pressed), focused, disabled and, where it applies, selected each have a
      style: the state comes from `guiStateManager.GetCurrentState(id)` and is set on the
      element and on every part drawn in it.
- [ ] Disabled (`BeginDisabled`): drawn dimmed, takes no input and isn't a Tab stop. The
      mouse is handled for you (a disabled element absorbs hits); a handler that acts on
      keys or callbacks must not act while disabled.
- [ ] Indeterminate (`BeginIndeterminate`), for a field showing several objects' values: it
      shows a dash and returns `false` until edited.

**Values and results**

- [ ] A value control takes a `T&` and writes it; it returns `true` on the frame the value
      changed (by the user, not on every commit), and calls
      `guiStateManager.SetModified(changed)`. A handler that writes the value runs after
      the control's call returned: wrap it in `ReportingChanges`, and count
      `Interaction().HasBeenClicked(id)` as changed in the next call.
- [ ] Callbacks run from the input handler, after the frame: they capture copies, or
      references to storage that outlives the frame, never the caller's temporaries
      (a `span` of the caller's strings, say).
- [ ] A text the user can edit has undo (the text system's editable text does it).

**Labels and tooltips**

- [ ] Works after `Label(...)`: a click on the label focuses it, and its focus shows on the
      label. `Label` binds to the first control in the element after it, so the control is
      the next element, or its first control is.
- [ ] A numeric field works after `DragLabel(...)`: dragging the label scrubs the value
      (`LabelDrag*` events).
- [ ] `SetTooltip` and `BeginContextMenu` right after it attach to it: they take the
      element last finished at the caller's level (`GetLastItemID()`), so the control's
      outer element is the one it adds there, with its parts inside. When the parts take
      input of their own (a swatch and fields), the outer element has
      `ElementFlags::PropagatesDescendantState` (`BeginFieldBox`), so the tooltip and the
      context menu work over each part. A `GUIContent` tooltip is shown with
      `ShowContentTooltip`.
- [ ] Its context menu opens from the keyboard too (Shift+F10, the Menu key), which
      `BeginContextMenu` does for the focused control.

**Popups**

A control that opens a popup (a list, a picker, a menu):

- [ ] The popup is anchored to the control (`Anchored(id, ...)`, flipping at the screen
      edge) and is at least as wide as it when it's a list for it.
- [ ] Closes on Escape, on a press anywhere outside it and the control (including the
      window's title bar) and when the focus leaves; `CloseOnPressOutside` does the press,
      `TakeEscapeFor` Escape for a picker window. Escape cancels and puts the focus back on
      the control.
- [ ] Keeps the keyboard where it belongs: in the field for a suggestion list
      (`WindowNotFocusable`), in the popup for a menu or picker.
- [ ] Tab from the control goes on to the next control, not into the popup.
- [ ] Opens on the current value (a dropdown on the selected option, scrolled into view).

**Theming and layout**

- [ ] One CSS class per part, registered in `GUITheme` and listed in
      [Default CSS classes](css-classes.md), with the dark and light palettes' variables,
      not colors. A toolbar variant where controls look different in a toolbar
      (`theme.controls->`).
- [ ] Sizes in CSS pixels (`styleManager.Px` for anything computed), so it follows the UI
      scale.
- [ ] `LayoutOverride&& constraints = {}` last, applied to the control's outer element;
      sensible flex defaults (a field grows in a row, a button doesn't).
- [ ] In a node row (`nodeRowFieldMinWidth`), a field fills the row like the others.

**Frame and cache**

- [ ] Begin / End pairs follow [Begin and End](../overview/begin-end.md): End is called
      whatever Begin returned, and a misuse is reported (`ReportMisuse`), not fatal.
- [ ] Something that changes how it is drawn without an input or a size change (an
      animation, a value set from outside, its own size learned after layout) asks for a
      redraw, or a cached panel keeps the old drawing: `Invalidate`, or
      `RebuildCachedScopesAbove` from layout.
- [ ] Nothing it needs between frames depends on the call order of other controls; per-ID
      state goes in the element data store or a map keyed by its ID.

**Documentation and tests**

- [ ] On its page under [Controls](../controls.md), with the signature, what it returns, its
      keys and its CSS classes; a CHANGELOG line.
- [ ] Headless tests for its mouse and keyboard use, its return value, and its popup if it
      has one; added to `KeyboardFocusTest`.
- [ ] A row in the showcase, so it can be tried by hand.

## Decide for each control

These don't apply to every control. Decide them deliberately, the same way as the controls
like it:

| Feature | When | How |
| --- | --- | --- |
| Drag to change | Numbers, sliders, colors | Press, a threshold (`kDragStartDistance`), then drag; Escape during the drag restores the value |
| Edit as text | A value that also has a text form (a slider's number) | Click to type; Enter commits, Escape cancels |
| Context menu | Anything with actions beyond the main one | `BeginContextMenu(id)`: right-click, Shift+F10, the Menu key |
| Move in a list | Items the user orders (rows, tabs, columns) | Drag, and Alt + an arrow on the focused item (a reorderable group does both) |
| Close | Items that close (tabs) | A "Close" context menu item, and Ctrl+W / Ctrl+F4 on the focused one |
| Drag and drop | Values that move between controls | A drag source and drop target with a payload type |
| Copy and paste | Values with a text form | Ctrl+C / Ctrl+V while focused |
| Placeholder text | Empty text fields | The hint style, shown while empty |
| Validation | Text that must be a number, a name, ... | A validator on the editable text |
| Units and limits | Numbers | `NumericFieldConfig` (`minVal`, `maxVal`, `stepPerPixel`), a unit label |
| Icon | Buttons, toggles, list items, menu items | `GUIContent`, with `-tachyon-icon-side` |
| Mixed value | Multi-selection editors | Indeterminate (above) |
| Read-only | Values shown but not edited | Focusable and selectable, not editable |
| Transition | State changes worth animating | The style's states; transitions blend between them |
