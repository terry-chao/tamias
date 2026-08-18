add_library(tamias_ifc STATIC
  ifc_spatial_tree.cpp
)
add_library(tamias::ifc ALIAS tamias_ifc)
target_include_directories(tamias_ifc PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tamias_ifc
  PUBLIC
    tamias::core
    IfcOpenShell::IfcParse
)

add_executable(tamias_ifc_dump ifc_spatial_tree_main.cpp)
target_link_libraries(tamias_ifc_dump PRIVATE tamias::ifc)
tamias_copy_runtime_dlls(tamias_ifc_dump)
