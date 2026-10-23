find_package(Threads REQUIRED)

# MKI（Modeling Kernel Interface）+ 各造型后端（mki/occt、mki/truck、mki/smlib）。
# 目录形状对应 render/rhi：抽象在 mki/，后端在 mki/<backend>/。
add_subdirectory(mki)

# 中间层：特征树、求值器、草图数学、拓扑命名。不含任何内核实现。
add_library(tamias_modeling STATIC
  # 配方：特征树数据 + 曲线定义与参数转换
  feature/curve_geom.cpp
  # 求值链路：求值器、几何建造器、异步离散、边指纹
  evaluate/edge_fingerprint.cpp
  evaluate/evaluator.cpp
  evaluate/geom_builder.cpp
  evaluate/tess_worker.cpp
  # 后端装配：把编进来的内核登记成默认实现
  linked_kernels.cpp
)

add_library(tamias::modeling ALIAS tamias_modeling)
target_include_directories(tamias_modeling PUBLIC
  ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(tamias_modeling PUBLIC
  tamias::mki
  tamias::base
  tamias::graphics
  Threads::Threads
)

# 后端目标由 mki/ 下的 CMakeLists 创建；中间层不 include 它们，
# 只是把注册进来的内核当默认实现用（过渡期）。
if(TARGET tamias::mki_occt)
  target_link_libraries(tamias_modeling PUBLIC tamias::mki_occt)
  target_compile_definitions(tamias_modeling PRIVATE TAMIAS_HAS_MKI_OCCT=1)
endif()

if(TARGET tamias::mki_truck)
  target_link_libraries(tamias_modeling PUBLIC tamias::mki_truck)
  target_compile_definitions(tamias_modeling PRIVATE TAMIAS_HAS_MKI_TRUCK=1)
endif()

if(TARGET tamias::mki_smlib)
  target_link_libraries(tamias_modeling PUBLIC tamias::mki_smlib)
  target_compile_definitions(tamias_modeling PRIVATE TAMIAS_HAS_MKI_SMLIB=1)
endif()
