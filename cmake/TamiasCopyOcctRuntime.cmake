# Copy OCCT + required 3rdparty DLLs next to an executable.
# cmake -DDEST_DIR=... -DOCCT_BIN_DIR=... -DOCCT_3RDPARTY_ROOT=... -P TamiasCopyOcctRuntime.cmake

if(NOT DEST_DIR OR NOT OCCT_BIN_DIR)
  message(FATAL_ERROR "TamiasCopyOcctRuntime: DEST_DIR and OCCT_BIN_DIR are required")
endif()

file(MAKE_DIRECTORY "${DEST_DIR}")

set(_occt_toolkit_dlls
  TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep
  TKGeomAlgo TKTopAlgo TKPrim TKFillet TKBO TKBool TKMesh TKShHealing TKHLR
  TKService TKV3d
  TKCDF TKLCAF TKCAF TKVCAF TKXCAF
  TKDE TKXSBase TKDESTEP TKDEIGES
)

foreach(_name IN LISTS _occt_toolkit_dlls)
  set(_src "${OCCT_BIN_DIR}/${_name}.dll")
  if(EXISTS "${_src}")
    file(COPY_FILE "${_src}" "${DEST_DIR}/${_name}.dll" ONLY_IF_DIFFERENT)
  else()
    message(WARNING "OCCT DLL missing: ${_src}")
  endif()
endforeach()

if(OCCT_3RDPARTY_ROOT AND EXISTS "${OCCT_3RDPARTY_ROOT}")
  set(_thirdparty_dlls
    "freetype-2.13.3-x64/bin/freetype.dll"
    "freeimage-3.18.0-x64/bin/FreeImage.dll"
    "ffmpeg-3.3.4-64/bin/avcodec-57.dll"
    "ffmpeg-3.3.4-64/bin/avformat-57.dll"
    "ffmpeg-3.3.4-64/bin/avutil-55.dll"
    "ffmpeg-3.3.4-64/bin/swscale-4.dll"
    "openvr-1.14.15-64/bin/win64/openvr_api.dll"
    "tbb-2021.13.0-x64/bin/tbb12.dll"
    "tbb-2021.13.0-x64/bin/tbbmalloc.dll"
  )
  # OCCT Debug toolkits live in .../bind and need jemalloc debug CRT build.
  if(OCCT_BIN_DIR MATCHES ".*/bind/?$")
    list(APPEND _thirdparty_dlls "jemalloc-vc14-64/debug/bin/jemalloc.dll")
  else()
    list(APPEND _thirdparty_dlls "jemalloc-vc14-64/bin/jemalloc.dll")
  endif()
  foreach(_rel IN LISTS _thirdparty_dlls)
    set(_src "${OCCT_3RDPARTY_ROOT}/${_rel}")
    if(EXISTS "${_src}")
      get_filename_component(_name "${_src}" NAME)
      file(COPY_FILE "${_src}" "${DEST_DIR}/${_name}" ONLY_IF_DIFFERENT)
    endif()
  endforeach()
endif()
