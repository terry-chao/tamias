# tac：自研界面库的契约层（两套 UI 共用）。
#
# 硬规则：本目录（含所有子目录）不得 include 任何 Qt 头。
# 门禁：scripts/check-tac-boundary.ps1
add_library(tamias_tac STATIC
  backend.cpp
  native_backend.cpp
  platform/prompt_form.cpp
)
add_library(tamias::tac ALIAS tamias_tac)

target_include_directories(tamias_tac PUBLIC ${CMAKE_SOURCE_DIR}/src)
