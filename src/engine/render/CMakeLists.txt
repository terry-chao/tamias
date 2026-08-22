add_subdirectory(rhi)

add_library(tamias_render STATIC render_runtime.cpp)
add_library(tamias::render ALIAS tamias_render)
target_include_directories(tamias_render PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(tamias_render PRIVATE
  TAMIAS_SHADER_DIR="${TAMIAS_SHADER_OUTPUT_DIR}"
  TAMIAS_SHADER_SOURCE_DIR="${TAMIAS_SHADER_SOURCE_DIR}"
)
target_link_libraries(tamias_render PUBLIC tamias::rhi tamias::graphics)
if(TARGET tamias::rhi_webgl)
  target_link_libraries(tamias_render PUBLIC tamias::rhi_webgl)
  target_compile_definitions(tamias_render PUBLIC TAMIAS_HAS_RHI_WEBGL=1)
endif()
