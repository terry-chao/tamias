# OCCT 后端：全项目唯一 include BRep* / TopoDS* 做造型的地方。
#
# 源文件分两组，因为两组要的工具包差很多：
#
#   - 内核（occt_kernel / occt_mesh）：只依赖 FoundationClasses + ModelingData +
#     ModelingAlgorithms。一份最小的 wasm OCCT 就能编，是「造型引擎」的本体。
#   - 导入路径（occt_shape_ops）：STEP / IGES / BREP + XCAF 逐面颜色，要 DataExchange
#     那一组，而 OCCT 7.9 上那组还会把 Visualization 拖进来。
#
# 关掉 TAMIAS_OCCT_DATA_EXCHANGE 时整份 occt_shape_ops.cpp 不编——不是打补丁把它
# 掏空，而是这个文件从头到尾都泡在 XCAF 里（XCAF 文档、颜色工具、STEP/IGES reader
# 都是它的成员）。它的调用点全在桌面壳（src/app/main.cpp 的 register_occt_shape_ops），
# wasm 和服务端都不注册它：服务端 load_bytes 只认 .tdoc / .trscn / .obj。
set(_tamias_kernel_occt_sources
  occt_kernel.cpp
  occt_mesh.cpp
)
if(TAMIAS_OCCT_DATA_EXCHANGE)
  list(APPEND _tamias_kernel_occt_sources occt_shape_ops.cpp)
endif()

add_library(tamias_kernel_occt STATIC ${_tamias_kernel_occt_sources})
add_library(tamias::kernel_occt ALIAS tamias_kernel_occt)
target_include_directories(tamias_kernel_occt PUBLIC ${CMAKE_SOURCE_DIR}/src)
if(TAMIAS_OCCT_INCLUDE_DIR)
  target_include_directories(tamias_kernel_occt PUBLIC "${TAMIAS_OCCT_INCLUDE_DIR}")
endif()

# 谁要用 shape ops 的注册口，先看这个宏。定义成 0 / 1 而不是「有 / 没有」，
# 这样 #if 不会碰上未定义宏（-Wundef）。
if(TAMIAS_OCCT_DATA_EXCHANGE)
  target_compile_definitions(tamias_kernel_occt PUBLIC TAMIAS_HAS_OCCT_DATA_EXCHANGE=1)
else()
  target_compile_definitions(tamias_kernel_occt PUBLIC TAMIAS_HAS_OCCT_DATA_EXCHANGE=0)
endif()

target_link_libraries(tamias_kernel_occt
  PUBLIC
    tamias::kernel
    tamias::base
    tamias::graphics
)
if(TAMIAS_OCCT_LIBRARIES)
  target_link_libraries(tamias_kernel_occt PUBLIC ${TAMIAS_OCCT_LIBRARIES})
endif()
