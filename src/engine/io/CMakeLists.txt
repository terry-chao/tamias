add_library(tamias_io STATIC
  binary_archive.cpp
  mesh_io.cpp
)
add_library(tamias::io ALIAS tamias_io)
target_include_directories(tamias_io PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_include_directories(tamias_io PRIVATE ${RAPIDOBJ_INCLUDE_DIRS})
target_link_libraries(tamias_io
  PUBLIC
    tamias::core
    tamias::graphics
    tamias::math
)
