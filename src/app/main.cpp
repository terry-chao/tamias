#include "main_window.h"

#include "core/log.h"
#include "engine/render/render_runtime.h"

#include <QApplication>
#include <QIcon>

namespace tamias {
void register_linked_rhi_backends();
}

int main(int argc, char* argv[]) {
  QApplication::setAttribute(Qt::AA_NativeWindows);
  QApplication app(argc, argv);
  QApplication::setOrganizationName("tamias");
  QApplication::setApplicationName("tamias");
  QApplication::setApplicationVersion("0.1.0");
  app.setWindowIcon(QIcon(QStringLiteral(":/branding/logo.png")));


  tamias::init_logging(tamias::LogLevel::Info);
  tamias::register_linked_rhi_backends();

  // Smoke: ensure Vulkan device can be created before showing UI.
  {
    tamias::DeviceCreateInfo info{};
    info.enable_validation = true;
    auto device = tamias::RHIDevice::create(info);
    if (!device) {
      tamias::log_error(device.error());
      return 1;
    }
    tamias::log_info("Vulkan device smoke test OK");
    (*device)->wait_idle();
  }

  tamias::MainWindow window;
  window.show();
  const int code = app.exec();
  tamias::RenderThreadPool::instance().shutdown();
  tamias::shutdown_logging();
  return code;
}
