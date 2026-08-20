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
  document_viewport.cpp
  box_select_overlay.cpp
  handle_inspector.cpp
  property_panel.cpp
  view_cube_widget.cpp
  viewport_tool_strip.cpp
  rhi_backends.cpp
  resources.qrc
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

target_include_directories(tamias PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
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
    tamias::modeling
    tamias::command
    tamias::ifc
    Qt6::Widgets
    Qt6::Gui
    Qt6::Svg
)

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
