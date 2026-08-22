if(NOT EMSCRIPTEN)
  message(FATAL_ERROR "tamias_rhi_webgl requires Emscripten")
endif()

add_library(tamias_rhi_webgl STATIC webgl_device.cpp)
add_library(tamias::rhi_webgl ALIAS tamias_rhi_webgl)
target_include_directories(tamias_rhi_webgl PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tamias_rhi_webgl PUBLIC tamias::rhi tamias::graphics)
target_compile_definitions(tamias_rhi_webgl PUBLIC TAMIAS_HAS_RHI_WEBGL=1)
