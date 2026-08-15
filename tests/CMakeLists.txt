add_executable(tamias_tests smoke_tests.cpp)
target_link_libraries(tamias_tests
  PRIVATE
    tamias::io
    tamias::entity
    tamias::command
    tamias::modeling
    tamias::render
    GTest::gtest_main
)
if(TAMIAS_HAS_OCCT)
  target_compile_definitions(tamias_tests PRIVATE TAMIAS_HAS_OCCT=1)
endif()
include(GoogleTest)

# PRE_TEST avoids build-time discovery failing when OCCT DLLs are not on PATH.
# ENVIRONMENT_MODIFICATION is required: ENVIRONMENT treats ';' as list separators.
set(_tamias_test_env_mod "")
if(TAMIAS_HAS_OCCT AND TAMIAS_OCCT_RUNTIME_PATH)
  foreach(_p IN LISTS TAMIAS_OCCT_RUNTIME_PATH)
    list(APPEND _tamias_test_env_mod "PATH=path_list_prepend:${_p}")
  endforeach()
endif()

gtest_discover_tests(tamias_tests
  DISCOVERY_MODE PRE_TEST
  PROPERTIES ENVIRONMENT_MODIFICATION "${_tamias_test_env_mod}"
)
if(TAMIAS_HAS_OCCT AND WIN32)
  add_custom_command(TARGET tamias_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND}
      -DDEST_DIR=$<TARGET_FILE_DIR:tamias_tests>
      -DOCCT_BIN_DIR=$<IF:$<CONFIG:Debug>,${TAMIAS_OCCT_BIN_DIR_DEBUG},${TAMIAS_OCCT_BIN_DIR}>
      -DOCCT_3RDPARTY_ROOT=${TAMIAS_OCCT_3RDPARTY_ROOT}
      -P ${CMAKE_SOURCE_DIR}/cmake/TamiasCopyOcctRuntime.cmake
    COMMENT "Copying OCCT runtime DLLs next to tamias_tests"
  )
endif()
