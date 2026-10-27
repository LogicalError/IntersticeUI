# Sliders

## SliderField

A filled bar with the value on it (and the optional text). Dragging the bar changes the
value; a click without dragging turns it into a numeric field for typing. The value is
clamped to `[minVal, maxVal]`; the `int` form snaps to whole numbers.

```cpp
bool SliderField(ID id, float& value, float minVal, float maxVal, std::string_view text,
                 LayoutOverride&& constraints = {});
bool SliderField(ID id, int& value, int minVal, int maxVal, std::string_view text,
                 LayoutOverride&& constraints = {});
```

Classes: `slider-field-track`, `slider-field-bg`, `slider-field-fill`,
`slider-field-text`, `slider-field-value`, `slider-field-edge`.

```cpp
SliderField(volume, 0.0f, 100.0f, "Volume");
```

## Slider

A groove and a knob, without value text; drag only, integer valued.

```cpp
bool Slider(ID id, int& value, int minVal, int maxVal, LayoutOverride&& constraints = {});
```

Classes: `slider`, `slider-groove`, `slider-fill`, `slider-knob`.

---

[← Fields](fields.md) · [Controls](../controls.md) · [Color and gradient fields →](color-and-gradient.md)
