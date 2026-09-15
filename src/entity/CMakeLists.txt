add_library(tamias_entity STATIC
  ${CMAKE_SOURCE_DIR}/src/bim/point_location.cpp
  ${CMAKE_SOURCE_DIR}/src/bim/line_location.cpp
  ${CMAKE_SOURCE_DIR}/src/bim/surface_location.cpp
  # 核心：实体基类、夹点、识别色
  core/entity.cpp
  core/entity_grip.cpp
  # 族实体：BIM 构件的族基类 + 扣减（开洞）实体，后者附属在墙 / 板等宿主上；
  # 下按专业分目录
  family/door_entity.cpp
  family/window_entity.cpp
  # 建筑族：墙、幕墙（宿主构件）
  family/architectural/wall_entity.cpp
  family/architectural/curtain_wall_entity.cpp
  # 结构族：梁、柱、板、结构墙、基础
  family/structural/beam_entity.cpp
  family/structural/column_entity.cpp
  family/structural/slab_entity.cpp
  family/structural/structural_wall_entity.cpp
  family/structural/foundation_entity.cpp
  # 草图：直线、折线、圆、圆弧、贝塞尔、矩形、B 样条、NURBS
  sketch/line_entity.cpp
  sketch/polyline_entity.cpp
  sketch/circle_entity.cpp
  sketch/arc_entity.cpp
  sketch/bezier_entity.cpp
  sketch/rectangle_entity.cpp
  sketch/bspline_entity.cpp
  sketch/nurbs_entity.cpp
  # 基础体：盒子、圆柱
  primitive/box_entity.cpp
  primitive/cylinder_entity.cpp
)
add_library(tamias::entity ALIAS tamias_entity)
target_include_directories(tamias_entity PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tamias_entity
  PUBLIC
    tamias::base
    tamias::math
    tamias::graphics
    tamias::modeling
)
