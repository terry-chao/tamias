add_library(tamias_plugin STATIC
  command_arg_text.cpp
  plugin_host.cpp
  csharp_runtime.cpp
)
add_library(tamias::plugin ALIAS tamias_plugin)
target_include_directories(tamias_plugin PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tamias_plugin PUBLIC tamias::command tamias::document tamias::core)

if(TAMIAS_NETHOST_FOUND)
  target_compile_definitions(tamias_plugin PRIVATE TAMIAS_HAS_NETHOST=1)
  target_link_libraries(tamias_plugin PUBLIC tamias_nethost)
endif()
