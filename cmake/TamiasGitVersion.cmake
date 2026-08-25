# Writes src/app generated/tamias_version.h from PROJECT_VERSION + git HEAD.
# cmake -DSOURCE_DIR=... -DOUTPUT_FILE=... -DTAMIAS_VERSION=0.1.0 -P TamiasGitVersion.cmake

if(NOT DEFINED SOURCE_DIR OR NOT DEFINED OUTPUT_FILE OR NOT DEFINED TAMIAS_VERSION)
  message(FATAL_ERROR "TamiasGitVersion.cmake needs SOURCE_DIR, OUTPUT_FILE, TAMIAS_VERSION")
endif()

set(_hash "")
set(_dirty "")

find_program(_git NAMES git git.exe)
if(_git AND EXISTS "${SOURCE_DIR}/.git")
  execute_process(
    COMMAND "${_git}" rev-parse --short HEAD
    WORKING_DIRECTORY "${SOURCE_DIR}"
    OUTPUT_VARIABLE _hash
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE _hash_result
  )
  if(NOT _hash_result EQUAL 0)
    set(_hash "")
  endif()
  if(_hash)
    execute_process(
      COMMAND "${_git}" status --porcelain --untracked-files=no
      WORKING_DIRECTORY "${SOURCE_DIR}"
      OUTPUT_VARIABLE _status
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )
    if(_status)
      set(_dirty "-dirty")
    endif()
  endif()
endif()

if(_hash)
  set(_full "${TAMIAS_VERSION} (${_hash}${_dirty})")
else()
  set(_full "${TAMIAS_VERSION}")
endif()

set(_content "#pragma once

#define TAMIAS_VERSION \"${TAMIAS_VERSION}\"
#define TAMIAS_GIT_HASH \"${_hash}${_dirty}\"
#define TAMIAS_VERSION_FULL \"${_full}\"
")

set(_old "")
if(EXISTS "${OUTPUT_FILE}")
  file(READ "${OUTPUT_FILE}" _old)
endif()
if(NOT _content STREQUAL "${_old}")
  get_filename_component(_dir "${OUTPUT_FILE}" DIRECTORY)
  file(MAKE_DIRECTORY "${_dir}")
  file(WRITE "${OUTPUT_FILE}" "${_content}")
endif()
