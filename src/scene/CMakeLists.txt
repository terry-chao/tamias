add_library(tamias_scene STATIC scene.cpp)
add_library(tamias::scene ALIAS tamias_scene)
target_include_directories(tamias_scene PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(tamias_scene PUBLIC tamias::math)
