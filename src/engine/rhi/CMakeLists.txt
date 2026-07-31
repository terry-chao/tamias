add_library(tamias_rhi STATIC device_factory.cpp)
add_library(tamias::rhi ALIAS tamias_rhi)
target_include_directories(tamias_rhi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(tamias_rhi PUBLIC tamias::core tamias::math tamias::graphics)
