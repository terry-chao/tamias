add_library(tamias_command INTERFACE)
add_library(tamias::command ALIAS tamias_command)
target_include_directories(tamias_command INTERFACE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tamias_command INTERFACE tamias::core)
