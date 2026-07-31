add_library(tamias_rhi_opengl STATIC opengl_backend.cpp)
add_library(tamias::rhi_opengl ALIAS tamias_rhi_opengl)
target_include_directories(tamias_rhi_opengl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(tamias_rhi_opengl PUBLIC tamias::rhi)
