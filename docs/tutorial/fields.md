# 3. Fields

The greeting panel has one row per field. In `my_app.cpp`:

```cpp
void GreetingPanel::Draw(tachyon::GUIApplication& ui)
{
```

<!-- snippet: examples/tutorial/my_app.cpp [greeting-fields] -->
```cpp
ui.BeginLabelGroup();

ui.Label("Name");
ui.Field(name);

ui.Label("Times");
ui.Field(times, tachyon::NumericFieldConfig<int>{ .minVal = 1, .maxVal = 5 });

ui.Label("Excited");
ui.Field(excited, excited ? "Yes" : "No");

ui.EndLabelGroup();
```

- **`Field` picks its editor from the value's type:** a text field for the `std::string`,
  a number field for the `int`, a toggle for the `bool`. Each edits the value in place.
- **`NumericFieldConfig`** limits a number: `times` stays between 1 and 5.
- **A label group** lines each `Label` up with the control after it, in a label column the
  user can resize. Clicking a label focuses its field.
- **The toggle's text** is worked out each frame, so it reads "Yes" or "No" as the value
  changes.

Under the fields, the greeting they make:

<!-- snippet: examples/tutorial/my_app.cpp [greeting-text] -->
```cpp
const std::string text = "Hello, " + name + (excited ? "!" : ".");
for (int i = 0; i < times; ++i)
    ui.Text(text);
```

Typing a name changes the greeting on the next frame; nothing has to tell the text to
update. The loop draws the line `times` times: controls made in a loop get their own IDs,
even though they share a call site.

Close `GreetingPanel::Draw` with `}`.

---

[← 2. Buttons](buttons.md) · [Tutorial](../tutorial.md) · [4. The app →](app.md)
