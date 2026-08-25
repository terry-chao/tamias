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

# Web UI（Vite + React + TypeScript，工程根在 web/）：构建 dist 并拷到 wasm 输出目录，
# 与 tamias_viewer.js/.wasm 同目录。找不到 node/npm 时跳过 UI（只产出 wasm 文件）。
find_program(TAMIAS_NODE_EXECUTABLE NAMES node)
find_program(TAMIAS_NPM_EXECUTABLE NAMES npm)
set(_web_ui_dir "${CMAKE_SOURCE_DIR}/web")

if(TAMIAS_NODE_EXECUTABLE AND TAMIAS_NPM_EXECUTABLE)
  set(_ui_bootstrap_cmds)
  if(NOT EXISTS "${_web_ui_dir}/node_modules")
    list(APPEND _ui_bootstrap_cmds
      COMMAND "${TAMIAS_NPM_EXECUTABLE}" --prefix "${_web_ui_dir}" ci
    )
  endif()
  add_custom_command(TARGET tamias_viewer POST_BUILD
    ${_ui_bootstrap_cmds}
    COMMAND "${TAMIAS_NPM_EXECUTABLE}" --prefix "${_web_ui_dir}" run build
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${_web_ui_dir}/dist"
            $<TARGET_FILE_DIR:tamias_viewer>
    COMMENT "Building web UI (Vite + React + TS) and copying into wasm output"
    VERBATIM
  )
else()
  message(STATUS "web UI: node/npm not found, skipping UI build (wasm files still produced)")
endif()

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
