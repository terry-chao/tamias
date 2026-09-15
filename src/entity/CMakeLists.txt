add_library(tamias_entity STATIC
  ${CMAKE_SOURCE_DIR}/src/bim/point_location.cpp
  ${CMAKE_SOURCE_DIR}/src/bim/line_location.cpp
  ${CMAKE_SOURCE_DIR}/src/bim/surface_location.cpp
  # 核心：实体基类、夹点、识别色
  core/entity.cpp
  core/entity_grip.cpp
  # 族实体：族基类 family_entity.h；下面按「在宿主关系里扮演什么角色」分
  # 宿主实体：能独立存在、能承接附属件与开洞的构件
  # 建筑：墙、幕墙
  family/host/architectural/wall_entity.cpp
  family/host/architectural/curtain_wall_entity.cpp
  # 结构：梁、柱、板、结构墙、基础
  family/host/structural/beam_entity.cpp
  family/host/structural/column_entity.cpp
  family/host/structural/slab_entity.cpp
  family/host/structural/structural_wall_entity.cpp
  family/host/structural/foundation_entity.cpp
  # 附属实体：只能挂在宿主上；扣减（开洞）实体是其中一类 —— 门、窗
  family/attached/opening/door_entity.cpp
  family/attached/opening/window_entity.cpp
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
