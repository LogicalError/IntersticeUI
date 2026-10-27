# Lists and reorderable groups

## BeginListView / ListItem

A list of selectable items. `selectedIndex` is the selected item's index (Up / Down move
it when the list has focus); `itemCount` bounds the keyboard movement. With an
`ItemDragDropConfig` whose `payloadType` is not 0, items get a drag handle and become
drag sources and drop targets.

```cpp
bool BeginListView(ID id, int* selectedIndex, int itemCount, ItemDragDropConfig* dragDrop, StyleRef style,
                   LayoutOverride&& constraints = {});
void EndListView();
bool ListItem(ID id, const GUIContent& content, StyleRef style, LayoutOverride&& constraints = {});
int  FindListItemIndex(ID id) const;
```

`ListItem` returns `true` in the frame after a click. `FindListItemIndex` maps an item ID
to its index in end-of-frame handlers (drag and drop). Classes: `list-view`,
`list-item`; with drag and drop `list-item-row`, `list-item-body`,
`list-item-drag-knob-vert`.

```cpp
BeginListView(&selected, int(names.size()));
for (const std::string& name : names)
    ListItem(name);
EndListView();
```

## BeginReorderableGroup / BeginReorderableItem

A container whose items can be dragged into a new order. `onReorder(fromIndex, toIndex)`
fires on drop; the app moves its data. `isWrapped` lets the items wrap onto more lines.
`onDragOut(index, screenPos)` fires when an item is released outside the group, and
`dragVisualizer(index, screenPos)` runs each frame while one is dragged outside it
(typically to draw a ghost window).

```cpp
bool BeginReorderableGroup(ID id, ReorderDirection direction, bool isWrapped,
                           std::function<void(int fromIndex, int toIndex)> onReorder, StyleRef style,
                           LayoutOverride&& constraints = {},
                           std::function<void(int, int2)> onDragOut = nullptr,
                           std::function<void(int, int2)> dragVisualizer = nullptr);
bool BeginReorderableGroup(ID id, ReorderDirection direction, std::function<void(int fromIndex, int toIndex)> onReorder,
                           LayoutOverride&& constraints = {});
void EndReorderableGroup();
bool BeginReorderableItem(ID id, StyleRef style, LayoutOverride&& constraints = {});
void EndReorderableItem();
void EmitDragHandle();
```

`EmitDragHandle` adds a grip inside an item; clicks on it reach the item. Classes:
`horzReorderableGroup`, `vertReorderableGroup` (`horzReorderableGroupWrap`,
`vertReorderableGroupWrap` when wrapped), `reorderableItem`, `reorder-drop-slot`,
`reorder-lifted`.

```cpp
BeginReorderableGroup(groupID, ReorderDirection::Vertical, [this](int from, int to)
{
    std::string moved = std::move(items[from]);
    items.erase(items.begin() + from);
    items.insert(items.begin() + to, std::move(moved));
});
for (const std::string& item : items)
{
    BeginReorderableItem();
    EmitDragHandle();
    Text(item);
    EndReorderableItem();
}
EndReorderableGroup();
```

---

[← Tooltips and message dialogs](tooltips-and-dialogs.md) · [Controls](../controls.md) · [Tables →](tables.md)
