# Examples

The programs in [`examples/`](https://github.com/LogicalError/TachyonUI/tree/main/examples)
are complete applications, one folder each. The small ones each show one part of the
library; the showcase puts most of it together in an editor-style app. They build with
the project, and every one runs from any folder:

| | Build | Run |
| --- | --- | --- |
| Windows | `build.bat` | `bin\Debug\hello_world.exe` |
| Linux and macOS | `./build.sh` | `bin/Debug/hello_world` |

The small examples need no files at all: the shaders, the default themes and the fonts are
built into the library. They are what integrating TachyonUI into your own program looks
like.

`TACHYON_BUILD_EXAMPLES` and `TACHYON_BUILD_SHOWCASE` turn them off (they are on when
TachyonUI is the top-level project).

This guide walks through each one:

| Example | What it shows |
| --- | --- |
| [Hello world](examples/hello-world.md) | The smallest app: a window, a text field and buttons, and the program around them. |
| [Controls](examples/controls.md) | The common controls as label/value rows: buttons, toggles, text and number fields, a slider, a color, a menu and tabs. |
| [Docking](examples/docking.md) | Panels in a dock layout that the user can rearrange, tear off and dock again. |
| [Custom theme](examples/custom-theme.md) | Your own CSS classes next to the library's, and switching palettes and font size at runtime. |
| [Host device](examples/host-device.md) | Running on a Vulkan device the application already owns, and showing an image it renders. |
| [Showcase](examples/showcase.md) | An editor-style app with most of the library in its panels: docking, tables, trees, a file browser, a node graph, settings. |

The API reference shows each program's full source under **Examples**, and links the
functions they use back to them.

<!--! Page tree for the API reference.
\subpage md_docs_2examples_2hello-world
\subpage md_docs_2examples_2controls
\subpage md_docs_2examples_2docking
\subpage md_docs_2examples_2custom-theme
\subpage md_docs_2examples_2host-device
\subpage md_docs_2examples_2showcase
-->
