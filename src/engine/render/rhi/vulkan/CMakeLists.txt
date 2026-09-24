add_library(tamias_rhi_vulkan STATIC
  vulkan_device.cpp
  vulkan_gpu_timing.cpp
  # volk 的实现（VOLK_IMPLEMENTATION 在 vulkan_device.cpp 里定义）。
  # 编在同一个 target 里，平台宏（VK_USE_PLATFORM_WIN32_KHR / XLIB）才和用它的 TU 一致。
  ${TAMIAS_THIRDPARTY_DIR}/volk.c
)
add_library(tamias::rhi_vulkan ALIAS tamias_rhi_vulkan)
target_include_directories(tamias_rhi_vulkan PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_include_directories(tamias_rhi_vulkan PRIVATE ${Vulkan_INCLUDE_DIRS})
target_link_libraries(tamias_rhi_vulkan
  PUBLIC
    tamias::rhi
    # 只要头文件：Vulkan 入口由 volk 在**运行时**加载，不链 vulkan-1.lib，
    # 这样没装 Vulkan runtime 的机器也能启动（回退到 OpenGL 后端）。
    GPUOpen::VulkanMemoryAllocator
)
if(WIN32)
  target_compile_definitions(tamias_rhi_vulkan PRIVATE VK_USE_PLATFORM_WIN32_KHR NOMINMAX WIN32_LEAN_AND_MEAN)
else()
  target_compile_definitions(tamias_rhi_vulkan PRIVATE VK_USE_PLATFORM_XLIB_KHR)
  find_package(X11 REQUIRED)
  target_link_libraries(tamias_rhi_vulkan PUBLIC X11::X11)
endif()
