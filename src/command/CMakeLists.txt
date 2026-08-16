add_library(tamias_command STATIC
  create_wall_command.cpp
  create_primitive_command.cpp
)
add_library(tamias::command ALIAS tamias_command)
target_include_directories(tamias_command PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tamias_command PUBLIC tamias::entity tamias::graphics)
