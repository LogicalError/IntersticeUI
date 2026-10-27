# Color and gradient fields

## ColorField

A swatch, the color as hex and its alpha in percent. Clicking the swatch opens the color
picker; OK keeps the color, Cancel and the close button restore the original.
`allowHDR` allows values above 1.

```cpp
bool ColorField(ID id, color4f& value, bool allowHDR = false, LayoutOverride&& constraints = {});
bool ColorField(ID id, color4b& value, LayoutOverride&& constraints = {});
```

Returns `true` when the value changed. Classes: `color-field`, `color-swatch`,
`color-hex`, `color-alpha`; the picker uses `color-picker` and the `color-picker-*`
classes.

```cpp
DragLabel("Color");
ColorField(color);
```

## GradientField

A gradient swatch; clicking it opens the gradient editor, with the same OK / Cancel
behavior.

```cpp
bool GradientField(ID id, Gradient& value, LayoutOverride&& constraints = {});
```

Classes: `gradient-swatch`; the editor uses `gradient-editor` and the
`gradient-editor-*` classes.

---

[← Sliders](sliders.md) · [Controls](../controls.md) · [Menus →](menus.md)
