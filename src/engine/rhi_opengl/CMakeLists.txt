add_library(tamias_rhi_opengl STATIC
  opengl_device.cpp
  gl_loader.cpp
)
add_library(tamias::rhi_opengl ALIAS tamias_rhi_opengl)
target_include_directories(tamias_rhi_opengl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(tamias_rhi_opengl PUBLIC tamias::rhi tamias::graphics)

if(WIN32)
  target_compile_definitions(tamias_rhi_opengl PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(tamias_rhi_opengl PUBLIC opengl32)
else()
  find_package(OpenGL REQUIRED)
  find_package(X11 REQUIRED)
  target_link_libraries(tamias_rhi_opengl PUBLIC OpenGL::GL X11::X11)
endif()
