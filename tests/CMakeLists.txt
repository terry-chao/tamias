add_executable(tamias_tests smoke_tests.cpp location_tests.cpp ifc_spatial_tree_tests.cpp plugin_host_tests.cpp
  scene_graph_tests.cpp ibl_tests.cpp host/session_test.cpp render_scene_tests.cpp
  scene_debug_tests.cpp
  command_dispatch_tests.cpp feature_boolean_tests.cpp camera_controller_tests.cpp import_tests.cpp
  command_echo_tests.cpp
  command_transaction_tests.cpp
  timing_session_tests.cpp mesh_intern_tests.cpp mesh_lod_tests.cpp texture_library_tests.cpp
  dxf_reader_tests.cpp wall_join_tests.cpp grid_tests.cpp drawing_import_tests.cpp
  drawing_placement_tests.cpp
  edge_naming_tests.cpp kernel_conformance_tests.cpp dwf_reader_tests.cpp
  text_layout_tests.cpp glyph_atlas_tests.cpp stb_font_tests.cpp text_quad_builder_tests.cpp
  length_text_tests.cpp grid_dimensions_tests.cpp annotation_layout_tests.cpp
  text_annotation_tests.cpp vulkan_loader_tests.cpp rhi_startup_tests.cpp
  offscreen_render_tests.cpp scene_capture_tests.cpp edit_operations_tests.cpp
  interaction_tests.cpp mcp_server_tests.cpp mcp_session_backend_tests.cpp
  rag_tokenizer_tests.cpp rag_bm25_tests.cpp rag_lexical_provider_tests.cpp
  rag_composite_tests.cpp rag_docs_backend_tests.cpp)

# Vulkan 加载路径（volk）的冒烟测试：只在建了 Vulkan 后端时链进来。
if(TARGET tamias::rhi_vulkan)
  target_link_libraries(tamias_tests PRIVATE tamias::rhi_vulkan)
  target_compile_definitions(tamias_tests PRIVATE TAMIAS_HAS_RHI_VULKAN=1)
endif()

# OpenGL 后端的离屏渲染测试也走真机（有 GL 上下文就测，没有就跳过）。
if(TARGET tamias::rhi_opengl)
  target_link_libraries(tamias_tests PRIVATE tamias::rhi_opengl)
  target_compile_definitions(tamias_tests PRIVATE TAMIAS_HAS_RHI_OPENGL=1)
endif()
target_link_libraries(tamias_tests
  PRIVATE
    tamias::io
    tamias::drawing
    tamias::document
    tamias::command
    tamias::mcp
    tamias::rag
    tamias::host
    tamias::interaction
    tamias::plugin
    tamias::modeling
    tamias::render
    tamias::ifc
    GTest::gtest_main
)
target_compile_definitions(tamias_tests PRIVATE TAMIAS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
include(GoogleTest)

# PRE_TEST avoids build-time discovery failing when OCCT DLLs are not on PATH.
# ENVIRONMENT_MODIFICATION is required: ENVIRONMENT treats ';' as list separators.
set(_tamias_test_env_mod "")
if(TAMIAS_OCCT_RUNTIME_PATH)
  foreach(_p IN LISTS TAMIAS_OCCT_RUNTIME_PATH)
    list(APPEND _tamias_test_env_mod "PATH=path_list_prepend:${_p}")
  endforeach()
endif()

gtest_discover_tests(tamias_tests
  DISCOVERY_MODE PRE_TEST
  PROPERTIES ENVIRONMENT_MODIFICATION "${_tamias_test_env_mod}"
)
tamias_copy_runtime_dlls(tamias_tests)
tamias_copy_nethost(tamias_tests)

# ---------------------------------------------------------------------------
# C++20 模块试点（见 src/engine/math/tamias.math.cppm）。
#
# 单独一个 target 而不是塞进 tamias_tests：既有测试 target 挂着 PCH、unity、
# 目标文件缓存等加速手段，混在一起会污染“模块能不能用”的实验结论。
#
# 两侧探针必须分处不同 TU：header_side.cpp 只 include 头文件，module_side.cpp
# 只 import tamias.math。同一个 TU 里混用会让同名实体同时挂在全局模块和具名模块上。
if(TARGET tamias::math_module)
  add_executable(tamias_math_module_tests
    math_module_parity_tests.cpp
    math_module/header_side.cpp
    math_module/module_side.cpp
    math_module/parity_check.cpp)
  # 必须显式打开扫描：仓库的 cmake_minimum_required(VERSION 3.24) 让 CMP0155
  # 停留在 OLD，消费者 TU 默认不扫描 import，于是 module_side.cpp 会以
  # “unscanned” 规则编译（CMake 不给它 BMI 引用）并报 C2230 “无法找到模块”。
  # 试点只在需要的 target 上开，避免全项目每个 TU 多一遍扫描（那是整体迁移时才付的账）。
  set_property(TARGET tamias_math_module_tests PROPERTY CXX_SCAN_FOR_MODULES ON)
  target_include_directories(tamias_math_module_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(tamias_math_module_tests
    PRIVATE
      tamias::math
      tamias::math_module
      GTest::gtest_main
  )
  gtest_discover_tests(tamias_math_module_tests DISCOVERY_MODE PRE_TEST)
  tamias_copy_runtime_dlls(tamias_math_module_tests)
endif()

# ---------------------------------------------------------------------------
# RAG 金标集门禁（PLAN-RAG §3 M4）：驱动真正的 tamias_rag_cli 算 recall@5，
# 低于门槛即失败。
#
# 评测**不重实现分词** —— Python 与 C++ 双实现必然漂移，那样测过的不一定是发出去的。
# 缺 Python 或缺索引就跳过而不是失败：否则没装 Python 的开发机上整个套件跑不起来。
find_package(Python3 COMPONENTS Interpreter QUIET)
if(NOT EMSCRIPTEN AND Python3_Interpreter_FOUND
   AND EXISTS "${CMAKE_SOURCE_DIR}/resources/rag/chunks.jsonl")
  add_dependencies(tamias_tests tamias_rag_cli)
  add_test(NAME rag_golden
    COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_SOURCE_DIR}/scripts/rag/eval.py"
            --cli $<TARGET_FILE:tamias_rag_cli>
            --golden "${CMAKE_SOURCE_DIR}/tests/rag/golden_qa.jsonl"
            --show-misses)
endif()
