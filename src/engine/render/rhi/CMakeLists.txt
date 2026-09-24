add_library(tamias_rhi STATIC
  device_factory.cpp
  # GPU 身份 + 块名单/策略 + 启动探测与决策（见 docs/RENDERING.md §5）
  rhi_gpu_identity.cpp
  rhi_blocklist.cpp
  rhi_probe.cpp
  rhi_startup_decision.cpp
)
add_library(tamias::rhi ALIAS tamias_rhi)
target_include_directories(tamias_rhi PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tamias_rhi PUBLIC tamias::base tamias::math tamias::graphics)

if(TAMIAS_ENABLE_VULKAN_BACKEND)
  add_subdirectory(vulkan)
endif()

# OpenGL backend: isolation path, not shared with Vulkan views.
if(TAMIAS_ENABLE_OPENGL_BACKEND)
  add_subdirectory(opengl)
endif()

if(TAMIAS_ENABLE_WEBGL_BACKEND)
  add_subdirectory(webgl)
endif()

if(TAMIAS_ENABLE_WEBGPU_BACKEND)
  add_subdirectory(webgpu)
endif()
