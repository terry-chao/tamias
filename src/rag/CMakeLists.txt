# 检索核心：纯 C++，只依赖标准库与 mcp 的 JSON。
# **不**链接 Qt / engine / OCCT —— 否则 wasm、无头、CI 全被拖累（PLAN-RAG 不变量 3）。
# 语料由 scripts/rag/build_index.py 构建期产出，落在 resources/rag/。
add_library(tamias_rag STATIC
  bm25_index.cpp
  docs_mcp_backend.cpp
  index_source.cpp
  lexical_provider.cpp
  tokenizer.cpp
)
add_library(tamias::rag ALIAS tamias_rag)
target_include_directories(tamias_rag PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tamias_rag PUBLIC tamias::mcp)

# 拿不到 git（从 tar 包构建、CI 里没装）就留空，此时不做索引新鲜度判断。
execute_process(
  COMMAND git rev-parse --short HEAD
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE TAMIAS_GIT_REV
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)

# 开发机上直接跑构建产物时，靠这个兜住索引与别名表的位置（与 src/app 同一套写法）。
# PRIVATE：只有 index_source.cpp 用它，别把构建机的绝对路径泄漏给下游 target。
target_compile_definitions(tamias_rag PRIVATE
  TAMIAS_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  TAMIAS_GIT_REV="${TAMIAS_GIT_REV}"
)

# 独立评测入口：金标集必须跑真代码，不能另写一份 Python 分词（PLAN-RAG §9）。
add_executable(tamias_rag_cli rag_cli.cpp)
target_link_libraries(tamias_rag_cli PRIVATE tamias::rag)
