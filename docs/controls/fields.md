# Fields

`Field` is overloaded on the value type: text, bool, numbers, vectors, lists and
dictionaries. All return `true` when the value changed.

## Text fields

```cpp
bool Field(ID id, std::string& value, TextEditBehavior textEditBehavior, StyleRef style,
           LayoutOverride&& constraints = {});
```

`TextEditBehavior` flags combine with `|`:

| Flag | Effect |
| --- | --- |
| `ContinuousUpdate` (default) | Update `value` on every keystroke, not only on commit |
| `Multiline` | Allow newlines instead of committing on Enter |
| `AcceptTabs` | Tab inserts a tab instead of moving focus |
| `ReadOnly` | Selectable and copyable, not editable |
| `None` | Update on commit (Enter or focus loss) |

Default class: `textbox` (`toolbar-textbox` in a toolbar).

```cpp
Field(name);
Field(notes, TextEditBehavior::ContinuousUpdate | TextEditBehavior::Multiline);
```

## SearchField

A text field with a leading icon inside its border.

```cpp
bool SearchField(ID id, std::string& value, IconID icon,
                 TextEditBehavior textEditBehavior = TextEditBehavior::ContinuousUpdate,
                 LayoutOverride&& constraints = {});
```

Classes: `search-textbox`, `search-textbox-icon`, `search-textbox-field`.

## Dropdown

A field that shows the selected option and opens the list of options under itself.

```cpp
bool Dropdown(ID id, int& selected, std::span<const std::string_view> options, StyleRef style,
              LayoutOverride&& constraints = {});
bool Dropdown(int& selected, std::initializer_list<std::string_view> options,
              LayoutOverride&& constraints = {});
```

Picking an option, with a click or with the arrow keys and Enter, sets `selected` and
returns `true` on that frame; the field shows the new option from the next frame. Escape
or a click elsewhere closes the list without a change. The list opens with the keyboard
on the current option. A `selected` outside the options shows an empty field. Default
class: `menu-trigger` (popup `menu`, rows `menu-item-row`).

```cpp
static constexpr std::string_view kThemes[] = { "Dark", "Light" };
if (Dropdown(themeIndex, kThemes))
    ApplyTheme(themeIndex);

Dropdown(quality, { "Low", "Medium", "High" });
```

## Numeric fields

```cpp
template<typename T, typename ComponentT = typename TypeDimensions<T>::ComponentType>
bool Field(ID id, T& value, TextEditBehavior textEditBehavior, StyleRef style,
           NumericFieldConfig<ComponentT> config = {}, LayoutOverride&& constraints = {});
```

Shorter overloads leave out the behavior, the style or the config. For `float`,
`double` and the 8- to 64-bit signed and unsigned integers, it is an editable number
that also changes by dragging. `NumericFieldConfig` sets `minVal`, `maxVal` and
`stepPerPixel` (the change per dragged pixel). A NaN shows in a warning color. Default
class: `number-field` (`toolbar-number-field` in a toolbar).

```cpp
Field(floatValue);
Field(intValue, NumericFieldConfig<int>{ .minVal = 0, .maxVal = 10 });
```

## Vector fields

The same `Field` on a vector type shows one number per component, each after a colored
axis label that drags it: `int2`-`int4`, `uint2`-`uint4`, `short2`-`short4`,
`ushort2`-`ushort4`, `byte2`-`byte4`, `float2`-`float4` (X, Y, Z, W), and `color4b`,
`color4f` (R, G, B, A). The config applies to each component.

```cpp
DragLabel("Position");
Field(position);   // float3
```

Classes: `vector-component` for the numbers, `axis-label-x`, `axis-label-y`,
`axis-label-z`, `axis-label-w` for the labels.

## TypedField

A number field with its type after the value, e.g. "0.5  f32".

```cpp
template<typename T> bool TypedField(ID id, T& value, LayoutOverride&& constraints = {});
```

Classes: `typed-field`, `typed-field-input`, `typed-field-type`.

## Lists

A reorderable list over a `std::vector<T>`: each row has a grip, its index, the field
(numbers with their type) and a remove button, with "Add item" after the rows. `T` is
any type `Field` takes. `groupID` must be stable so the reorder state survives.

```cpp
template<typename T>
void Field(ID groupID, std::vector<T>& items, StyleRef style, LayoutOverride layoutOverride = {});
template<typename T>
void Field(ID groupID, std::vector<T>& items, LayoutOverride layoutOverride = {});
```

```cpp
Label("Names", std::to_string(names.size()) + " items");
Field(namesListID, names, LayoutOverride{}.FlexGrow(1));
```

Classes: `collection-area` around it, `collection-list`, `collection-row`,
`collection-index`, `collection-remove`, `collection-add`.

## Dictionaries

A key / value list with `std::string` keys: a header with the types, a row per entry
(key, value, remove) and "Add entry". Over a vector of pairs the rows keep their order;
over an `unordered_map` the order can change when the map rehashes, and a renamed key is
committed on Enter or focus loss (a rename onto an existing key is ignored).

```cpp
template<typename V>
void Field(std::vector<std::pair<std::string, V>>& items, StyleRef style, LayoutOverride layoutOverride = {});
template<typename V>
void Field(std::unordered_map<std::string, V>& items, StyleRef style, LayoutOverride layoutOverride = {});
```

Both also have a form without the style. Classes: `collection-area`,
`collection-header`, `collection-header-cell`, `collection-header-caption`,
`collection-header-type`, `collection-entry`, `collection-value`, `collection-remove`,
`collection-add-entry`.

---

[← Buttons and toggles](buttons-and-toggles.md) · [Controls](../controls.md) · [Sliders →](sliders.md)
