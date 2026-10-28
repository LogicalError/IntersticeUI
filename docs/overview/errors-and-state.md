# Errors and saved state

## Errors

Calls that can fail return a `Result<T>`: the value, or an `Error` with an
`ErrorCode` and a message. It converts to true when it holds the value. `Result<void>`
is success or an error.

```cpp
if (tachyon::Result<void> result = app.Initialize(); !result)
    std::fprintf(stderr, "%s\n", result.GetError().message.c_str());
```

`Result<T>` has `Value()`, `operator*`, `operator->` and `ValueOr(fallback)`.
The error codes are `Failed`, `InvalidArgument`, `NotFound`, `ParseFailed`,
`Unsupported`, `OutOfMemory` and `DeviceLost`.

`Initialize`, `Run`, `RecordFrame`, `ReadWindowPixels` and `RestoreUIState` return
one; `ThemeReloadResult()` tells how the last `RequestReloadTheme` went.


## Saved UI state

The library can save the state of the UI as JSON text, which the application stores
wherever it likes. Name what to keep once, by keys that stay the same between runs;
the named objects must outlive the naming:

```cpp
PersistWindow("main", mainWindowID);         // position, size, mode and monitor
PersistDocking("dock", dockingLayout);       // panels by type, else title
PersistTable("assets", tableState, columns); // column widths and order
PersistValue("zoom", zoom);                  // an app setting (bool, int, float, string)
```

`PersistToolbars` keeps the toolbars. Then:

```cpp
std::string json = app.SaveUIState();                        // e.g. after Run()

if (tachyon::Result<void> restored = RestoreUIState(json); !restored)
    std::fprintf(stderr, "UI state not restored: %s\n", restored.GetError().message.c_str());
```

`RestoreUIState` applies windows when they are next declared and everything else at
once. Keys, panels and columns it doesn't know are skipped.

A window comes back on the monitor it was saved on. When that monitor is gone, it goes to
the same place on a monitor of the same size (the displays were rearranged), else on the
primary monitor, made to fit inside its work area. A window whose top would still be off
every monitor, where it couldn't be grabbed or closed, keeps only its size and is placed by
the app. The demo calls the
`Persist*` functions and `RestoreUIState` at the end of `Initialize`, and
`SaveUIState` after `Run()` returns.

---

[← Return values, content and units](values.md) · [Overview](../overview.md)
