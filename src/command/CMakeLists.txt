add_library(tamias_command STATIC
  # 基础设施：命令接口、撤销栈、命令系统、历史、登记入口
  core/command_system.cpp
  core/register_commands.cpp
  # 创建：构件（按专业）/ 曲线 / 基础体 / 轴网 / 楼层
  create/create_wall_command.cpp
  create/create_curtain_wall_command.cpp
  create/create_beam_command.cpp
  create/create_slab_command.cpp
  create/create_structural_wall_command.cpp
  create/create_foundation_command.cpp
  create/create_primitive_command.cpp
  create/create_curve_command.cpp
  create/create_sketch_command.cpp
  create/create_grid_axis_command.cpp
  create/create_storey_command.cpp
  create/create_text_command.cpp
  # 编辑：改特征参数 / 夹点 / 移动 / 定位 / 布尔 / 材质 / 贴图 / 轴网 / 楼层
  edit/add_feature_command.cpp
  edit/set_feature_param_command.cpp
  edit/edit_entity_grip_command.cpp
  edit/move_entities_command.cpp
  edit/set_location_command.cpp
  edit/boolean_command.cpp
  edit/set_material_command.cpp
  edit/update_material_command.cpp
  edit/replace_texture_command.cpp
  edit/update_grid_command.cpp
  edit/update_storeys_command.cpp
  edit/update_text_command.cpp
  # 删除
  delete/delete_entity_command.cpp
  delete/delete_grid_axis_command.cpp
  delete/delete_text_command.cpp
  # 导入
  import/import_drawing_command.cpp
  import/import_texture_command.cpp
)
add_library(tamias::command ALIAS tamias_command)
target_include_directories(tamias_command PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tamias_command PUBLIC tamias::document tamias::graphics tamias::modeling)
