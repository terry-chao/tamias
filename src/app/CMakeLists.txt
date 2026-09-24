set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

add_executable(tamias
  main.cpp
  # 应用基础设施：设置、主题、翻译、最近文件、渲染后端登记
  base/app_settings.cpp
  base/theme.cpp
  base/i18n.cpp
  base/recent_files.cpp
  base/rhi_backends.cpp
  base/rhi_startup.cpp
  base/startup_guard.cpp
  base/log_buffer.cpp
  base/rhi_diagnostics.cpp
  # 窗口骨架：主窗口、欢迎页、Ribbon、通用对话框、提示条
  shell/main_window.cpp
  shell/home_page.cpp
  shell/ribbon_bar.cpp
  shell/ribbon_page.cpp
  shell/ribbon_group.cpp
  shell/toast.cpp
  shell/mesh_thumbnail.cpp
  shell/about_dialog.cpp
  shell/graphics_diagnostics_dialog.cpp
  shell/settings_dialog.cpp
  shell/settings_section.cpp
  shell/plugin_manager_dialog.cpp
  shell/plugin_prompt_dialog.cpp
  # 三维视口：画布本体 / 画布上的浮层 / 右侧工具列 / 录制回放
  viewport/canvas/document_viewport.cpp
  viewport/overlay/view_cube_widget.cpp
  viewport/overlay/box_select_overlay.cpp
  viewport/panel/viewport_tool_panel.cpp
  viewport/replay/replay_viewport.cpp
  # 构件界面：属性面板 / 绘制规格 / 构件显隐 / 楼层 / 轴网
  bim/properties/property_panel.cpp
  bim/components/draw_panel.cpp
  bim/components/section_preview_widget.cpp
  bim/components/component_specs.cpp
  bim/visibility/visibility_panel.cpp
  bim/visibility/entity_kind_catalog.cpp
  bim/floors/floor_panel.cpp
  bim/floors/floor_manager_panel.cpp
  bim/floors/floor_settings_dialog.cpp
  bim/grid/grid_settings_dialog.cpp
  # 二维图纸
  drawing/drawing_document.cpp
  drawing/drawing_view.cpp
  drawing/drawing_manager_panel.cpp
  drawing/drawing_settings_dialog.cpp
  drawing/drawing_import_dialog.cpp
  # 贴图
  texture/texture_image.cpp
  texture/texture_library_panel.cpp
  texture/texture_inspector_dialog.cpp
  # 调试 / 诊断：句柄检查、渲染场景检查、金样、计时
  debug/handle_inspector.cpp
  debug/render_scene_inspector.cpp
  debug/scene_debugger_window.cpp
  debug/pin_result_dialog.cpp
  debug/golden_test_runner.cpp
  debug/selection_accent_delegate.cpp
  debug/timing_panel.cpp
  debug/timing_timeline_widget.cpp
  resources.qrc
)

