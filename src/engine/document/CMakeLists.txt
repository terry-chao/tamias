add_library(tamias_document STATIC
  scene.cpp
  document.cpp
  document_io.cpp
  picking.cpp
  entity.cpp
)
add_library(tamias::document ALIAS tamias_document)
target_include_directories(tamias_document PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tamias_document
  PUBLIC
    tamias::core
    tamias::math
    tamias::graphics
    tamias::io
    tamias::modeling
)
