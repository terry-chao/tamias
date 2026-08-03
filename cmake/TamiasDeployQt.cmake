# Deploy Qt runtime DLLs/plugins next to an executable (Windows).
# Include after find_package(Qt6 ...) and the target exists.

function(tamias_deploy_qt_runtime target)
  if(NOT WIN32)
    return()
  endif()
  if(NOT TARGET Qt6::qmake)
    return()
  endif()

  get_target_property(_qmake_location Qt6::qmake IMPORTED_LOCATION)
  if(NOT _qmake_location)
    return()
  endif()
  get_filename_component(_qt_bin_dir "${_qmake_location}" DIRECTORY)
  find_program(TAMIAS_WINDEPLOYQT windeployqt HINTS "${_qt_bin_dir}")
  if(NOT TAMIAS_WINDEPLOYQT)
    message(WARNING "windeployqt not found; Qt DLLs will not be copied next to ${target}")
    return()
  endif()

  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND "${TAMIAS_WINDEPLOYQT}"
            $<IF:$<CONFIG:Debug>,--debug,--release>
            --no-compiler-runtime
            "$<TARGET_FILE:${target}>"
    COMMENT "Deploying Qt runtime next to ${target}"
    VERBATIM
  )
endfunction()
