add_library(tamias_graphics INTERFACE)
add_library(tamias::graphics ALIAS tamias_graphics)
target_include_directories(tamias_graphics INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(tamias_graphics INTERFACE tamias::math)
