# MCP 门面：纯 C++，不依赖 Qt / engine / Occt。
# 协议与工具描述在这里，具体能力由 McpBackend 的实现提供（见 src/host/session_mcp_backend）。
add_library(tamias_mcp STATIC
  composite_backend.cpp
  json.cpp
  mcp_backend.cpp
  mcp_server.cpp
)
add_library(tamias::mcp ALIAS tamias_mcp)
target_include_directories(tamias_mcp PUBLIC ${CMAKE_SOURCE_DIR}/src)
