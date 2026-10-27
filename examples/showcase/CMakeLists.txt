# The showcase: an editor-style app with most of the library in its panels. It loads its
# files relative to the working directory, so it runs from workdirectory/.
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

set_target_properties(showcase PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/workdirectory")
tachyon_precompile_headers(showcase <gui_controls/gui_application.h>)
