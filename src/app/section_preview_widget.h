#pragma once

#include "param_spec.h"
#include "section_preview_spec.h"

#include <QString>
#include <QWidget>
#include <unordered_map>
#include <vector>

class QDoubleSpinBox;
class QPushButton;

namespace tamias {

// 绘制面板上的截面示意图：按当前参数实时重绘，标注可点进编辑。
class SectionPreviewWidget final : public QWidget {
  Q_OBJECT
 public:
  explicit SectionPreviewWidget(QWidget* parent = nullptr);

  void set_section(const SectionPreviewSpec& spec, const std::vector<ParamSpec>& params);
  void set_value(const QString& key, double value);
  [[nodiscard]] double value(const QString& key, double fallback = 0.0) const;
  [[nodiscard]] std::unordered_map<std::string, double> values() const;
  [[nodiscard]] const SectionPreviewSpec& spec() const { return spec_; }

 signals:
  void param_edited(const QString& key, double value);

 protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

 private:
  struct DimEditor {
    QString key;
    ParamSpec param;
    QPushButton* chip = nullptr;
    QDoubleSpinBox* spin = nullptr;
  };

  void rebuild_editors();
  void layout_editors();
  void begin_edit(DimEditor& dim);
  void commit_edit(DimEditor& dim);
  void update_chip_text(DimEditor& dim);
  [[nodiscard]] const ParamSpec* find_param(const QString& key) const;
  [[nodiscard]] QRectF world_bounds() const;
  [[nodiscard]] QTransform world_transform() const;
  [[nodiscard]] QPointF to_widget(QPointF world) const;
  void draw_section(class QPainter& painter) const;
  void draw_h_dim(class QPainter& painter, QPointF a, QPointF b, bool above) const;
  void draw_v_dim(class QPainter& painter, QPointF a, QPointF b, const QString& key) const;
  [[nodiscard]] QRect chip_rect_h(QPointF a, QPointF b, bool above) const;
  [[nodiscard]] QRect chip_rect_v(QPointF a, QPointF b) const;

  SectionPreviewSpec spec_{};
  std::vector<ParamSpec> params_;
  std::unordered_map<std::string, double> values_;
  std::vector<DimEditor> editors_;
  bool editing_ = false;
};

}  // namespace tamias
