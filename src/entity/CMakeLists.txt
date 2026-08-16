add_library(tamias_entity STATIC
  entity.cpp
  wall_entity.cpp
  box_entity.cpp
  cylinder_entity.cpp
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
