add_executable(tamias_tests smoke_tests.cpp ifc_spatial_tree_tests.cpp plugin_host_tests.cpp
  scene_graph_tests.cpp host/session_test.cpp)
target_link_libraries(tamias_tests
  PRIVATE
    tamias::io
    tamias::document
    tamias::command
    tamias::host
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
