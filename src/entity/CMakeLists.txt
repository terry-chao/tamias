add_library(tamias_entity STATIC
  ${CMAKE_SOURCE_DIR}/src/bim/point_location.cpp
  ${CMAKE_SOURCE_DIR}/src/bim/line_location.cpp
  ${CMAKE_SOURCE_DIR}/src/bim/surface_location.cpp
  entity.cpp
  wall_entity.cpp
  box_entity.cpp
  cylinder_entity.cpp
  beam_entity.cpp
  column_entity.cpp
  slab_entity.cpp
  door_entity.cpp
  window_entity.cpp
  line_entity.cpp
  polyline_entity.cpp
  circle_entity.cpp
  arc_entity.cpp
  bezier_entity.cpp
  rectangle_entity.cpp
  bspline_entity.cpp
  nurbs_entity.cpp
  entity_grip.cpp
)
add_library(tamias::entity ALIAS tamias_entity)
target_include_directories(tamias_entity PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tamias_entity
  PUBLIC
    tamias::core
    tamias::math
    tamias::graphics
    tamias::modeling
)
