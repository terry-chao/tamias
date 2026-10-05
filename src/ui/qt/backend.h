#pragma once

#include "ui/tac/backend.h"

#include <memory>

namespace tac::qt {

// 把 Qt 后端登记进 tac 的后端表（名字 "qt"）。main() 启动时调用一次。
void register_qt_backend();

[[nodiscard]] std::unique_ptr<UiBackend> make_backend();

}  // namespace tac::qt
