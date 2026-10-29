# Custom drawing

Any element can draw with its own callback at render time. Register an element with
`RegisterElement`, then attach the callback with `elementStorage.AddCustomDraw`:

```cpp
ElementRef RegisterElement(Element&& element, LayoutOverride&& constraints);
ElementRef RegisterElement(Element&& element, std::string_view text, LayoutOverride&& constraints);

// ElementStorage:
ElementStorageResult AddCustomDraw(const ElementRef& ref, CustomDrawCallback callback);
ElementStorageResult AddCustomDrawAfter(const ElementRef& ref, CustomDrawCallback callback);
ElementStorageResult AddPostLayout(const ElementRef& ref, PostLayoutCallback callback);
```

`AddCustomDraw` runs before the element's children are drawn; `AddCustomDrawAfter`
runs after its whole subtree (call it after the element's children are closed).
`AddPostLayout` runs after layout and before rendering, with the final geometry and no
drawing, for state that depends on it.

The callback gets a `CustomDrawContext`. By default it draws in the element's local
space: (0, 0) is the top-left of its padding box, and pan / zoom of its ancestors applies.
`SetSpace(DrawSpace::Screen)` switches to absolute layout pixels, and
`ScreenToCurrent(screenPos)` converts a cursor position into the current space.

| Method | Draws or returns |
| --- | --- |
| `ElementRect()`, `ElementInnerRect()` | The element's border box and padding box, in the current space |
| `GetElementRect(ref)`, `GetInnerRect(ref)` | Another element's rects |
| `RenderBox(rect, boxStyle, state, ...)` | A styled box |
| `RenderQuad(rect, colorStyle, cornerRadius, ...)` | A filled rectangle |
| `RenderLine(p0, p1, lineStyle)` | A line (also a four-point form) |
| `RenderPolyline(points, closed, lineStyle, cornerRadius)` | A polyline |
| `RenderCircle`, `RenderDisc`, `RenderDot` | Circle outline (exactly round; a gradient and dashes run clockwise from the right), filled disc, dot |
| `RenderTriangle`, `RenderFilledPolygon` | Filled triangle, convex polygon |
| `RenderText(textId, rect)` | Text registered with `textSystem->RegisterText` |

Colors are style indices (for example `theme.lineWhiteStyle`, or one registered with
`styleManager.RegisterColorStyle`); `LineStyle` has `colorStyleIndex`, `lineWidth` and
dash settings.

```cpp
const ElementRef ref = RegisterElement(Element{ .id = canvasID, .styleRef = theme.GetStyle("my-canvas") },
                                       LayoutOverride::FixedSize(200, 100));
VERIFY(elementStorage.AddCustomDraw(ref, [this](CustomDrawContext& ctx)
{
    const IntRect r = ctx.ElementInnerRect();
    const LineStyle line{ .colorStyleIndex = theme.lineWhiteStyle, .lineWidth = styleManager.Px(2.0f) };
    ctx.RenderLine(r.position, r.position + r.size, line);
}));
```

The callback runs at render time, after `OnGUI` returns, so its captures must outlive the
frame.

---

[← Other controls](other-controls.md) · [Controls](../controls.md) · [Default CSS classes →](css-classes.md)
