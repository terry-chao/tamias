# MKI（Modeling Kernel Interface，对应 render/rhi）：只有动词与注册表，不含任何后端实现。
add_library(tamias_mki STATIC kernel_registry.cpp shape_ops.cpp)
add_library(tamias::mki ALIAS tamias_mki)
target_include_directories(tamias_mki PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tamias_mki PUBLIC tamias::base tamias::graphics tamias::math)

# 各造型后端挂在接口层下面，和 render/rhi/<backend>/ 是同一形状。
# 接口层不 include 后端；后端自己实现 ModelKernel，并用 register_*_kernel_backend() 注册。
if(TAMIAS_ENABLE_OCCT)
  add_subdirectory(occt)
endif()
if(TAMIAS_ENABLE_TRUCK_KERNEL)
  add_subdirectory(truck)
endif()
if(TAMIAS_ENABLE_SMLIB_KERNEL)
  add_subdirectory(smlib)
endif()
