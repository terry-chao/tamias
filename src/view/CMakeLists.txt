add_library(tamias_view STATIC picking.cpp)
add_library(tamias::view ALIAS tamias_view)
target_include_directories(tamias_view PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(tamias_view PUBLIC tamias::document tamias::math)
