# AI 的协议层：纯 C++，不依赖 Qt / engine。
# 网络与 UI 在 src/app/ai（Qt 壳）；这里只做「拼请求 / 解回包 / 拼端点」，
# 所以能在 tamias_tests 里直接测（见 tests/meshy_protocol_tests.cpp）。
add_library(tamias_ai STATIC
  meshy_protocol.cpp
)
add_library(tamias::ai ALIAS tamias_ai)
target_include_directories(tamias_ai PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tamias_ai
  PUBLIC
    tamias::base
    tamias::mcp
)
