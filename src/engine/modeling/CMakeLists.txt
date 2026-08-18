add_library(tamias_modeling STATIC
  shape_ops.cpp
  occt_shape_ops.cpp
  occt_feature.cpp
  occt_geom_builder.cpp
)
add_library(tamias::modeling ALIAS tamias_modeling)
target_include_directories(tamias_modeling PUBLIC
  ${CMAKE_SOURCE_DIR}/src
  "${TAMIAS_OCCT_INCLUDE_DIR}"
)
target_link_libraries(tamias_modeling PUBLIC
  tamias::core
  tamias::graphics
  ${TAMIAS_OCCT_LIBRARIES}
)
if(WIN32)
  target_link_directories(tamias_modeling PUBLIC
    $<$<CONFIG:Debug>:${TAMIAS_OCCT_LIBRARY_DIR_DEBUG}>
    $<$<NOT:$<CONFIG:Debug>>:${TAMIAS_OCCT_LIBRARY_DIR}>
  )
endif()
