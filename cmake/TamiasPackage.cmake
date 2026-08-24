# Stage Tamias next to tamias.exe and wrap that tree in a WiX MSI (Windows).

cmake_policy(SET CMP0177 NEW)

if(NOT WIN32)
  return()
endif()
if(NOT TARGET tamias)
  return()
endif()

install(TARGETS tamias RUNTIME DESTINATION ".")

install(DIRECTORY "${TAMIAS_SHADER_OUTPUT_DIR}/"
  DESTINATION shaders
  FILES_MATCHING PATTERN "*.spv")

install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets/samples"
  DESTINATION assets)

install(DIRECTORY "$<TARGET_FILE_DIR:tamias>/managed/"
  DESTINATION managed
  OPTIONAL)
install(DIRECTORY "$<TARGET_FILE_DIR:tamias>/plugins/"
  DESTINATION plugins
  OPTIONAL)

# POST_BUILD already copies OCCT / freetype / Qt next to the exe. Reuse that
# set so transitive DLLs (freetype, zlib, …) are not dropped.
install(CODE "
  file(GLOB _dlls \"$<TARGET_FILE_DIR:tamias>/*.dll\")
  foreach(_dll IN LISTS _dlls)
    file(INSTALL DESTINATION \"\${CMAKE_INSTALL_PREFIX}\" TYPE FILE FILES \"\${_dll}\")
  endforeach()
")

# App-local MSVC CRT so the MSI does not depend on a separate VC++ redist.
install(CODE [[
  if(DEFINED ENV{VCToolsRedistDir})
    file(GLOB _crt "$ENV{VCToolsRedistDir}/x64/Microsoft.VC*.CRT/*.dll")
    foreach(_dll IN LISTS _crt)
      file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}" TYPE FILE FILES "${_dll}")
    endforeach()
  endif()
]])

qt_generate_deploy_app_script(
  TARGET tamias
  OUTPUT_SCRIPT TAMIAS_QT_DEPLOY_SCRIPT
  NO_UNSUPPORTED_PLATFORM_ERROR
  DEPLOY_TOOL_OPTIONS --no-compiler-runtime)
install(CODE [[
  set(QT_DEPLOY_BIN_DIR ".")
  set(QT_DEPLOY_PLUGINS_DIR ".")
]])
install(SCRIPT ${TAMIAS_QT_DEPLOY_SCRIPT})

option(TAMIAS_BUILD_MSI "Add the tamias_msi target (requires the .NET SDK)." ON)
if(TAMIAS_BUILD_MSI)
  add_custom_target(tamias_msi
    COMMAND ${CMAKE_COMMAND}
            -DTAMIAS_BINARY_DIR=${CMAKE_BINARY_DIR}
            -DTAMIAS_SOURCE_DIR=${CMAKE_SOURCE_DIR}
            -DTAMIAS_CONFIG=$<CONFIG>
            -DTAMIAS_VERSION=${PROJECT_VERSION}
            -P ${CMAKE_SOURCE_DIR}/cmake/TamiasBuildMsi.cmake
    DEPENDS tamias
    COMMENT "Building Tamias MSI"
    VERBATIM)
  set_property(TARGET tamias_msi PROPERTY FOLDER "packaging")
endif()
