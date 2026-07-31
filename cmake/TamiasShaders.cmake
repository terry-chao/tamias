# Compile GLSL to SPIR-V with glslangValidator when available.
find_program(TAMIAS_GLSLANG_VALIDATOR NAMES glslangValidator glslang)
if(NOT TAMIAS_GLSLANG_VALIDATOR)
  message(WARNING "glslangValidator not found; prebuilt SPIR-V under shaders/spv must exist.")
endif()

function(tamias_compile_shader TARGET_NAME SHADER_PATH)
  get_filename_component(SHADER_NAME "${SHADER_PATH}" NAME)
  set(OUT_SPV "${TAMIAS_SHADER_OUTPUT_DIR}/${SHADER_NAME}.spv")
  set(SRC_ABS "${TAMIAS_SHADER_SOURCE_DIR}/${SHADER_PATH}")
  if(TAMIAS_GLSLANG_VALIDATOR)
    add_custom_command(
      OUTPUT "${OUT_SPV}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${TAMIAS_SHADER_OUTPUT_DIR}"
      COMMAND "${TAMIAS_GLSLANG_VALIDATOR}" -V "${SRC_ABS}" -o "${OUT_SPV}"
      DEPENDS "${SRC_ABS}"
      COMMENT "Compiling ${SHADER_PATH}"
      VERBATIM)
  else()
    set(PREBUILT "${TAMIAS_SHADER_SOURCE_DIR}/spv/${SHADER_NAME}.spv")
    add_custom_command(
      OUTPUT "${OUT_SPV}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${TAMIAS_SHADER_OUTPUT_DIR}"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PREBUILT}" "${OUT_SPV}"
      DEPENDS "${PREBUILT}"
      COMMENT "Copying prebuilt ${SHADER_NAME}.spv"
      VERBATIM)
  endif()
  target_sources(${TARGET_NAME} PRIVATE "${OUT_SPV}")
endfunction()

function(tamias_add_shader_target TARGET_NAME)
  add_custom_target(${TARGET_NAME} DEPENDS ${ARGN})
endfunction()
