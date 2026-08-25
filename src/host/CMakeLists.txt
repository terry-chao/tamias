add_library(tamias_host STATIC
  camera_controller.cpp
  command_arg_text.cpp
  session.cpp
)
add_library(tamias::host ALIAS tamias_host)
target_include_directories(tamias_host PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tamias_host PUBLIC tamias::command tamias::document tamias::core tamias::math)
