# qt：两套 UI 里的 Qt 那套（tac 契约的 Qt 6 实现）。
#
# 这里是允许 include Qt 的地方；src/ui/tac 与 src/app 的白名单只减不增。
add_library(tamias_qt STATIC
  backend.cpp
  dialogs.cpp
  text.cpp
  theme.cpp
)
add_library(tamias::qt ALIAS tamias_qt)

target_include_directories(tamias_qt PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tamias_qt PUBLIC tamias::tac PRIVATE Qt6::Widgets Qt6::Gui)
