set(_tamias_modeling_sources shape_ops.cpp)
if(TAMIAS_HAS_OCCT)
  list(APPEND _tamias_modeling_sources occt_shape_ops.cpp occt_feature.cpp occt_geom_builder.cpp)
endif()

add_library(tamias_modeling STATIC ${_tamias_modeling_sources})
add_library(tamias::modeling ALIAS tamias_modeling)
target_include_directories(tamias_modeling PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tamias_modeling PUBLIC tamias::core tamias::graphics)

if(TAMIAS_HAS_OCCT)
  target_compile_definitions(tamias_modeling PUBLIC TAMIAS_HAS_OCCT=1)
  target_include_directories(tamias_modeling PUBLIC "${TAMIAS_OCCT_INCLUDE_DIR}")
  target_link_libraries(tamias_modeling PUBLIC ${TAMIAS_OCCT_LIBRARIES})
  if(WIN32)
    target_link_directories(tamias_modeling PUBLIC
      $<$<CONFIG:Debug>:${TAMIAS_OCCT_LIBRARY_DIR_DEBUG}>
      $<$<NOT:$<CONFIG:Debug>>:${TAMIAS_OCCT_LIBRARY_DIR}>
    )
  endif()
endif()
