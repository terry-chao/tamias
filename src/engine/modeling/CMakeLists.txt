find_package(Threads REQUIRED)

# 内核接口层（对应 render/rhi：动词 + 注册表）
add_subdirectory(kernel)

# 中间层：特征树、求值器、草图数学、拓扑命名。不含任何内核实现。
add_library(tamias_modeling STATIC
  curve_geom.cpp
  edge_fingerprint.cpp
  evaluator.cpp
  geom_builder.cpp
  linked_kernels.cpp
  tess_worker.cpp
)

add_library(tamias::modeling ALIAS tamias_modeling)
target_include_directories(tamias_modeling PUBLIC
  ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(tamias_modeling PUBLIC
  tamias::kernel
  tamias::base
  tamias::graphics
  Threads::Threads
)

# 后端：OCCT。中间层不 include 它，只是把注册进来的内核当默认实现用（过渡期）。
if(TAMIAS_ENABLE_OCCT)
  add_subdirectory(occt)
endif()
if(TARGET tamias::kernel_occt)
  target_link_libraries(tamias_modeling PUBLIC tamias::kernel_occt)
  target_compile_definitions(tamias_modeling PRIVATE TAMIAS_HAS_KERNEL_OCCT=1)
endif()

# 后端：Truck（Rust）。默认关；打开后 conformance 用例会一并跑它。
if(TAMIAS_ENABLE_TRUCK_KERNEL)
  add_subdirectory(truck)
endif()
if(TARGET tamias::kernel_truck)
  target_link_libraries(tamias_modeling PUBLIC tamias::kernel_truck)
  target_compile_definitions(tamias_modeling PRIVATE TAMIAS_HAS_KERNEL_TRUCK=1)
endif()
