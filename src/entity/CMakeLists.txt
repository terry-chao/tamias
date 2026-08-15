add_library(tamias_entity STATIC
  scene.cpp
  document_io.cpp
  picking.cpp
)
add_library(tamias::entity ALIAS tamias_entity)
target_include_directories(tamias_entity PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tamias_entity
  PUBLIC
    tamias::core
    tamias::math
    tamias::graphics
    tamias::io
)
