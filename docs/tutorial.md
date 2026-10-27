# Tutorial: your first app

Step by step, from an empty folder to an app with two docked panels: one with buttons
that change a count, and one with fields that change a greeting.

You need a C++20 compiler, CMake 3.22 or newer, the [Vulkan SDK](https://vulkan.lunarg.com/)
and [Git LFS](https://git-lfs.com/). The [README](https://github.com/LogicalError/TachyonUI#readme)
lists the exact versions.

The finished program is in [`examples/tutorial`](https://github.com/LogicalError/TachyonUI/tree/main/examples/tutorial)
and builds with the other examples, so you can run it before you start. The tests in
`tests/tutorial_test.cpp` check it does what these pages say, and
`tests/tutorial_snippets_test.cpp` checks every code block here is the program's own code.

| Step | |
| --- | --- |
| [1. Set up the project](tutorial/setup.md) | Add TachyonUI to a CMake project and build it. |
| [2. Buttons](tutorial/buttons.md) | Panels as classes; a panel with a count and buttons that change it. |
| [3. Fields](tutorial/fields.md) | A panel with text, number and toggle fields. |
| [4. The app](tutorial/app.md) | A window with the panels docked side by side, and `main`. |

<!--! Page tree for the API reference.
\subpage md_docs_2tutorial_2setup
\subpage md_docs_2tutorial_2buttons
\subpage md_docs_2tutorial_2fields
\subpage md_docs_2tutorial_2app
-->
