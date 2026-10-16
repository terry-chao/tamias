add_library(tamias_rhi_opengl STATIC
  opengl_device.cpp
  gl_loader.cpp
  # GL 入口点整表由 vendored 的 glad 提供（生成参数见 3rdparty/glad/README.md）。
  # 和 vulkan 那边的 volk.c 一个道理：编在同一个 target 里，平台宏才和用它的 TU 一致。
  ${TAMIAS_THIRDPARTY_DIR}/glad/src/gl.c
)
add_library(tamias::rhi_opengl ALIAS tamias_rhi_opengl)
target_include_directories(tamias_rhi_opengl PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_include_directories(tamias_rhi_opengl PRIVATE ${TAMIAS_THIRDPARTY_DIR}/glad/include)
target_link_libraries(tamias_rhi_opengl PUBLIC tamias::rhi tamias::graphics)

if(WIN32)
  target_compile_definitions(tamias_rhi_opengl PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(tamias_rhi_opengl PUBLIC opengl32)
else()
  find_package(OpenGL REQUIRED)
  find_package(X11 REQUIRED)
  target_link_libraries(tamias_rhi_opengl PUBLIC OpenGL::GL X11::X11)
endif()
