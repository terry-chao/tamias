add_library(tamias_rhi_vulkan STATIC vulkan_device.cpp)
add_library(tamias::rhi_vulkan ALIAS tamias_rhi_vulkan)
target_include_directories(tamias_rhi_vulkan PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(tamias_rhi_vulkan
  PUBLIC
    tamias::rhi
    Vulkan::Vulkan
    GPUOpen::VulkanMemoryAllocator
)
if(WIN32)
  target_compile_definitions(tamias_rhi_vulkan PRIVATE VK_USE_PLATFORM_WIN32_KHR NOMINMAX WIN32_LEAN_AND_MEAN)
else()
  target_compile_definitions(tamias_rhi_vulkan PRIVATE VK_USE_PLATFORM_XLIB_KHR)
  find_package(X11 REQUIRED)
  target_link_libraries(tamias_rhi_vulkan PUBLIC X11::X11)
endif()
