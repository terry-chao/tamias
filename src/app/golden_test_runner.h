#pragma once

#include "golden_test_case.h"

#include <QString>
#include <QVector>
#include <QWidget>

#include <filesystem>

namespace tamias {

// Result of Pin's follow-up: run RenderSceneGolden* (build tamias_tests if missing).
struct GoldenTestRun {
  bool ok = false;
  bool built_exe = false;
  QString exe_path;
  QString log;
  QVector<GoldenTestCase> cases;
};

GoldenTestRun run_render_scene_golden_tests(const std::filesystem::path& source_dir,
                                            QWidget* parent = nullptr);

}  // namespace tamias
