# Copies the runtime resources a GUI loads (fonts, themes, icons) from SOURCE
# to DESTINATION. Run with cmake -P; the compiled shaders are built in place.
foreach(_dir fonts themes icons)
  file(COPY "${SOURCE}/${_dir}" DESTINATION "${DESTINATION}")
endforeach()
