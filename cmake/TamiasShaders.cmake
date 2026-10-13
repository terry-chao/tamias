# Compile HLSL to SPIR-V with DXC when available.
find_program(TAMIAS_DXC NAMES dxc
  HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
if(NOT TAMIAS_DXC)
  message(WARNING "dxc not found; prebuilt SPIR-V under shaders/spv must exist.")
endif()

# tamias_compile_hlsl(<target> <hlsl_relpath> <profile> <out_basename>)
# Produces:
#   <out_basename>.spv     (Vulkan, -DTAMIAS_VULKAN=1)
#   <out_basename>.gl.spv  (OpenGL / SPIR-V 1.0)
#
# GL 变体为什么用 vulkan1.0 这个 target：dxc 没有 OpenGL 目标，而 ARB_gl_spirv 只认
# **SPIR-V 1.0**（universal1.5 会产出 1.5 的模块：spirv-val --target-env opengl4.5 直接
# 判非法，驱动侧也会走歪，见 docs/RHI-TRIAGE.md §10）。vulkan1.0 是 dxc 里唯一能出
# 1.0 的档；采样器仍然按 GL 的规矩写成合并的 image+sampler（见 *.hlsl 里的 #else 分支）。
function(tamias_compile_hlsl TARGET_NAME SHADER_PATH PROFILE OUT_BASENAME)
  set(SRC_ABS "${TAMIAS_SHADER_SOURCE_DIR}/${SHADER_PATH}")
  set(HLSLI_ABS "${TAMIAS_SHADER_SOURCE_DIR}/mesh.hlsli")
  set(OUT_VK "${TAMIAS_SHADER_OUTPUT_DIR}/${OUT_BASENAME}.spv")
  set(OUT_GL "${TAMIAS_SHADER_OUTPUT_DIR}/${OUT_BASENAME}.gl.spv")

  if(TAMIAS_DXC)
    add_custom_command(
      OUTPUT "${OUT_VK}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${TAMIAS_SHADER_OUTPUT_DIR}"
      COMMAND "${TAMIAS_DXC}"
              -T ${PROFILE}
              -E main
              -spirv
              -fspv-target-env=vulkan1.1
              -DTAMIAS_VULKAN=1
              -I "${TAMIAS_SHADER_SOURCE_DIR}"
              -Fo "${OUT_VK}"
              "${SRC_ABS}"
      DEPENDS "${SRC_ABS}" "${HLSLI_ABS}"
      COMMENT "DXC ${SHADER_PATH} -> ${OUT_BASENAME}.spv (Vulkan)"
      VERBATIM)
    add_custom_command(
      OUTPUT "${OUT_GL}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${TAMIAS_SHADER_OUTPUT_DIR}"
      COMMAND "${TAMIAS_DXC}"
              -T ${PROFILE}
              -E main
              -spirv
              -fspv-target-env=vulkan1.0
              -I "${TAMIAS_SHADER_SOURCE_DIR}"
              -Fo "${OUT_GL}"
              "${SRC_ABS}"
      DEPENDS "${SRC_ABS}" "${HLSLI_ABS}"
      COMMENT "DXC ${SHADER_PATH} -> ${OUT_BASENAME}.gl.spv (OpenGL)"
      VERBATIM)
  else()
    set(PREBUILT_VK "${TAMIAS_SHADER_SOURCE_DIR}/spv/${OUT_BASENAME}.spv")
    set(PREBUILT_GL "${TAMIAS_SHADER_SOURCE_DIR}/spv/${OUT_BASENAME}.gl.spv")
    add_custom_command(
      OUTPUT "${OUT_VK}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${TAMIAS_SHADER_OUTPUT_DIR}"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PREBUILT_VK}" "${OUT_VK}"
      DEPENDS "${PREBUILT_VK}"
      COMMENT "Copying prebuilt ${OUT_BASENAME}.spv"
      VERBATIM)
    add_custom_command(
      OUTPUT "${OUT_GL}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${TAMIAS_SHADER_OUTPUT_DIR}"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PREBUILT_GL}" "${OUT_GL}"
      DEPENDS "${PREBUILT_GL}"
      COMMENT "Copying prebuilt ${OUT_BASENAME}.gl.spv"
      VERBATIM)
  endif()

  target_sources(${TARGET_NAME} PRIVATE "${OUT_VK}" "${OUT_GL}")
endfunction()

function(tamias_add_shader_target TARGET_NAME)
  add_custom_target(${TARGET_NAME} DEPENDS ${ARGN})
endfunction()
