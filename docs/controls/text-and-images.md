# Text and images

## Text

Static text.

```cpp
void Text(ID id, std::string_view text, StyleRef style, LayoutOverride&& constraints = {});
```

Default class: `static-text`.

## Label

A label for the control after it. Inside a label group it takes the group's width.

```cpp
void Label(ID id, std::string_view text, StyleRef style, LayoutOverride&& constraints = {});
void Label(ID id, std::string_view text, std::string_view caption, LayoutOverride&& constraints = {});
```

The second form puts the label at the top of a tall row (such as a list), with a caption
under it, for example `Label("Items", "3 items")`. Default class: `label`; the caption
form uses `group-label-top`, `group-label-title` and `group-label-caption`.

## DragLabel

A label that changes the value of the numeric field after it when dragged sideways.

```cpp
void DragLabel(ID id, std::string_view text, StyleRef style, LayoutOverride&& constraints = {});
```

Default class: `drag-label`.

```cpp
DragLabel("Float");
Field(floatValue);
```

## SelectableText

Read-only rich text with links, mouse selection and Ctrl+C. The text is a span of
`ContentRun`s (text or icon runs; a run with `isLink` and a non-zero `contentId` is a
link). `onLinkActivate` gets a clicked link's `contentId`. Use a stable ID so the
selection survives across frames.

```cpp
void SelectableText(ID id, const std::span<ContentRun>& runs,
                    std::function<void(intptr_t contentId)> onLinkActivate,
                    StyleRef style, LayoutOverride&& constraints = {});
```

There is no default style; pass one.

## Image

Draws an image registered with `graphicsContext->RegisterImage`, stretched over the
element. With `sourceSize` (the image's pixel size) it keeps the aspect ratio.

```cpp
void Image(ID id, ImageIndex image, StyleRef style, LayoutOverride&& constraints = {});
void Image(ID id, ImageIndex image, uint2 sourceSize, StyleRef style, LayoutOverride&& constraints = {});
```

Default style: `theme.imageStyleRef`, built in code (CSS can't express the texture
fill).

---

[← Windows and layout](windows-and-layout.md) · [Controls](../controls.md) · [Buttons and toggles →](buttons-and-toggles.md)
