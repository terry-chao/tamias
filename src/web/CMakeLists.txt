add_executable(tamias_viewer
  viewer_main.cpp
  viewer_host.cpp
)
target_include_directories(tamias_viewer PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tamias_viewer
  PRIVATE
    tamias::core
    tamias::math
    tamias::graphics
    tamias::rhi
    tamias::rhi_webgl
    tamias::render
    tamias::io
    tamias::document
    tamias::entity
    tamias::modeling
)
target_compile_definitions(tamias_viewer PRIVATE TAMIAS_HAS_RHI_WEBGL=1)
target_compile_options(tamias_viewer PRIVATE -fexceptions)
target_link_options(tamias_viewer PRIVATE
  "SHELL:-sUSE_WEBGL2=1"
  "SHELL:-sFULL_ES3=1"
  "SHELL:-sALLOW_MEMORY_GROWTH=1"
  "SHELL:-sINITIAL_MEMORY=67108864"
  "SHELL:-sDISABLE_EXCEPTION_CATCHING=0"
  "SHELL:-sMODULARIZE=1"
  "SHELL:-sEXPORT_NAME=createTamiasViewer"
  "SHELL:-sENVIRONMENT=web"
  "SHELL:-sEXPORTED_RUNTIME_METHODS=['UTF8ToString']"
  --bind
)

add_custom_command(TARGET tamias_viewer POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_SOURCE_DIR}/web/index.html
          $<TARGET_FILE_DIR:tamias_viewer>/index.html
  COMMENT "Copying web viewer shell next to tamias_viewer"
)

add_custom_target(serve_viewer
  COMMAND "${CMAKE_SOURCE_DIR}/scripts/wasm-launch.cmd" "$<TARGET_FILE_DIR:tamias_viewer>"
  DEPENDS tamias_viewer
  USES_TERMINAL
  VERBATIM
)

add_custom_target(stop_viewer
  COMMAND powershell -NoProfile -File "${CMAKE_SOURCE_DIR}/scripts/wasm-stop.ps1"
  USES_TERMINAL
  VERBATIM
)
