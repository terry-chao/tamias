# 可独立复用的「图形后端启动选择」组件：决策 + 探测 + 块名单 + 策略。
#
# 设计目标（见 README.md）：只依赖标准库和 engine/base/result.h（一个 12 行头），
# 不认识 Qt，也不认识任何具体渲染 API —— 后端的接入点在 device.h 的 IBackendDevice。
add_library(tamias_rhi_startup STATIC
  identity.cpp
  blocklist.cpp
  decision.cpp
  probe.cpp
  startup.cpp
)
add_library(tamias::rhi_startup ALIAS tamias_rhi_startup)
# 组件头用 "rhi_startup/xxx.h" / "engine/base/result.h" 引用，所以对外暴露 src 根。
target_include_directories(tamias_rhi_startup PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
