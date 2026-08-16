#include "property_panel.h"

#include <QAbstractSpinBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

#include <algorithm>
#include <string>
#include <vector>

namespace tamias {

QString PropertyPanel::entity_label(EntityKind kind) {
  switch (kind) {
    case EntityKind::Wall:
      return tr("Wall");
    case EntityKind::Box:
      return tr("Box");
    case EntityKind::Cylinder:
      return tr("Cylinder");
  }
  return tr("Entity");
}

QString PropertyPanel::param_label(EntityKind entity_kind, FeatureKind feature_kind,
                                   const QString& param_name) {
  // 求值器把特征树按 Z-up 构造后转到 Y-up（(x,y,z)→(x,z,-y)），因此：
  //   RectProfile.width → 视口 X，RectProfile.height → 视口 Z，Extrude.depth → 视口 Y。
  const auto is = [&](const char* n) { return param_name == QLatin1String(n); };
  // 特征级参数（与实体类型无关）。
  if (feature_kind == FeatureKind::Fillet) {
    if (is("radius")) {
      return tr("Radius");
    }
    if (is("edge")) {
      return tr("Edge");
    }
  }
  if (feature_kind == FeatureKind::Chamfer) {
    if (is("distance")) {
      return tr("Distance");
    }
    if (is("edge")) {
      return tr("Edge");
    }
  }
  if (feature_kind == FeatureKind::Boolean) {
    if (is("operation")) {
      return tr("Operation");
    }
  }
  switch (entity_kind) {
    case EntityKind::Wall:
      if (feature_kind == FeatureKind::RectProfile) {
        if (is("width")) {
          return tr("Thickness");
        }
        if (is("height")) {
          return tr("Length");
        }
      } else if (feature_kind == FeatureKind::Extrude && is("depth")) {
        return tr("Height");
      }
      break;
    case EntityKind::Box:
      if (feature_kind == FeatureKind::RectProfile) {
        if (is("width")) {
          return tr("Width");
        }
        if (is("height")) {
          return tr("Depth");
        }
      } else if (feature_kind == FeatureKind::Extrude && is("depth")) {
        return tr("Height");
      }
      break;
    case EntityKind::Cylinder:
      if (feature_kind == FeatureKind::CircleProfile && is("radius")) {
        return tr("Radius");
      }
      if (feature_kind == FeatureKind::Extrude && is("depth")) {
        return tr("Height");
      }
      break;
  }
  return param_name;  // 未知组合：回退原始参数名
}

PropertyPanel::PropertyPanel(QWidget* parent) : QWidget(parent) {
  setMinimumWidth(260);  // 默认别太窄，给名称+数值留够空间
  root_ = new QVBoxLayout(this);
  root_->setContentsMargins(8, 8, 8, 8);
  root_->setSpacing(0);
}

void PropertyPanel::show_entity(const Entity* entity, const QString& fallback_note) {
  // 整块重建内容区。旧块用 deleteLater：刷新可能由旧块里 spinbox 的 valueChanged
  // 信号链同步触发，直接 delete 会在该信号栈内销毁正被使用的 widget。
  if (content_) {
    content_->hide();
    content_->deleteLater();
    content_ = nullptr;
  }

  content_ = new QWidget(this);
  auto* column = new QVBoxLayout(content_);
  column->setContentsMargins(0, 0, 0, 0);
  column->setSpacing(6);
  root_->addWidget(content_);

  auto* header = new QLabel(content_);
  header->setWordWrap(true);
  header->setTextInteractionFlags(Qt::TextSelectableByMouse);

  if (entity == nullptr) {
    header->setText(fallback_note);
    column->addWidget(header);
    column->addStretch(1);
    return;
  }

  const std::uint64_t eid = entity->id;
  header->setText(QStringLiteral("%1 — %2")
                      .arg(QString::fromStdString(entity->name), entity_label(entity->kind())));
  column->addWidget(header);

  auto* form = new QFormLayout();
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

  // 扁平 grid：每行 [参数名 | 数值 spinbox]，按特征顺序 + 参数名排序，顺序稳定。
  for (const auto& feature : entity->model.features()) {
    std::vector<std::string> keys;
    keys.reserve(feature.params.size());
    for (const auto& [key, unused] : feature.params) {
      (void)unused;
      keys.push_back(key);
    }
    std::sort(keys.begin(), keys.end());

    const std::uint64_t fid = feature.id;
    for (const auto& key : keys) {
      auto* spin = new QDoubleSpinBox(content_);
      spin->setRange(-1.0e6, 1.0e6);
      spin->setDecimals(3);
      spin->setSingleStep(0.1);
      spin->setKeyboardTracking(false);
      spin->setButtonSymbols(QAbstractSpinBox::UpDownArrows);  // 上/下箭头竖排（▲上▼下）
      spin->setValue(feature.params.at(key));

      const QString param_name = QString::fromStdString(key);
      connect(spin, &QDoubleSpinBox::valueChanged, this, [this, eid, fid, param_name](double value) {
        emit param_edited(eid, fid, param_name, value);
      });

      form->addRow(param_label(entity->kind(), feature.kind, QString::fromStdString(key)), spin);
    }
  }

  column->addLayout(form);
  column->addStretch(1);
}

}  // namespace tamias
