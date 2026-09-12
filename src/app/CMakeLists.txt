set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

add_executable(tamias
  main.cpp
  main_window.cpp
  ribbon_bar.cpp
  ribbon_page.cpp
  ribbon_group.cpp
  home_page.cpp
  recent_files.cpp
  mesh_thumbnail.cpp
  app_settings.cpp
  i18n.cpp
  settings_dialog.cpp
  settings_section.cpp
  plugin_manager_dialog.cpp
  plugin_prompt_dialog.cpp
  about_dialog.cpp
  pin_result_dialog.cpp
  selection_accent_delegate.cpp
  render_scene_inspector.cpp
  document_viewport.cpp
  box_select_overlay.cpp
  replay_viewport.cpp
  scene_debugger_window.cpp
  golden_test_runner.cpp
  handle_inspector.cpp
  property_panel.cpp
  draw_panel.cpp
  drawing_document.cpp
  drawing_view.cpp
  section_preview_widget.cpp
  component_specs.cpp
  texture_library_panel.cpp
  texture_inspector_dialog.cpp
  texture_image.cpp
  timing_panel.cpp
  timing_timeline_widget.cpp
  view_cube_widget.cpp
  viewport_tool_panel.cpp
  entity_kind_catalog.cpp
  visibility_panel.cpp
  theme.cpp
  rhi_backends.cpp
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
set_source_files_properties(main.cpp about_dialog.cpp PROPERTIES
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
    tamias::core
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
