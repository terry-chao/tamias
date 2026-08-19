add_library(tamias_modeling STATIC
  shape_ops.cpp
  occt_shape_ops.cpp
  occt_feature.cpp
  occt_geom_builder.cpp
  curve_geom.cpp
)
add_library(tamias::modeling ALIAS tamias_modeling)
target_include_directories(tamias_modeling PUBLIC
  ${CMAKE_SOURCE_DIR}/src
)
if(TAMIAS_OCCT_INCLUDE_DIR)
  target_include_directories(tamias_modeling PUBLIC "${TAMIAS_OCCT_INCLUDE_DIR}")
endif()
target_link_libraries(tamias_modeling PUBLIC
  tamias::core
  tamias::graphics
  ${TAMIAS_OCCT_LIBRARIES}
)
