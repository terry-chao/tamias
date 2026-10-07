# TacUI —— 自研界面库的实现（D3D12 + DirectWrite，只有 Windows x64）。
#
# 两种来源，消费端看到的 target 名字一样（TacUI::tacui_host / TacUI::tacui）：
#
#   1. TAMIAS_TACUI_SOURCE_DIR 指向本地 checkout —— 当子工程编。改完 TacUI 源码
#      直接 `cmake --build` 就生效，没有「重新 vcpkg 安装」这一步。
#   2. 留空 —— find_package(TacUI CONFIG)，用 vcpkg 按 vcpkg.json 装好的那份
#      （TacUI 自己的 git registry，见 vcpkg-configuration.json）。
#
# 现在还没有目标链它，所以本地那份用 EXCLUDE_FROM_ALL 拉进来：不参与 all、不
# 影响编译时间；阶段 5 把自研实现接进 src/ui/tac 后，链接关系会把需要的目标拉起来。

set(TAMIAS_TACUI_SOURCE_DIR "" CACHE PATH
  "本地 TacUI checkout；非空则当子工程编，空则用 vcpkg 装的那份")

# 阶段 5 接线时链这个：两种来源都是 TacUI::tacui_host。
set(TAMIAS_TACUI_TARGET "")

if(WIN32 AND MSVC AND NOT EMSCRIPTEN)
  if(TAMIAS_TACUI_SOURCE_DIR)
    if(NOT EXISTS "${TAMIAS_TACUI_SOURCE_DIR}/CMakeLists.txt")
      message(FATAL_ERROR
        "TAMIAS_TACUI_SOURCE_DIR=${TAMIAS_TACUI_SOURCE_DIR} 不是 TacUI checkout"
        "（没有 CMakeLists.txt）。留空可改用 vcpkg 装的那份。")
    endif()
    add_subdirectory("${TAMIAS_TACUI_SOURCE_DIR}"
                     "${CMAKE_BINARY_DIR}/_deps/tacui-build" EXCLUDE_FROM_ALL)
    set(TAMIAS_TACUI_TARGET TacUI::tacui_host)
    message(STATUS "TacUI: 本地 checkout ${TAMIAS_TACUI_SOURCE_DIR}")
  else()
    find_package(TacUI CONFIG REQUIRED)
    if(NOT TARGET TacUI::tacui_host)
      message(FATAL_ERROR
        "找到 TacUI 但没有 TacUI::tacui_host。检查 vcpkg 的 tacui port。")
    endif()
    set(TAMIAS_TACUI_TARGET TacUI::tacui_host)
    message(STATUS "TacUI: ${TacUI_DIR}")
  endif()
else()
  message(STATUS "TacUI: 跳过（只有 Windows + MSVC 有 D3D12 后端）")
endif()
