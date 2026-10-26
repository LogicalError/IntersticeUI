# IntersticeUI documentation

IntersticeUI is an immediate-mode GUI library for C++20 on Vulkan, with windows and
input through SDL3 by default. These guides explain how it works and how to use it;
the [README](https://github.com/LogicalError/IntersticeUI#readme) has the requirements, the build and a first program.

- [Overview](overview.md): the frame model, IDs, the Begin/End rules, results and
  errors, units, saving UI state.
- [Controls](controls.md): every control and container, with its signature, what it
  returns and its default CSS class.
- [Styling with CSS](css.md): how themes load, the selectors and states, and every
  supported property.
- [Integration](integration.md): adding the library to a build, your own Vulkan device,
  window and input, frame loop, and rendering into your own image.

## API reference

The headers carry `///` comments, from which Doxygen builds a reference, published at
https://logicalerror.github.io/IntersticeUI/ with each change on main. To build it locally, with Doxygen
installed, configure the project and build the `interstice_docs` target:

```text
cmake -S . -B build
cmake --build build --target interstice_docs
```

The reference is written to `build/docs/html/index.html`.

## Design

[design/graphite](design/graphite) holds the Graphite theme's specification and mockups,
the design the default themes follow.
