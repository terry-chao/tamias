cmake_minimum_required(VERSION 3.24)

add_subdirectory(core)
add_subdirectory(math)
add_subdirectory(graphics)
add_subdirectory(rhi)

if(TAMIAS_ENABLE_VULKAN_BACKEND)
  add_subdirectory(rhi_vulkan)
endif()

add_subdirectory(render)

# OpenGL backend: isolation path, not shared with Vulkan views.
if(TAMIAS_ENABLE_OPENGL_BACKEND)
  add_subdirectory(rhi_opengl)
endif()

add_subdirectory(modeling)
add_subdirectory(io)
