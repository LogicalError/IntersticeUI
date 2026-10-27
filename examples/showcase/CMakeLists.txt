# The showcase: an editor-style app with most of the library in its panels. Its own files
# (showcase.css, icons, the Noto fallback fonts) load from workdirectory/ through
# SetResourceRoot(TACHYON_RESOURCE_DIR), so it runs from any folder.
tachyon_add_example(showcase
  main.cpp
  application.h
  application.cpp
  panel_box_test.cpp
  panel_console.cpp
  panel_files.cpp
  panel_hierarchy.cpp
  panel_inspector.cpp
  panel_misc.cpp
  panel_node_view.cpp
  panel_profiler_tree.cpp
  panel_reorderable.cpp
  panel_settings.cpp
  panel_split_scroll.cpp
  panel_table.cpp
  panel_tree_table.cpp
  panel_view.cpp

  # The theme files it uses, for editing them in the IDE
  "${PROJECT_SOURCE_DIR}/workdirectory/themes/dark.css"
  "${PROJECT_SOURCE_DIR}/workdirectory/themes/light.css"
  "${PROJECT_SOURCE_DIR}/workdirectory/themes/styles.css"
  "${PROJECT_SOURCE_DIR}/workdirectory/themes/showcase.css")

tachyon_precompile_headers(showcase <gui_controls/gui_application.h>)
