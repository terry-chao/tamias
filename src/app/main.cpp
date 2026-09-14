#include "main_window.h"

#include "app_settings.h"
#include "tamias_version.h"
#include "command/command_system.h"
#include "engine/core/log.h"
#include "engine/render/render_runtime.h"
#include "engine/graphics/graphics_backend.h"
#include "i18n.h"
#include "engine/modeling/linked_kernels.h"
#include "engine/modeling/occt/occt_shape_ops.h"
#include "engine/profile/profiling.h"

#include <QApplication>
#include <QIcon>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <string>

namespace tamias {
void register_linked_rhi_backends();
}

int main(int argc, char* argv[]) {
  QApplication::setAttribute(Qt::AA_NativeWindows);
  QApplication app(argc, argv);
  QApplication::setOrganizationName("tamias");
  QApplication::setApplicationName("tamias");
  QApplication::setApplicationVersion(QStringLiteral(TAMIAS_VERSION_FULL));
  app.setWindowIcon(QIcon(QStringLiteral(":/branding/logo.png")));

  tamias::init_logging(tamias::LogLevel::Info);
  tamias::profiling::set_program_name("Tamias");
  tamias::profiling::set_thread_name("ui");
  tamias::register_linked_rhi_backends();
  tamias::register_linked_kernels();
  tamias::register_commands(tamias::command_registry());
  tamias::register_occt_shape_ops();

  tamias::AppSettings::instance().load();
  // 建模内核：用设置里选的那个（默认 occt）。必须在任何文档求值之前设置，
  // 内核实例建好之后再改就晚了——和渲染后端一样，改完要重启。
  tamias::set_default_kernel_backend(
      tamias::AppSettings::instance().kernel_backend().compare(QStringLiteral("truck"),
                                                              Qt::CaseInsensitive) == 0
          ? tamias::KernelBackend::Truck
          : tamias::KernelBackend::Occt);
  if (!tamias::apply_ui_language(tamias::AppSettings::instance().ui_language())) {
    tamias::log_error("Failed to load UI translation catalog; falling back to source language");
  }
  tamias::apply_ui_color_scheme(tamias::AppSettings::instance().ui_color_scheme());

  // Smoke: ensure the preferred graphics backend can create a device.
  {
    tamias::DeviceCreateInfo info{};
    info.backend = tamias::AppSettings::instance().graphics_backend();
    info.enable_validation = true;
    auto device = tamias::RHIDevice::create(info);
    if (!device) {
      tamias::log_error(device.error());
      return 1;
    }
    tamias::log_info(std::string(tamias::to_string(info.backend)) + " device smoke test OK");
    (*device)->wait_idle();
  }

  int code = 0;
  {
    tamias::MainWindow window;
    QStringList startup_paths;
    const QStringList arguments = QCoreApplication::arguments();
    for (int i = 1; i < arguments.size(); ++i) {
      if (!arguments[i].startsWith(QLatin1Char('-'))) {
        startup_paths.push_back(arguments[i]);
      }
    }
    window.show();
    if (!startup_paths.isEmpty()) {
      QTimer::singleShot(0, &window,
                         [&window, startup_paths] { window.open_paths(startup_paths); });
    }
    code = app.exec();
  }
  tamias::RenderThreadPool::instance().shutdown();
  tamias::shutdown_logging();
  return code;
}
