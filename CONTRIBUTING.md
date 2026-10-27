# Contributing to TachyonUI

Thanks for wanting to help! Bug reports, ideas and pull requests are all welcome.

* **Report problems** with the steps, or a small `OnGUI` snippet, that show them: [open an issue](https://github.com/LogicalError/TachyonUI/issues/new/choose).
* **Suggest features** the same way.
* **Fix bugs or build features**, see below.

## Setting up

Follow [Requirements](README.md#requirements) and [Build](README.md#build) in the README. In short: clone with
`--recursive`, have [Git LFS](https://git-lfs.com/) installed (the fonts live in LFS) and the
[Vulkan SDK](https://vulkan.lunarg.com/), then configure and build with CMake.

## Where things are

* `src/gui_core`: windowing, rendering backends, input, layout, text, styles and element storage.
* `src/gui_controls`: `GUIApplication`, the controls, docking, menus, tooltips and transitions.
* `examples`: small programs that each show one thing; `tests`: the unit and GPU tests.
* `docs`: the guides; [docs/README.md](docs/README.md) is the place to start.

## Pull requests

* Make a branch for each change, and keep a pull request to one thing: a fix, or a feature.
* Write code that reads like the code around it. `.clang-format` matches the existing style; format only what you touch.
* Say in the pull request what it changes and why, and how you tested it, on which platform and GPU.
* For a bug fix, link the issue it fixes, and add a test that fails without the fix.
* Add a line under **Unreleased** in [CHANGELOG.md](CHANGELOG.md) for anything a user of the library would notice.
* The submodules in `third_party` are pinned on purpose. Update one only with a build and both test runs, and
  list a new third-party library in [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt).

Pull requests are squash-merged into a single commit, which keeps you as its author.

## Tests

Build with the tests (on by default) and run them from the repository root:

```text
bin/Debug/gui_core_tests          unit tests, no GPU needed
bin/Debug/gui_gpu_tests           real-device tests, skipped without a GPU
ctest --test-dir build -C Debug   both, one process per test
```

Please run both before opening a pull request.

## License

By contributing, you agree that your contributions are licensed under the [MIT license](LICENSE), like the rest of
TachyonUI.
