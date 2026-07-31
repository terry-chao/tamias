set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

add_executable(tamias
  main.cpp
  main_window.cpp
  home_page.cpp
  recent_files.cpp
  mesh_thumbnail.cpp
  app_settings.cpp
  i18n.cpp
  settings_dialog.cpp
  document_viewport.cpp
  view_cube_widget.cpp
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
endif()

target_include_directories(tamias PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_definitions(tamias PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

target_link_libraries(tamias
  PRIVATE
    tamias::core
    tamias::math
    tamias::graphics
    tamias::rhi
    tamias::render
    tamias::asset
    tamias::scene
    tamias::document
    tamias::io
    tamias::view
    tamias::modeling
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

if(TAMIAS_HAS_OCCT)
  target_compile_definitions(tamias PRIVATE TAMIAS_HAS_OCCT=1)
endif()

tamias_compile_shader(tamias mesh.vert)
tamias_compile_shader(tamias mesh.frag)

add_custom_command(TARGET tamias POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:tamias>/shaders
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${TAMIAS_SHADER_OUTPUT_DIR} $<TARGET_FILE_DIR:tamias>/shaders
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${TAMIAS_SHADER_SOURCE_DIR}/mesh.gl.vert
          ${TAMIAS_SHADER_SOURCE_DIR}/mesh.gl.frag
          $<TARGET_FILE_DIR:tamias>/shaders
  COMMENT "Copying shaders next to tamias"
)

if(TAMIAS_HAS_OCCT AND WIN32)
  # Copy toolkit + 3rdparty DLLs beside the exe so F5 / Explorer launch works
  # without requiring OCCT on PATH.
  add_custom_command(TARGET tamias POST_BUILD
    COMMAND ${CMAKE_COMMAND}
      -DDEST_DIR=$<TARGET_FILE_DIR:tamias>
      -DOCCT_BIN_DIR=$<IF:$<CONFIG:Debug>,${TAMIAS_OCCT_BIN_DIR_DEBUG},${TAMIAS_OCCT_BIN_DIR}>
      -DOCCT_3RDPARTY_ROOT=${TAMIAS_OCCT_3RDPARTY_ROOT}
      -P ${CMAKE_SOURCE_DIR}/cmake/TamiasCopyOcctRuntime.cmake
    COMMENT "Copying OCCT runtime DLLs next to tamias"
  )
endif()
