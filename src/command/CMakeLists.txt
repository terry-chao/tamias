add_library(tamias_command STATIC
  add_feature_command.cpp
  boolean_command.cpp
  create_wall_command.cpp
  create_beam_command.cpp
  create_primitive_command.cpp
  create_sketch_command.cpp
  delete_entity_command.cpp
  set_feature_param_command.cpp
  set_material_command.cpp
  command_system.cpp
  register_commands.cpp
)
add_library(tamias::command ALIAS tamias_command)
target_include_directories(tamias_command PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tamias_command PUBLIC tamias::document tamias::graphics tamias::modeling)
