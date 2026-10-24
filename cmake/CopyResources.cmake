# Copies the runtime resources a GUI loads (fonts, themes, icons) from SOURCE
# to DESTINATION. Run with cmake -P; the compiled shaders are built in place.
# The Noto fallback fonts come along only with FALLBACK_FONTS.
foreach(_dir fonts themes icons)
  if(_dir STREQUAL "fonts" AND NOT FALLBACK_FONTS)
    file(COPY "${SOURCE}/${_dir}" DESTINATION "${DESTINATION}" PATTERN Noto-otf EXCLUDE)
  else()
    file(COPY "${SOURCE}/${_dir}" DESTINATION "${DESTINATION}")
  endif()
endforeach()
