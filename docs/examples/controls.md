# Controls

The common controls in one window, laid out as label/value rows.
Full source: [examples/controls/controls.cpp](https://github.com/LogicalError/TachyonUI/blob/main/examples/controls/controls.cpp).

## A window as a scope

Instead of `BeginWindow` / `EndWindow`, this example uses the scope form. The scope ends
the window when it leaves, so an early `return` is safe:

```cpp
// The scope ends the window when it leaves, open or not.
auto window = Window(windowID, "Controls", tachyon::LayoutOverride{}.Size(480, 560));
if (!window)
    return;
BeginVertical(tachyon::LayoutOverride{}.FlexGrow(1));
```

The container pairs have scopes like this, taking the same arguments as their `Begin*`:
`Menu`, `Horizontal`, `Table`, `Tooltip` and more ([Begin and End](../overview/begin-end.md)).

## Label/value rows

A label group lines each `Label` up with the control after it, in a label column the
user can resize. `DragLabel` does the same, and dragging it changes the value of the
field it labels:

```cpp
BeginLabelGroup(140);

Label("Button");
Button("Click me", [this] { ++clicks; });

Label("Clicks");
Text(std::to_string(clicks));

Label("Checkbox");
Field(enabled, enabled ? "On" : "Off");
```

## Toggles in a row

```cpp
Label("Toggles");
BeginHorizontal();
Toggle(bold, "B");
Toggle(italic, "I");
Toggle(underline, "U");
EndHorizontal();
```

## Text and numbers

`Field` picks its editor from the value's type. Text fields take behavior flags, and
number fields an optional range:

```cpp
Label("Text");
Field(text);

Label("Multiline");
Field(notes, tachyon::TextEditBehavior::ContinuousUpdate | tachyon::TextEditBehavior::Multiline);

DragLabel("Float");
Field(floatValue);

DragLabel("Int [0, 10]");
Field(intValue, tachyon::NumericFieldConfig<int>{ .minVal = 0, .maxVal = 10 });

DragLabel("Vector");
Field(position);
```

## Slider and color

```cpp
Label("Slider");
SliderField(volume, 0.0f, 100.0f, "Volume");

Label("Color");
ColorField(color);
```

## A menu

A menu is open while `BeginMenu` returns true; emit its items then. `EndMenu` is called
either way. An item's callback runs when it is chosen:

```cpp
Label("Menu");
if (BeginMenu(menuID, "Choose...", tachyon::LayoutOverride{}.NoGrow().NoShrink()))
{
    for (const char* item : { "Apple", "Banana", "Cherry" })
        MenuItem(item, [this, item] { choice = item; });
}
EndMenu();
```

## Tabs

A tab bar only tracks which tab is selected; what to show for it is up to the app:

```cpp
BeginTabBar(idManager.AllocateDynamicID("tabs"), &selectedTab, nullptr);
Tab("First");
Tab("Second");
EndTabBar();
Text(selectedTab == 0 ? "Content of the first tab" : "Content of the second tab");
```

Every control here has its own page in the [Controls](../controls.md) guide, with all
its overloads and its default CSS class.

---

[← Hello world](hello-world.md) · [Examples](../examples.md) · [Docking →](docking.md)
