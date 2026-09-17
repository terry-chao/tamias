# OCCT 后端：全项目唯一 include BRep* / TopoDS* 做造型的地方。
add_library(tamias_kernel_occt STATIC
  occt_kernel.cpp
  occt_shape_ops.cpp
  occt_mesh.cpp
)
add_library(tamias::kernel_occt ALIAS tamias_kernel_occt)
target_include_directories(tamias_kernel_occt PUBLIC ${CMAKE_SOURCE_DIR}/src)
if(TAMIAS_OCCT_INCLUDE_DIR)
  target_include_directories(tamias_kernel_occt PUBLIC "${TAMIAS_OCCT_INCLUDE_DIR}")
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
