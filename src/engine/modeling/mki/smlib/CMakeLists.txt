# SMLib 后端（NVIDIA USD BRep v1.0.0 的 SMLib + SM_API）。
#
# 两个 target，因为语言版本要求不同：
#   tamias_smlib_c    —— C++17，唯一 include SMLib 头的地方（smlib_api.cpp）；
#   tamias_mki_smlib —— C++23（项目默认），实现 ModelKernel，只 include smlib_c.h。
#
# 发布包是 Unicode + /MD + v142 构建的，所以：
#   - 必须定义 UNICODE/_UNICODE，否则 TCHAR 是 char，Dump/smos_* 的符号对不上；
#   - Debug（/MDd）不要打开这个后端，跨 CRT 释放会炸。

if(NOT WIN32)
  message(WARNING "TAMIAS_ENABLE_SMLIB_KERNEL 目前只接了 Windows；"
                  "Linux 的 .so 还没验证，后端被跳过。")
  return()
endif()

if(NOT TAMIAS_SMLIB_PREFIX)
  message(FATAL_ERROR
    "TAMIAS_ENABLE_SMLIB_KERNEL=ON 需要 -DTAMIAS_SMLIB_PREFIX=<prefix>。\n"
    "prefix 下应有 include/SMLib、include/SM_API、lib/SMLib.{dll,lib}、lib/SM_API.{dll,lib}。\n"
    "从 https://github.com/NVIDIA-Omniverse/usd-brep/releases 的 windows-x86_64 包解压即可。")
endif()

set(_smlib_lib_dir "${TAMIAS_SMLIB_PREFIX}/lib")
foreach(_file SMLib.dll SMLib.lib SM_API.dll SM_API.lib)
  if(NOT EXISTS "${_smlib_lib_dir}/${_file}")
    message(FATAL_ERROR "TAMIAS_SMLIB_PREFIX 缺 ${_file}（${_smlib_lib_dir}）")
  endif()
endforeach()

add_library(smlib_native SHARED IMPORTED)
set_target_properties(smlib_native PROPERTIES
  IMPORTED_LOCATION "${_smlib_lib_dir}/SMLib.dll"
  IMPORTED_IMPLIB   "${_smlib_lib_dir}/SMLib.lib")

add_library(sm_api_native SHARED IMPORTED)
set_target_properties(sm_api_native PROPERTIES
  IMPORTED_LOCATION "${_smlib_lib_dir}/SM_API.dll"
  IMPORTED_IMPLIB   "${_smlib_lib_dir}/SM_API.lib")

add_library(tamias_smlib_c STATIC smlib_api.cpp)
target_include_directories(tamias_smlib_c SYSTEM PRIVATE
  "${TAMIAS_SMLIB_PREFIX}/include/SMLib"
  "${TAMIAS_SMLIB_PREFIX}/include/SM_API")
target_include_directories(tamias_smlib_c PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(tamias_smlib_c PRIVATE UNICODE _UNICODE)
set_target_properties(tamias_smlib_c PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF)
if(MSVC)
  # 老代码头文件：代码页警告 + DLL 导出类的 C4251/C4275，不是我们能修的。
  target_compile_options(tamias_smlib_c PRIVATE /wd4819 /wd4251 /wd4275 /wd4244 /wd4267)
endif()
target_link_libraries(tamias_smlib_c PRIVATE smlib_native sm_api_native)

add_library(tamias_mki_smlib STATIC smlib_kernel.cpp)
add_library(tamias::mki_smlib ALIAS tamias_mki_smlib)
target_include_directories(tamias_mki_smlib PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(tamias_mki_smlib
  PUBLIC
    tamias::mki
    tamias::base
    tamias::graphics
  PRIVATE
    tamias_smlib_c)
