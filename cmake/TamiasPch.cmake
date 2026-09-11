# Precompiled headers and optional unity builds.
#
# TAMIAS_ENABLE_PCH (ON): inject src/pch.h (STL). Extra headers via EXTRA.
#   Skipped on MSVC + Ninja Multi-Config: CMake emits cmake_pch.cxx.pch as a
#   phony implicit dep, so Ninja rebuilds every TU on every build.
# TAMIAS_UNITY_BUILD (OFF): batch TUs — faster clean builds, worse incremental.

option(TAMIAS_ENABLE_PCH "Use precompiled headers for faster builds." ON)
option(TAMIAS_UNITY_BUILD "Batch translation units (faster clean builds, slower incremental)." OFF)

set(TAMIAS_PCH_ACTIVE OFF)
if(TAMIAS_ENABLE_PCH AND NOT EMSCRIPTEN)
  if(MSVC AND CMAKE_GENERATOR MATCHES "Ninja Multi-Config")
    set(TAMIAS_PCH_ACTIVE OFF)
  else()
    set(TAMIAS_PCH_ACTIVE ON)
  endif()
endif()

# tamias_accelerate_target(<target> [EXTRA <header>...])
function(tamias_accelerate_target target)
  if(NOT TARGET "${target}")
    return()
  endif()

  cmake_parse_arguments(_t "" "" "EXTRA" ${ARGN})

  get_target_property(_type "${target}" TYPE)
  if(_type STREQUAL "INTERFACE_LIBRARY" OR _type STREQUAL "UTILITY")
    return()
  endif()

  if(TAMIAS_PCH_ACTIVE)
    target_precompile_headers("${target}" PRIVATE "${PROJECT_SOURCE_DIR}/src/pch.h")
    foreach(_hdr IN LISTS _t_EXTRA)
      target_precompile_headers("${target}" PRIVATE "${_hdr}")
    endforeach()
  endif()

  if(TAMIAS_UNITY_BUILD)
    set_target_properties("${target}" PROPERTIES
      UNITY_BUILD ON
      UNITY_BUILD_BATCH_SIZE 8
    )
  endif()
endfunction()

function(tamias_apply_build_acceleration)
  tamias_accelerate_target(tamias_core)
  tamias_accelerate_target(tamias_io)
  tamias_accelerate_target(tamias_modeling)
  tamias_accelerate_target(tamias_render)
  tamias_accelerate_target(tamias_document)
  tamias_accelerate_target(tamias_entity)
  tamias_accelerate_target(tamias_command)
  tamias_accelerate_target(tamias_host)
  tamias_accelerate_target(tamias_plugin)
  tamias_accelerate_target(tamias EXTRA "${PROJECT_SOURCE_DIR}/src/app/qt_pch.h")
  tamias_accelerate_target(tamias_tests EXTRA "<gtest/gtest.h>")
  tamias_accelerate_target(tamias_viewer)

  if(TAMIAS_UNITY_BUILD AND TARGET tamias_modeling AND TAMIAS_ENABLE_OCCT)
    set_source_files_properties(
      "${PROJECT_SOURCE_DIR}/src/engine/modeling/occt_shape_ops.cpp"
      "${PROJECT_SOURCE_DIR}/src/engine/modeling/occt_feature.cpp"
      "${PROJECT_SOURCE_DIR}/src/engine/modeling/occt_geom_builder.cpp"
      PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON
    )
  endif()
endfunction()
