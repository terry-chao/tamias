# 内核接口层（对应 render/rhi）：只有动词与注册表，不含任何后端实现。
add_library(tamias_kernel STATIC kernel_registry.cpp shape_ops.cpp)
add_library(tamias::kernel ALIAS tamias_kernel)
target_include_directories(tamias_kernel PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tamias_kernel PUBLIC tamias::base tamias::graphics tamias::math)
