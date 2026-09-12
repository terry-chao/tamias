add_library(tamias_drawing STATIC
  drawing.cpp
  dxf_reader.cpp
)
add_library(tamias::drawing ALIAS tamias_drawing)
target_include_directories(tamias_drawing PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tamias_drawing
  PUBLIC
    tamias::core
    tamias::math
)
