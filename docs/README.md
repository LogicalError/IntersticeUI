# TachyonUI documentation

TachyonUI is an immediate-mode GUI library for C++20 on Vulkan, with windows and
input through SDL3 by default. You describe the whole UI in code every frame; the
library keeps the state, lays it out with flexbox and grid, and draws it.

New here? Start with the [README](https://github.com/LogicalError/TachyonUI#readme)
for the requirements, the build and a first program, then read the Overview.

| Guide | Read it for |
| --- | --- |
| [Overview](overview.md) | The frame model, IDs, the Begin/End rules, return values, units, errors and saved state. |
| [Controls](controls.md) | Every control and container, by family: what it takes, what it returns and how it's styled. |
| [CSS and theming](css.md) | How themes load, the selectors and states, values, and every supported property. |
| [Integration](integration.md) | Adding the library to a build, your own Vulkan device, window, input and frame loop, and rendering into your own image. |
| [Examples](examples.md) | Walkthroughs of the example programs, from hello world to hosting on your own Vulkan device. |

The [API reference](https://logicalerror.github.io/TachyonUI/) is built from these guides
and the headers' `///` comments, and is published with each change on main. Its
**API by topic** pages group the classes by area: application, controls, layout, styling,
text, input, graphics and windows.

To build it locally, install [Doxygen](https://www.doxygen.nl/) 1.18 or newer, then
configure the project and build the `tachyon_docs` target:

```text
cmake -S . -B build
cmake --build build --target tachyon_docs
```

The reference is written to `build/docs/html/index.html`.

<!--! Page tree for the API reference.
\subpage md_docs_2overview
\subpage md_docs_2controls
\subpage md_docs_2css
\subpage md_docs_2integration
\subpage md_docs_2examples
-->
