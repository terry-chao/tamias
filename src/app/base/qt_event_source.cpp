#include "app/base/qt_event_source.h"

#include "engine/interaction/drag_manager.h"

#include <QCoreApplication>
#include <QTimer>

#include <string_view>

namespace tamias {
namespace {

QtEventSource& shared_source() {
  static QtEventSource source;
  return source;
}

}  // namespace

bool QtEventSource::wait(InteractionEvent& out, std::chrono::milliseconds timeout) {
  (void)out;  // 事件走 handle_*，不从这里取（见类注释）。
  if (closed_ || QCoreApplication::closingDown()) {
    return false;
  }

  // 关键就这一句：把控制权交回平台泵，UI 继续响应，Qt 继续把鼠标/键盘
  // 事件派发到视口，视口再送进管理器直达 drag。
  QEventLoop loop;
  loop_ = &loop;
  QTimer timeout_timer;
  timeout_timer.setSingleShot(true);
  QObject::connect(&timeout_timer, &QTimer::timeout, &loop, &QEventLoop::quit);
  timeout_timer.start(timeout);
  loop.exec();
  loop_ = nullptr;

  return false;
}

bool QtEventSource::alive() const {
  return !closed_ && !QCoreApplication::closingDown();
}

void QtEventSource::wake() {
  if (loop_ != nullptr) {
    loop_->quit();
  }
}

void QtEventSource::close() {
  closed_ = true;
  wake();
}

void install_qt_event_source() {
  DragManager& manager = DragManager::instance();
  manager.set_event_source(&shared_source());
  // drag 结束就唤醒阻塞循环，不用等满一个轮询周期。
  manager.set_end_observer(
      [](DragOwnerId, std::string_view, DragEnd) { shared_source().wake(); });
}

}  // namespace tamias
