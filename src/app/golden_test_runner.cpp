#include "golden_test_runner.h"

#include "qt_path.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHash>
#include <QObject>
#include <QProcess>
#include <QProgressDialog>
#include <QRegularExpression>
#include <QStandardPaths>

namespace tamias {
namespace {

#ifdef _WIN32
constexpr auto kTestsExeName = "tamias_tests.exe";
#else
constexpr auto kTestsExeName = "tamias_tests";
#endif

constexpr auto kGtestFilter = "RenderSceneGolden*";
constexpr int kBuildTimeoutMs = 40 * 60 * 1000;
constexpr int kTestTimeoutMs = 2 * 60 * 1000;

QString config_folder_from_app() {
  const QString name = QFileInfo(QCoreApplication::applicationDirPath()).fileName();
  if (name.compare(QStringLiteral("Debug"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Debug");
  }
  if (name.compare(QStringLiteral("RelWithDebInfo"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("RelWithDebInfo");
  }
  if (name.compare(QStringLiteral("Release"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Release");
  }
  return QStringLiteral("Debug");
}

QString preset_from_folder(const QString& folder) {
  if (folder == QStringLiteral("RelWithDebInfo")) {
    return QStringLiteral("relwithdebinfo");
  }
  if (folder == QStringLiteral("Release")) {
    return QStringLiteral("release");
  }
  return QStringLiteral("debug");
}

QString tests_exe_in_dir(const QString& dir) {
  return QDir(dir).filePath(QString::fromLatin1(kTestsExeName));
}

QString find_tests_exe(const std::filesystem::path& source_dir, const QString& preferred_folder) {
  QStringList dirs;
  dirs << QCoreApplication::applicationDirPath();
  const QString bin = path_to_qstring(source_dir / "build" / "bin");
  dirs << QDir(bin).filePath(preferred_folder);
  for (const auto& folder : {QStringLiteral("Debug"), QStringLiteral("RelWithDebInfo"),
                             QStringLiteral("Release")}) {
    if (folder != preferred_folder) {
      dirs << QDir(bin).filePath(folder);
    }
  }
  for (const QString& dir : dirs) {
    const QString exe = tests_exe_in_dir(dir);
    if (QFileInfo::exists(exe)) {
      return exe;
    }
  }
  return {};
}

QString run_logged(const QString& program, const QStringList& args, const QString& working_dir,
                   int timeout_ms, int* exit_code) {
  QProcess proc;
  proc.setWorkingDirectory(working_dir);
  proc.setProcessChannelMode(QProcess::MergedChannels);
  proc.start(program, args);
  if (!proc.waitForStarted(15000)) {
    *exit_code = -1;
    return QStringLiteral("failed to start %1: %2\n").arg(program, proc.errorString());
  }
  QElapsedTimer timer;
  timer.start();
  while (proc.state() != QProcess::NotRunning) {
    proc.waitForFinished(100);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    if (timeout_ms >= 0 && timer.elapsed() > timeout_ms) {
      proc.kill();
      *exit_code = -1;
      return QString::fromLocal8Bit(proc.readAll()) + QStringLiteral("\ntimed out after %1 ms\n")
                                                          .arg(timeout_ms);
    }
  }
  *exit_code = proc.exitStatus() == QProcess::NormalExit ? proc.exitCode() : -1;
  return QString::fromLocal8Bit(proc.readAll());
}

QString powershell_exe() {
  const QString pwsh = QStandardPaths::findExecutable(QStringLiteral("pwsh"));
  if (!pwsh.isEmpty()) {
    return pwsh;
  }
  const QString ps = QStandardPaths::findExecutable(QStringLiteral("powershell"));
  if (!ps.isEmpty()) {
    return ps;
  }
  return QStringLiteral("powershell.exe");
}

QVector<GoldenTestCase> parse_gtest_log(const QString& log) {
  QVector<GoldenTestCase> cases;
  QHash<QString, int> index_of;
  const auto ensure = [&](const QString& id) {
    const auto it = index_of.constFind(id);
    if (it != index_of.cend()) {
      return it.value();
    }
    GoldenTestCase row;
    row.id = id;
    row.status = GoldenTestCase::Status::Failed;
    cases.push_back(row);
    const int idx = static_cast<int>(cases.size() - 1);
    index_of.insert(id, idx);
    return idx;
  };

  const QRegularExpression run_re(QStringLiteral(R"(\[\s*RUN\s*\]\s+(\S+))"));
  const QRegularExpression done_re(
      QStringLiteral(R"(\[\s*(OK|FAILED|SKIPPED)\s*\]\s+(\S+)(?:\s*\((\d+)\s*ms\))?)"));

  for (const QString& line : log.split(QLatin1Char('\n'))) {
    const auto run = run_re.match(line);
    if (run.hasMatch()) {
      const QString id = run.captured(1);
      if (id.contains(QLatin1Char('.'))) {
        ensure(id);
      }
      continue;
    }
    const auto done = done_re.match(line);
    if (!done.hasMatch()) {
      continue;
    }
    const QString id = done.captured(2);
    if (!id.contains(QLatin1Char('.'))) {
      continue;
    }
    const QString kind = done.captured(1);
    const int i = ensure(id);
    if (kind == QLatin1String("OK")) {
      cases[i].status = GoldenTestCase::Status::Passed;
    } else if (kind == QLatin1String("SKIPPED")) {
      cases[i].status = GoldenTestCase::Status::Skipped;
    } else {
      cases[i].status = GoldenTestCase::Status::Failed;
    }
    if (!done.captured(3).isEmpty()) {
      cases[i].duration_ms = done.captured(3).toInt();
    }
  }
  return cases;
}

}  // namespace

GoldenTestRun run_render_scene_golden_tests(const std::filesystem::path& source_dir,
                                            QWidget* parent) {
  GoldenTestRun out;
  const QString source_q = path_to_qstring(source_dir);
  const QString folder = config_folder_from_app();
  const QString preset = preset_from_folder(folder);

  QProgressDialog progress(QObject::tr("Running RenderSceneGolden*..."), QString(), 0, 0, parent);
  progress.setWindowModality(Qt::ApplicationModal);
  progress.setMinimumDuration(0);
  progress.setCancelButton(nullptr);
  progress.show();
  QCoreApplication::processEvents();

  QString exe = find_tests_exe(source_dir, folder);
  if (exe.isEmpty()) {
#ifdef _WIN32
    const QString script = path_to_qstring(source_dir / "scripts" / "build-tests.ps1");
    if (!QFileInfo::exists(script)) {
      out.log = QObject::tr("tamias_tests.exe not found, and missing build script:\n%1\n")
                    .arg(script);
      return out;
    }
    progress.setLabelText(QObject::tr("Building tamias_tests (%1)...").arg(preset));
    QCoreApplication::processEvents();
    int build_code = -1;
    const QString build_log =
        run_logged(powershell_exe(),
                   {QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"),
                    QStringLiteral("Bypass"), QStringLiteral("-File"), script,
                    QStringLiteral("-Preset"), preset, QStringLiteral("-BuildOnly")},
                   source_q, kBuildTimeoutMs, &build_code);
    out.built_exe = true;
    out.log += QObject::tr("built via scripts/build-tests.ps1 -Preset %1\n").arg(preset);
    out.log += build_log;
    if (build_code != 0) {
      out.log += QObject::tr("\nbuild failed (exit %1)\n").arg(build_code);
      return out;
    }
    exe = find_tests_exe(source_dir, folder);
    if (exe.isEmpty()) {
      out.log += QObject::tr("\nbuild finished but tamias_tests.exe still missing\n");
      return out;
    }
#else
    out.log = QObject::tr("tamias_tests not found next to the app or under build/bin/.\n");
    return out;
#endif
  }

  out.exe_path = exe;
  progress.setLabelText(QObject::tr("Running %1 --gtest_filter=%2")
                            .arg(QFileInfo(exe).fileName(), QString::fromLatin1(kGtestFilter)));
  QCoreApplication::processEvents();

  int test_code = -1;
  const QString test_log =
      run_logged(exe,
                 {QStringLiteral("--gtest_filter=%1").arg(QString::fromLatin1(kGtestFilter)),
                  QStringLiteral("--gtest_color=no")},
                 source_q, kTestTimeoutMs, &test_code);
  out.log += QStringLiteral("\n%1 --gtest_filter=%2\n").arg(exe, QString::fromLatin1(kGtestFilter));
  out.log += test_log;
  out.cases = parse_gtest_log(test_log);
  out.ok = test_code == 0;
  if (!out.ok) {
    out.log += QObject::tr("\ntests failed (exit %1)\n").arg(test_code);
  }
  return out;
}

}  // namespace tamias
