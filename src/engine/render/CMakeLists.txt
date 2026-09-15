add_subdirectory(rhi)

add_library(tamias_render STATIC
  # 运行时：渲染线程、帧统计、命令录制、批次 / 实例 / 常驻缓存
  runtime/render_runtime.cpp
  # 场景：GPU 场景图 + 渲染快照（含快照 IO / 调试输出 / 金样）
  scene/scene_graph.cpp
  scene/render_scene.cpp
  scene/render_scene_debug.cpp
  scene/render_scene_io.cpp
  scene/render_scene_golden.cpp
  # 资源：材质、贴图（mip / 内置）、离散档位、IBL
  resource/ibl.cpp
  resource/texture_mips.cpp
  resource/builtin_textures.cpp
  # 调试回放：录制日志与播放器
  debug/scene_debug_player.cpp
  debug/scene_debug_log.cpp
)
add_library(tamias::render ALIAS tamias_render)
target_include_directories(tamias_render PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(tamias_render PRIVATE
  TAMIAS_SHADER_DIR="${TAMIAS_SHADER_OUTPUT_DIR}"
  TAMIAS_SHADER_SOURCE_DIR="${TAMIAS_SHADER_SOURCE_DIR}"
)
target_link_libraries(tamias_render PUBLIC tamias::rhi tamias::graphics tamias::io)
if(TARGET tamias::rhi_webgl)
  target_link_libraries(tamias_render PUBLIC tamias::rhi_webgl)
  target_compile_definitions(tamias_render PUBLIC TAMIAS_HAS_RHI_WEBGL=1)
endif()
if(TARGET tamias::rhi_webgpu)
  target_link_libraries(tamias_render PUBLIC tamias::rhi_webgpu)
  target_compile_definitions(tamias_render PUBLIC TAMIAS_HAS_RHI_WEBGPU=1)
endif()
