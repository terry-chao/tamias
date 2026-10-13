# 服务端宿主：把 Session 暴露成扁平 C ABI 的**共享库**，由 server/csharp 的 .NET 服务壳
# P/Invoke 进来（决策见 docs/DECISION-SERVER-LANGUAGE.md）。
#
# 依赖是刻意压到最小的：
#   - 不链 Qt（桌面壳专属，src/ui/qt + src/app）
#   - 不链 plugin / nethost（服务壳自己就是 .NET 进程；原生侧再加载一遍 CoreCLR 是反的）
#   - rhi 只有 tamias::render 间接带进来，用来读 .trscn（render_scene_io.cpp）；
#     具体的图形后端（vulkan / opengl）不在这里，服务端不建交换链、不开窗口。
add_library(tamias_server SHARED
  server_api.cpp
  session_service.cpp
)
add_library(tamias::server ALIAS tamias_server)
target_include_directories(tamias_server PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(tamias_server PRIVATE TAMIAS_SERVER_BUILD=1)
# 导出面就是 server_api.h 里那几个函数，显式 dllexport；不要用
# WINDOWS_EXPORT_ALL_SYMBOLS 把静态依赖的符号一起导出去。
set_target_properties(tamias_server PROPERTIES CXX_VISIBILITY_PRESET default)
target_link_libraries(tamias_server PRIVATE
  tamias::host        # Session（无 Qt 会话层）
  tamias::command     # 命令注册表：register_commands
  tamias::document
  tamias::io          # load_obj_bytes
  tamias::render      # deserialize_render_scene（.trscn）
  tamias::modeling    # register_linked_kernels
  tamias::base
  tamias::math
)

# .NET 服务壳发布到原生库旁边，P/Invoke 按 "tamias_server" 在同目录解析。
if(TAMIAS_BUILD_SERVER_SHELL)
  tamias_publish_server(tamias_server)
endif()