set(TAMIAS_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(TAMIAS_VERSION_HEADER "${TAMIAS_GENERATED_DIR}/tamias_version.h")
execute_process(
  COMMAND "${CMAKE_COMMAND}"
          -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
          -DOUTPUT_FILE=${TAMIAS_VERSION_HEADER}
          -DTAMIAS_VERSION=${PROJECT_VERSION}
          -P "${CMAKE_SOURCE_DIR}/cmake/TamiasGitVersion.cmake"
)
set(_tamias_version_depends "${CMAKE_SOURCE_DIR}/cmake/TamiasGitVersion.cmake")
if(EXISTS "${CMAKE_SOURCE_DIR}/.git/HEAD")
  list(APPEND _tamias_version_depends "${CMAKE_SOURCE_DIR}/.git/HEAD")
endif()
add_custom_command(
  OUTPUT "${TAMIAS_VERSION_HEADER}"
  COMMAND "${CMAKE_COMMAND}"
          -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
          -DOUTPUT_FILE=${TAMIAS_VERSION_HEADER}
          -DTAMIAS_VERSION=${PROJECT_VERSION}
          -P "${CMAKE_SOURCE_DIR}/cmake/TamiasGitVersion.cmake"
  DEPENDS ${_tamias_version_depends}
  COMMENT "Refreshing Tamias version header from git"
  VERBATIM
)
set_source_files_properties(main.cpp shell/about_dialog.cpp PROPERTIES
  OBJECT_DEPENDS "${TAMIAS_VERSION_HEADER}"
)

set(TAMIAS_TS_FILES
  ${CMAKE_SOURCE_DIR}/translations/tamias_zh_CN.ts
)
qt_add_translations(tamias
  TS_FILES ${TAMIAS_TS_FILES}
  RESOURCE_PREFIX "/i18n"
)

if(WIN32)
  target_sources(tamias PRIVATE app.rc)
  # Debug keeps a console for stderr logs; packaged Release/RelWithDebInfo do not.
  set_target_properties(tamias PROPERTIES WIN32_EXECUTABLE $<NOT:$<CONFIG:Debug>>)
endif()

target_include_directories(tamias PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/..
  ${TAMIAS_GENERATED_DIR}
)
target_compile_definitions(tamias PRIVATE
  NOMINMAX
  WIN32_LEAN_AND_MEAN
  TAMIAS_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_link_libraries(tamias
  PRIVATE
    tamias::base
    tamias::math
    tamias::graphics
    tamias::rhi
    tamias::render
    tamias::document
    tamias::io
    tamias::drawing
    tamias::modeling
    tamias::command
    tamias::plugin
    tamias::ifc
    Qt6::Widgets
    Qt6::Gui
    Qt6::Svg
)

# PDF 图纸需要 Qt PDF 模块（Qt 6 下是 GPLv3 / 商业双许可，未随本仓库分发）。
# 装上就自动启用；没装则 PDF 走"请导出 DXF/SVG/图片"的提示。
find_package(Qt6 QUIET COMPONENTS Pdf)
if(TARGET Qt6::Pdf)
  target_link_libraries(tamias PRIVATE Qt6::Pdf)
  target_compile_definitions(tamias PRIVATE TAMIAS_HAVE_QT_PDF=1)
  message(STATUS "Tamias PDF drawings: enabled (Qt6::Pdf)")
else()
  message(STATUS "Tamias PDF drawings: disabled (Qt6::Pdf not found)")
endif()

if(TARGET tamias::rhi_vulkan)
  target_link_libraries(tamias PRIVATE tamias::rhi_vulkan)
  target_compile_definitions(tamias PRIVATE TAMIAS_HAS_RHI_VULKAN=1)
endif()

if(TARGET tamias::rhi_opengl)
  target_link_libraries(tamias PRIVATE tamias::rhi_opengl)
  target_compile_definitions(tamias PRIVATE TAMIAS_HAS_RHI_OPENGL=1)
endif()

tamias_compile_hlsl(tamias mesh.vert.hlsl vs_6_0 mesh.vert)
tamias_compile_hlsl(tamias mesh.frag.hlsl ps_6_0 mesh.frag)
tamias_compile_hlsl(tamias sky.vert.hlsl vs_6_0 sky.vert)
tamias_compile_hlsl(tamias sky.frag.hlsl ps_6_0 sky.frag)
tamias_compile_hlsl(tamias grid.vert.hlsl vs_6_0 grid.vert)
tamias_compile_hlsl(tamias grid.frag.hlsl ps_6_0 grid.frag)
tamias_compile_hlsl(tamias overlay.vert.hlsl vs_6_0 overlay.vert)
tamias_compile_hlsl(tamias overlay.frag.hlsl ps_6_0 overlay.frag)
tamias_compile_hlsl(tamias text.vert.hlsl vs_6_0 text.vert)
tamias_compile_hlsl(tamias text.frag.hlsl ps_6_0 text.frag)

add_custom_command(TARGET tamias POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E rm -rf $<TARGET_FILE_DIR:tamias>/shaders
  COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:tamias>/shaders
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${TAMIAS_SHADER_OUTPUT_DIR} $<TARGET_FILE_DIR:tamias>/shaders
  COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:tamias>/assets/samples
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/assets/samples
          $<TARGET_FILE_DIR:tamias>/assets/samples
  COMMENT "Copying shaders and sample assets next to tamias"
)

tamias_copy_runtime_dlls(tamias)

if(WIN32)
  include(${CMAKE_SOURCE_DIR}/cmake/TamiasDeployQt.cmake)
  tamias_deploy_qt_runtime(tamias)
endif()

tamias_publish_csharp(tamias)
