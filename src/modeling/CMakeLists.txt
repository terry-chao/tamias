add_library(tamias_modeling STATIC shape_ops.cpp)
add_library(tamias::modeling ALIAS tamias_modeling)
target_include_directories(tamias_modeling PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(tamias_modeling PUBLIC tamias::core tamias::graphics)
