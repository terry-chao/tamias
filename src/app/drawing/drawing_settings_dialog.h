#pragma once

#include "engine/document/document.h"
#include "app/viewport/canvas/document_viewport.h"

#include <QDialog>

#include <functional>
#include <optional>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;

namespace tamias {

// 一张参考图纸的「设置」：在视口里显示不显示、画第几页、以及它贴在模型里的位置
//（比例 / 旋转 / 偏移 / 标高）。底图摆不准就翻模对不上，所以这几个数要能直接改，
// 也要能一键"按模型适配"。
//
// 对话框只算数、不碰文档：确定后由图纸管理面板写回视口（走 set_drawing_placement）。
class DrawingSettingsDialog final : public QDialog {
  Q_OBJECT
 public:
  // fit_provider：按图纸自带单位 / 模型范围算一份默认摆放（可能为空 = 读不到图纸）。
  using FitProvider = std::function<std::optional<DrawingPlacement>(int page)>;

  DrawingSettingsDialog(const DrawingRef& drawing, int page_count, double declared_unit_scale,
                        FitProvider fit_provider, QWidget* parent = nullptr);

  [[nodiscard]] DrawingPlacement placement() const;
  [[nodiscard]] int page() const;
  [[nodiscard]] bool visible_in_viewport() const;

 private:
  void apply_fit();
  void reset_to_unit_scale();

  double unit_scale_ = 0.0;
  FitProvider fit_provider_;
  QCheckBox* visible_box_ = nullptr;
  QSpinBox* page_spin_ = nullptr;
  QDoubleSpinBox* scale_spin_ = nullptr;
  QDoubleSpinBox* rotation_spin_ = nullptr;
  QDoubleSpinBox* elevation_spin_ = nullptr;
  QDoubleSpinBox* offset_x_spin_ = nullptr;
  QDoubleSpinBox* offset_z_spin_ = nullptr;
  QLabel* hint_ = nullptr;
};

}  // namespace tamias
