# 交互层：输入词汇 + drag 管理器。**不含 Qt**，也不依赖 document/command，
# 所以它既能在桌面壳里用，也能被 wasm 壳、插件宿主或测试单独复用。
# 具体 drag（夹点、框选…）落地时按需往下加依赖，别在这里一把梭。
add_library(tamias_interaction STATIC
  drag.cpp
  drag_manager.cpp
  event_source.cpp
)
add_library(tamias::interaction ALIAS tamias_interaction)
target_include_directories(tamias_interaction PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tamias_interaction
  PUBLIC
    tamias::base
    tamias::math
)
