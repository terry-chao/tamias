#include "app/shell/main_window.h"

#include "app/base/app_settings.h"
#include "app/base/qt_path.h"
#include "app/base/log_buffer.h"
#include "app/base/rhi_diagnostics.h"
#include "app/base/rhi_startup.h"
#include "app/base/startup_guard.h"
#include "tamias_version.h"
#include "command/core/command_system.h"
#include "engine/base/log.h"
#include "engine/render/rhi/rhi_probe.h"
#include "engine/render/rhi/rhi_startup_decision.h"
#include "engine/document/scene_capture.h"
#include "engine/io/mesh_io.h"
#include "engine/render/runtime/render_runtime.h"
#include "engine/graphics/graphics_backend.h"
#include "app/base/i18n.h"
#include "engine/modeling/linked_kernels.h"
#include "engine/modeling/occt/occt_shape_ops.h"
#include "engine/profile/profiling.h"

#include <QApplication>
#include <QIcon>
#include <QMessageBox>
#include <QSaveFile>
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
  tamias::install_log_buffer();  // 诊断面板要用的最近日志（不替换 stderr 输出）
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
    // 体检档：离屏画一个像素再读回来（最接近「真的能出图」；正常启动只到建设备）。
    tamias::RhiProbeOptions options{};
    options.depth = tamias::RhiProbeDepth::Pixel;
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
  startup_input.previous_startup_incomplete = startup_guard.previous_startup_incomplete();
  startup_input.preferred_backend = settings.graphics_backend();
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
  if (decision.remember_result) {
    // 只记「自然选中并跑通」的后端：安全模式 / 策略强制 / 命令行指定的会话不记，
    // 否则一次崩溃循环（被迫走 OpenGL）就会把这台机器永久降级。
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
  }
  settings.save();
  // 把这次探测的现场留给诊断面板（面板不重新探测：volk 单设备，见 rhi_diagnostics.h）。
  {
    tamias::RhiDiagnosticsSnapshot snapshot{};
    snapshot.report = report;
    snapshot.startup_reason = decision.why;
    snapshot.preference = tamias::to_string(settings.graphics_backend());
    snapshot.degraded = *report.chosen != settings.graphics_backend();
    snapshot.safe_mode = decision.safe_mode;
    snapshot.policy_locked = decision.policy_locked;
    snapshot.has_policy = policy.force_backend.has_value() || policy.lock ||
                          policy.override_blocklist;
    snapshot.blocklist_version = blocklist.version;
    snapshot.blocklist_entries = blocklist.entries.size();
    tamias::RhiDiagnostics::instance().set_snapshot(std::move(snapshot));
  }
  // ===== 离屏出图：把文档渲成 PNG 后退出（不建窗口）=====
  if (cli.diagnostics_report_path.has_value()) {
    // IT / 脚本收集现场用：和「图形诊断」面板显示的是同一份文本。
    const QString path = *cli.diagnostics_report_path;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
      tamias::log_error("--diagnostics-report: 打不开输出文件: " + path.toStdString());
      return 5;
    }
    const std::string report_text = tamias::build_diagnostics_report();
    file.write(report_text.data(), static_cast<qint64>(report_text.size()));
    if (!file.commit()) {
      tamias::log_error("--diagnostics-report: 写文件失败: " + path.toStdString());
      return 5;
    }
    tamias::log_info("--diagnostics-report: 写出 " + path.toStdString());
    tamias::RenderThreadPool::instance().shutdown();
    return 0;
  }

  // 启动标记只保护**开窗口**的那条路：从这里到首帧之间崩了，下次就强制安全模式。
  // 上面那些命令行模式不建窗口、跑完就退出，标记不该被它们碰（否则污染下次启动）。
  startup_guard.begin();

  if (cli.render_view_path.has_value()) {
    QStringList positional;
    // 注意：arguments() 返回的是**临时** QStringList，必须先落到具名变量再取元素引用。
    const QStringList arguments = QCoreApplication::arguments();
    for (int i = 1; i < arguments.size(); ++i) {
      const QString& argument = arguments[i];
      if (!argument.startsWith(QLatin1Char('-'))) {
        positional.push_back(argument);
      }
    }
    if (positional.isEmpty()) {
      tamias::log_error("--render-view 需要给一个文档路径（.tdoc / .obj）");
      return 4;
    }
    const std::filesystem::path source = tamias::qstring_to_path(positional.front());
    tamias::SceneCaptureRequest request{};
    request.width = cli.render_width;
    request.height = cli.render_height;
    tamias::Document document("render-view");
    if (tamias::is_tdoc_document_path(source)) {
      auto loaded = tamias::load_document(source);
      if (!loaded) {
        tamias::log_error(loaded.error());
        return 4;
      }
      document = std::move(loaded->document);
      if (loaded->has_viewport) {
        // 用文档里存的相机，导出的角度和上次在屏幕上看到的一致。
        request.target = loaded->viewport.target;
        request.distance = loaded->viewport.distance;
        request.yaw = loaded->viewport.yaw;
        request.pitch = loaded->viewport.pitch;
        request.fovy = loaded->viewport.fovy;
        request.mode = static_cast<tamias::RenderMode>(loaded->viewport.render_mode);
        request.xray = loaded->viewport.xray;
      }
    } else {
      auto mesh = tamias::load_mesh_file(source);
      if (!mesh) {
        tamias::log_error(mesh.error());
        return 4;
      }
      document.add_import_mesh(source.stem().string(), std::move(*mesh), tamias::Mat4::identity(),
                               tamias::Vec3{0.75f, 0.78f, 0.82f});
      // 没有存过的相机：按模型包围盒大致框一下。
      const tamias::Aabb bounds = document.bounds();
      if (bounds.valid()) {
        request.target = bounds.center();
        request.distance = std::max(tamias::length(bounds.extent()) * 1.6f, 2.f);
      }
    }

    const auto thread = tamias::RenderThreadPool::instance().acquire(settings.render_device_config());
    if (!thread) {
      tamias::log_error("--render-view: 取不到渲染线程");
      return 4;
    }
    auto pixels = tamias::capture_document_rgba(*thread, document, request);
    if (!pixels) {
      tamias::log_error("--render-view: " + pixels.error());
      return 4;
    }
    const QImage image(pixels->data(), static_cast<int>(request.width),
                       static_cast<int>(request.height), QImage::Format_RGBA8888);
    const QString output = *cli.render_view_path;
    if (!image.save(output)) {
      tamias::log_error("--render-view: 写 PNG 失败: " + output.toStdString());
      return 4;
    }
    tamias::log_info("--render-view: 写出 " + output.toStdString() + " (" +
                     std::to_string(request.width) + "x" + std::to_string(request.height) + ")");
    tamias::RenderThreadPool::instance().shutdown();
    return 0;
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
