set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

add_executable(tamias
  main.cpp
  main_window.cpp
  document_viewport.cpp
  rhi_backends.cpp
)

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

tamias_compile_shader(tamias mesh.vert)
tamias_compile_shader(tamias mesh.frag)

add_custom_command(TARGET tamias POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:tamias>/shaders
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${TAMIAS_SHADER_OUTPUT_DIR} $<TARGET_FILE_DIR:tamias>/shaders
  COMMENT "Copying SPIR-V shaders next to tamias"
)
