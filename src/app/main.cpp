#include "app/shell/main_window.h"

#include "app/base/app_settings.h"
#include "app/base/rhi_startup.h"
#include "app/base/startup_guard.h"
#include "tamias_version.h"
#include "command/core/command_system.h"
#include "engine/base/log.h"
#include "engine/render/rhi/rhi_probe.h"
#include "engine/render/rhi/rhi_startup_decision.h"
#include "engine/render/runtime/render_runtime.h"
#include "engine/graphics/graphics_backend.h"
#include "app/base/i18n.h"
#include "engine/modeling/linked_kernels.h"
#include "engine/modeling/occt/occt_shape_ops.h"
#include "engine/profile/profiling.h"

#include <QApplication>
#include <QIcon>
#include <QMessageBox>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <cstdio>
#include <optional>
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

  // ===== RHI 启动：探测 → 降级 → 记忆（见 docs/RHI-STARTUP.md）=====
  const tamias::RhiCliOptions cli = tamias::parse_rhi_cli(QCoreApplication::arguments());
  QStringList rhi_warnings;
  const tamias::RhiBlocklist blocklist = tamias::load_rhi_blocklist(&rhi_warnings);
  const tamias::RhiPolicy policy = tamias::load_rhi_policy(&rhi_warnings);
  for (const QString& warning : rhi_warnings) {
    tamias::log_warn(warning.toStdString());
  }

  if (cli.probe) {
    // 体检档：连「提交一次空命令」一起验（正常启动只到建设备）。
    tamias::RhiProbeOptions options{};
    options.depth = tamias::RhiProbeDepth::Submit;
    options.blocklist = &blocklist;
    // 体检也可以指定范围：--probe-rhi --gpu-backend=opengl 就只查那一条路。
    if (cli.backend.has_value()) {
      options.candidates = {*cli.backend};
    } else if (cli.safe_mode) {
      options.candidates = {tamias::GraphicsBackend::OpenGL};
    }
    const tamias::RhiProbeReport report = tamias::probe_rhi(options);
    const std::string output = cli.json ? report.to_json() : report.summary;
    std::fputs(output.c_str(), stdout);
    std::fputc('\n', stdout);
    return report.chosen.has_value() ? 0 : 2;
  }

  tamias::StartupGuard startup_guard;
  if (startup_guard.previous_startup_incomplete()) {
    tamias::log_warn("上次启动没走完（可能在建设备/首帧崩了），本次强制安全模式");
  }
  tamias::AppSettings& settings = tamias::AppSettings::instance();
  tamias::RhiStartupInput startup_input{};
  startup_input.cli_backend = cli.backend;
  startup_input.cli_safe_mode = cli.safe_mode;
  startup_input.policy = policy;
  startup_input.last_good_backend = settings.last_good_backend();
  startup_input.last_good_trusted = !startup_guard.previous_startup_incomplete();
  const tamias::RhiStartupDecision decision = tamias::decide_rhi_startup(startup_input);
  tamias::log_info("RHI startup: " + decision.why);

  tamias::RhiProbeOptions probe{};
  probe.candidates = decision.candidates;
  probe.depth = tamias::RhiProbeDepth::Device;  // 正常启动走 A 档：快
  probe.blocklist = &blocklist;
  probe.blocklist_override = decision.blocklist_override;
  const tamias::RhiProbeReport report = tamias::probe_rhi(probe);
  tamias::log_info(report.summary);
  if (!report.chosen.has_value()) {
    // 以前这里是静默 return 1；现在必须让人知道为什么，并给一条可操作的路。
    QMessageBox::critical(nullptr, QCoreApplication::applicationName(),
                          QString::fromStdString(report.summary) +
                              QLatin1String("\n\nTry --safe-mode or --gpu-backend=opengl."));
    return 3;
  }
  if (*report.chosen != settings.graphics_backend()) {
    tamias::log_warn(std::string("回退到 ") + tamias::to_string(*report.chosen) + "（用户偏好是 " +
                     tamias::to_string(settings.graphics_backend()) + "）");
  }
  settings.set_resolved_backend(*report.chosen);
  settings.set_safe_mode(decision.safe_mode);
  settings.set_backend_locked(decision.policy_locked);
  for (const tamias::RhiProbeResult& attempt : report.attempts) {
    if (attempt.ok) {
      const std::string fingerprint = tamias::rhi_gpu_fingerprint(attempt.identity);
      if (!settings.last_good_gpu_fingerprint().isEmpty() &&
          settings.last_good_gpu_fingerprint() != QString::fromStdString(fingerprint)) {
        tamias::log_info("GPU/驱动指纹变了（换卡或升驱动），已更新记忆");
      }
      settings.set_last_good_backend(attempt.backend, QString::fromStdString(fingerprint));
      break;
    }
  }
  settings.save();
  startup_guard.begin();  // 从这里到首帧之间崩了，下次就是安全模式

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
    // 界面起来了 = 这次启动走通了：清掉启动标记（下次启动直接信任记住的后端）。
    QTimer::singleShot(1500, &window, [&startup_guard] { startup_guard.mark_started_ok(); });
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
