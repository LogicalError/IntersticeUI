# nanosvg — header-only SVG parser (no rasterizer; we drive skribidi's
# icon builder from the parsed paths). NANOSVG_IMPLEMENTATION must be
# defined in exactly one translation unit by the consumer.
add_library(nanosvg INTERFACE)
target_include_directories(nanosvg INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/nanosvg>
)

if(_tachyon_tp_standalone)
  install(TARGETS nanosvg EXPORT nanosvgTargets)
  install(FILES nanosvg.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/nanosvg)
  install(EXPORT nanosvgTargets
    FILE nanosvgConfig.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/nanosvg)
endif()
