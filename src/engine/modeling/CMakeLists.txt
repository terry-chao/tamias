set(_tamias_modeling_src shape_ops.cpp curve_geom.cpp)
if(TAMIAS_ENABLE_OCCT)
  list(APPEND _tamias_modeling_src
    occt_shape_ops.cpp
    occt_feature.cpp
    occt_geom_builder.cpp)
else()
  list(APPEND _tamias_modeling_src stub_geom_builder.cpp)
endif()

add_library(tamias_modeling STATIC ${_tamias_modeling_src})
add_library(tamias::modeling ALIAS tamias_modeling)
target_include_directories(tamias_modeling PUBLIC
  ${CMAKE_SOURCE_DIR}/src
)
if(TAMIAS_ENABLE_OCCT AND TAMIAS_OCCT_INCLUDE_DIR)
  target_include_directories(tamias_modeling PUBLIC "${TAMIAS_OCCT_INCLUDE_DIR}")
endif()
target_link_libraries(tamias_modeling PUBLIC
  tamias::core
  tamias::graphics
)
if(TAMIAS_ENABLE_OCCT)
  target_link_libraries(tamias_modeling PUBLIC ${TAMIAS_OCCT_LIBRARIES})
endif()