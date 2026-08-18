# Copy imported shared-library DLLs next to an executable (Windows).
# vcpkg applocal usually does this too; this covers F5 / Explorer when
# TARGET_RUNTIME_DLLS is populated.

function(tamias_copy_runtime_dlls target)
  if(NOT WIN32)
    return()
  endif()
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E
      $<IF:$<BOOL:$<TARGET_RUNTIME_DLLS:${target}>>,copy_if_different,true>
      $<TARGET_RUNTIME_DLLS:${target}>
      $<TARGET_FILE_DIR:${target}>
    COMMAND_EXPAND_LISTS
    COMMENT "Copying runtime DLLs next to ${target}"
  )
endfunction()
