cmake_minimum_required(VERSION 3.24)

add_subdirectory(rhi)

if(TAMIAS_ENABLE_VULKAN_BACKEND)
  add_subdirectory(rhi_vulkan)
endif()

add_subdirectory(render)

# OpenGL backend stub (M4): isolation path, not shared with Vulkan views.
if(TAMIAS_ENABLE_OPENGL_BACKEND)
  add_subdirectory(rhi_opengl)
endif()
