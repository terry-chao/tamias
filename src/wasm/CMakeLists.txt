add_executable(tamias_viewer
  viewer_main.cpp
  viewer_host.cpp
)
target_include_directories(tamias_viewer PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tamias_viewer
  PRIVATE
    tamias::base
    tamias::math
    tamias::graphics
    tamias::rhi
    tamias::render
    tamias::io
    tamias::document
    tamias::entity
    tamias::modeling
    tamias::host
)
if(TARGET tamias::rhi_webgpu)
  target_link_libraries(tamias_viewer PRIVATE tamias::rhi_webgpu)
  target_compile_definitions(tamias_viewer PRIVATE TAMIAS_HAS_RHI_WEBGPU=1)
endif()
if(TARGET tamias::rhi_webgl)
  target_link_libraries(tamias_viewer PRIVATE tamias::rhi_webgl)
  target_compile_definitions(tamias_viewer PRIVATE TAMIAS_HAS_RHI_WEBGL=1)
endif()
target_compile_options(tamias_viewer PRIVATE -fexceptions)
target_link_options(tamias_viewer PRIVATE
  "SHELL:-sASYNCIFY=1"
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
#
# 这一步必须是带 OUTPUT / DEPENDS 的 custom command，**不能**只挂在 tamias_viewer 的
# POST_BUILD 上：只改 TS 时 C++ 目标是最新的、ninja 不重链，POST_BUILD 于是根本不跑，
# UI 改了却不生效（踩过一次：加了按钮却一直是旧的 bundle）。
find_program(TAMIAS_NODE_EXECUTABLE NAMES node)
find_program(TAMIAS_NPM_EXECUTABLE NAMES npm)
set(_web_ui_dir "${CMAKE_SOURCE_DIR}/web")
# 顶层把 CMAKE_RUNTIME_OUTPUT_DIRECTORY 设为 <build>/bin，wasm 走单配置 Ninja，
# 所以这里能直接写出来。不用 $<TARGET_FILE_DIR:tamias_viewer>：那会让 tamias_web_ui
# 反过来依赖 tamias_viewer，形成依赖环。
set(_wasm_bin_dir "${CMAKE_BINARY_DIR}/bin")

if(TAMIAS_NODE_EXECUTABLE AND TAMIAS_NPM_EXECUTABLE)
  set(_ui_bootstrap_cmds)
  if(NOT EXISTS "${_web_ui_dir}/node_modules")
    list(APPEND _ui_bootstrap_cmds
      COMMAND "${TAMIAS_NPM_EXECUTABLE}" --prefix "${_web_ui_dir}" ci
    )
  endif()
  # 只拷 UI 自己的产物（index.html + assets/）。**不能**整目录 copy_directory：
  # web/dist 里还带着 vite 从 web/public 复制过去的 tamias_viewer.js/.wasm，
  # 整目录覆盖会把刚链接好的 wasm 换成 public 里的旧副本——改了 C++ 却一直在跑
  # 旧 wasm（踩过一次：wgpuSurfacePresent 改掉了却仍旧 abort）。
  file(GLOB_RECURSE _web_ui_sources CONFIGURE_DEPENDS
    "${_web_ui_dir}/src/*"
    "${_web_ui_dir}/index.html"
    "${_web_ui_dir}/package.json"
    "${_web_ui_dir}/vite.config.ts"
    "${_web_ui_dir}/tsconfig.json"
  )
  set(_web_ui_stamp "${CMAKE_CURRENT_BINARY_DIR}/tamias_web_ui.stamp")
  add_custom_command(
    OUTPUT "${_web_ui_stamp}"
    ${_ui_bootstrap_cmds}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_wasm_bin_dir}"
    COMMAND "${TAMIAS_NPM_EXECUTABLE}" --prefix "${_web_ui_dir}" run build
    # vite 产物是内容哈希命名的，copy_directory 不删旧文件；先清掉，免得
    # build/wasm/bin/assets 里越堆越多过期 bundle。
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${_wasm_bin_dir}/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_web_ui_dir}/dist/index.html" "${_wasm_bin_dir}/index.html"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${_web_ui_dir}/dist/assets" "${_wasm_bin_dir}/assets"
    COMMAND ${CMAKE_COMMAND} -E touch "${_web_ui_stamp}"
    DEPENDS ${_web_ui_sources}
    COMMENT "Building web UI (Vite + React + TS) and copying into wasm output"
    VERBATIM
  )
  add_custom_target(tamias_web_ui DEPENDS "${_web_ui_stamp}")
  add_dependencies(tamias_viewer tamias_web_ui)
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
