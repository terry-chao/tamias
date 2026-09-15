#pragma once

#include "engine/render/scene/render_scene.h"
#include "app/debug/golden_test_runner.h"

#include <QDialog>
#include <QString>

namespace tamias {

class PinResultDialog final : public QDialog {
  Q_OBJECT
 public:
  PinResultDialog(const RenderScene& scene, const QString& golden_dir,
                  const QString& inspect_text, const GoldenTestRun& tests,
                  QWidget* parent = nullptr);
};

}  // namespace tamias
