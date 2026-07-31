add_library(tamias_scene INTERFACE)
add_library(tamias::scene ALIAS tamias_scene)
target_include_directories(tamias_scene INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(tamias_scene INTERFACE tamias::math)
