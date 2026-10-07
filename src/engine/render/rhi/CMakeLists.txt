add_library(tamias_rhi STATIC
  device_factory.cpp
  # 把 RHIDevice 适配成启动选择组件的 IBackendDevice。
  # 选择逻辑（GPU 身份 / 块名单 / 策略 / 决策 / 探测）本身在 tamias::rhi_startup，
  # 见 src/rhi_startup/ 与 docs/RHI-STARTUP.md。
  rhi_selection_device.cpp
)
add_library(tamias::rhi ALIAS tamias_rhi)
target_include_directories(tamias_rhi PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tamias_rhi PUBLIC
  tamias::base tamias::math tamias::graphics tamias::rhi_startup)

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
