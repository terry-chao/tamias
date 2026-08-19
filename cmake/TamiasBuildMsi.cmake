# Build Tamias-*.msi from the CMake install tree. Invoked by the tamias_msi target.

cmake_minimum_required(VERSION 3.24)

if(NOT TAMIAS_BINARY_DIR OR NOT TAMIAS_SOURCE_DIR)
  message(FATAL_ERROR "TamiasBuildMsi.cmake needs TAMIAS_BINARY_DIR and TAMIAS_SOURCE_DIR")
endif()
if(NOT TAMIAS_CONFIG)
  set(TAMIAS_CONFIG Release)
endif()
if(NOT TAMIAS_VERSION)
  set(TAMIAS_VERSION 0.1.0)
endif()

if(TAMIAS_CONFIG STREQUAL "Debug")
  message(WARNING "Packaging Debug; ship Release or RelWithDebInfo instead.")
endif()

set(TAMIAS_WIX_VERSION 5.0.2)
set(TAMIAS_PACKAGE_DIR "${TAMIAS_BINARY_DIR}/package")
set(TAMIAS_PAYLOAD_DIR "${TAMIAS_PACKAGE_DIR}/payload")
set(TAMIAS_WIX_DIR "${TAMIAS_BINARY_DIR}/tools/wix")
set(TAMIAS_WIX "${TAMIAS_WIX_DIR}/wix.exe")
set(TAMIAS_MSI "${TAMIAS_PACKAGE_DIR}/Tamias-${TAMIAS_VERSION}-win64.msi")

find_program(TAMIAS_DOTNET NAMES dotnet REQUIRED)

if(NOT EXISTS "${TAMIAS_WIX}")
  message(STATUS "Installing WiX ${TAMIAS_WIX_VERSION} into ${TAMIAS_WIX_DIR}")
  execute_process(
    COMMAND "${TAMIAS_DOTNET}" tool install wix
            --tool-path "${TAMIAS_WIX_DIR}"
            --version ${TAMIAS_WIX_VERSION}
    RESULT_VARIABLE _wix_install)
  if(_wix_install)
    message(FATAL_ERROR "dotnet tool install wix failed (${_wix_install})")
  endif()
endif()

execute_process(
  COMMAND "${TAMIAS_WIX}" extension add "WixToolset.UI.wixext/${TAMIAS_WIX_VERSION}"
  RESULT_VARIABLE _wix_ext)
if(_wix_ext)
  message(FATAL_ERROR "wix extension add WixToolset.UI.wixext failed (${_wix_ext})")
endif()

message(STATUS "Staging install payload to ${TAMIAS_PAYLOAD_DIR}")
file(REMOVE_RECURSE "${TAMIAS_PAYLOAD_DIR}")
file(MAKE_DIRECTORY "${TAMIAS_PAYLOAD_DIR}")
execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${TAMIAS_BINARY_DIR}"
          --config "${TAMIAS_CONFIG}"
          --prefix "${TAMIAS_PAYLOAD_DIR}"
  RESULT_VARIABLE _install)
if(_install)
  message(FATAL_ERROR "cmake --install failed (${_install})")
endif()

set(_exe "${TAMIAS_PAYLOAD_DIR}/tamias.exe")
if(NOT EXISTS "${_exe}")
  message(FATAL_ERROR "Install payload is missing tamias.exe (${_exe})")
endif()

file(MAKE_DIRECTORY "${TAMIAS_PACKAGE_DIR}")
message(STATUS "Building ${TAMIAS_MSI}")
set(_wxs "${TAMIAS_SOURCE_DIR}/packaging/windows/tamias.wxs")
execute_process(
  COMMAND "${TAMIAS_WIX}" build "${_wxs}"
          -arch x64
          -ext WixToolset.UI.wixext
          -d "ProductVersion=${TAMIAS_VERSION}"
          -b "Payload=${TAMIAS_PAYLOAD_DIR}"
          -b "Packaging=${TAMIAS_SOURCE_DIR}/packaging/windows"
          -b "Branding=${TAMIAS_SOURCE_DIR}/assets/branding"
          -pdbtype none
          -o "${TAMIAS_MSI}"
  RESULT_VARIABLE _wix_build)
if(_wix_build)
  message(FATAL_ERROR "wix build failed (${_wix_build})")
endif()

message(STATUS "MSI: ${TAMIAS_MSI}")
