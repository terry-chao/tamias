#include "property_panel.h"

#include "engine/document/document.h"

#include <QAbstractSpinBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
#include <unordered_map>
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
    case EntityKind::Beam:
      return tr("Beam");
    case EntityKind::Column:
      return tr("Column");
    case EntityKind::Slab:
      return tr("Slab");
    case EntityKind::Door:
      return tr("Door");
    case EntityKind::Window:
      return tr("Window");
    case EntityKind::Line:
      return tr("Line");
    case EntityKind::Polyline:
      return tr("Polyline");
    case EntityKind::Circle:
      return tr("Circle");
    case EntityKind::Arc:
      return tr("Arc");
    case EntityKind::Bezier:
      return tr("Bezier");
    case EntityKind::Rectangle:
      return tr("Rectangle");
    case EntityKind::BSpline:
      return tr("B-spline");
    case EntityKind::Nurbs:
      return tr("NURBS");
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
  if (feature_kind == FeatureKind::BSpline || feature_kind == FeatureKind::Nurbs) {
    if (is("degree")) {
      return tr("Degree");
    }
  }
  if (feature_kind == FeatureKind::Nurbs && param_name.size() >= 2 && param_name[0] == QLatin1Char('w')) {
    bool digits = true;
    for (int i = 1; i < param_name.size(); ++i) {
      if (!param_name[i].isDigit()) {
        digits = false;
        break;
      }
    }
    if (digits) {
      return tr("Weight %1").arg(param_name.mid(1));
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
    case EntityKind::Beam:
      if (feature_kind == FeatureKind::RectProfile) {
        if (is("width")) {
          return tr("Width");
        }
        if (is("height")) {
          return tr("Length");
        }
      } else if (feature_kind == FeatureKind::Extrude && is("depth")) {
        return tr("Depth");
      }
      break;
    case EntityKind::Column:
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
    case EntityKind::Slab:
      if (feature_kind == FeatureKind::RectProfile) {
        if (is("width")) {
          return tr("Length");
        }
        if (is("height")) {
          return tr("Width");
        }
      } else if (feature_kind == FeatureKind::Extrude && is("depth")) {
        return tr("Thickness");
      }
      break;
    case EntityKind::Door:
    case EntityKind::Window:
      if (feature_kind == FeatureKind::RectProfile) {
        if (is("width")) {
          return tr("Width");
        }
        if (is("height")) {
          return tr("Thickness");
        }
      } else if (feature_kind == FeatureKind::Extrude && is("depth")) {
        return tr("Height");
      }
      break;
    case EntityKind::Circle:
      if (feature_kind == FeatureKind::CircleWire && is("radius")) {
        return tr("Radius");
      }
      break;
    case EntityKind::Line:
    case EntityKind::Polyline:
    case EntityKind::Arc:
    case EntityKind::Bezier:
    case EntityKind::BSpline:
    case EntityKind::Nurbs:
    case EntityKind::Rectangle:
      break;
  }
  return param_name;  // 未知组合：回退原始参数名
}

QString PropertyPanel::material_display_name(const std::string& name) {
  if (name == "Default") {
    return tr("Default");
  }
  if (name == "Concrete") {
    return tr("Concrete");
  }
  if (name == "Steel") {
    return tr("Steel");
  }
  if (name == "Glass") {
    return tr("Glass");
  }
  if (name == "Wood") {
    return tr("Wood");
  }
  if (name == "Plaster") {
    return tr("Plaster");
  }
  return name.empty() ? tr("(Custom)") : QString::fromStdString(name);
}

PropertyPanel::PropertyPanel(QWidget* parent) : QWidget(parent) {
  setMinimumWidth(260);  // 默认别太窄，给名称+数值留够空间
  root_ = new QVBoxLayout(this);
  root_->setContentsMargins(8, 8, 8, 8);
  root_->setSpacing(0);
}

void PropertyPanel::show_entity(const Entity* entity, Document* document,
                                const QString& fallback_note) {
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

  if (entity->location != nullptr && document != nullptr) {
    auto* storey_combo = new QComboBox(content_);
    storey_combo->addItem(tr("Unassigned"), static_cast<qulonglong>(0));
    int selected_storey = entity->location->storey_id() == 0 ? 0 : -1;
    for (const Storey& storey : document->bim().storeys()) {
      storey_combo->addItem(QString::fromStdString(storey.name),
                            static_cast<qulonglong>(storey.id));
      if (storey.id == entity->location->storey_id()) {
        selected_storey = storey_combo->count() - 1;
      }
    }
    storey_combo->setCurrentIndex(selected_storey >= 0 ? selected_storey : 0);

    auto* offset_spin = new QDoubleSpinBox(content_);
    offset_spin->setRange(-1.0e6, 1.0e6);
    offset_spin->setDecimals(3);
    offset_spin->setSingleStep(0.1);
    offset_spin->setKeyboardTracking(false);
    offset_spin->setValue(entity->location->elevation_offset());

    connect(storey_combo, &QComboBox::currentIndexChanged, this,
            [this, eid, storey_combo, offset_spin](int index) {
              if (index < 0) {
                return;
              }
              emit location_edited(
                  eid,
                  static_cast<std::uint64_t>(storey_combo->itemData(index).toULongLong()),
                  offset_spin->value());
            });
    connect(offset_spin, &QDoubleSpinBox::valueChanged, this,
            [this, eid, storey_combo](double offset) {
              emit location_edited(
                  eid,
                  static_cast<std::uint64_t>(
                      storey_combo->currentData().toULongLong()),
                  offset);
            });
    form->addRow(tr("Storey"), storey_combo);
    form->addRow(tr("Elevation Offset"), offset_spin);
  }

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
      if (is_sketch_feature(feature.kind)) {
        const bool is_weight = key.size() >= 2 && key[0] == 'w' &&
                               std::all_of(key.begin() + 1, key.end(), [](unsigned char c) {
                                 return std::isdigit(c) != 0;
                               });
        if (key != "radius" && key != "degree" && !is_weight) {
          continue;
        }
      }
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
  add_material_editor(content_, column, eid, entity, document);
  column->addStretch(1);
}

void PropertyPanel::add_material_editor(QWidget* parent, QVBoxLayout* column,
                                        std::uint64_t entity_id, const Entity* entity,
                                        Document* document) {
  auto* header = new QLabel(tr("Material"), parent);
  header->setStyleSheet(QStringLiteral("font-weight: 600; color: #9aa0a6; margin-top: 4px;"));
  column->addWidget(header);

  auto* form = new QFormLayout();
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

  // 材质库快照（拷贝一份，避免在信号 lambda 里持有 Document 裸指针 —— 面板可跨文档存活）。
  auto materials = std::make_shared<std::unordered_map<std::uint64_t, Material>>();
  Material current;  // 默认材质（灰），entity 未赋值材质时的初始快照
  if (document != nullptr) {
    for (const auto& [mid, mat] : document->materials()) {
      (*materials)[mid] = mat;
    }
    if (entity->material_id != 0) {
      if (const Material* m = document->material(entity->material_id)) {
        current = *m;
      }
    }
  }
  auto current_sp = std::make_shared<Material>(current);

  // 颜色色块：点击弹 QColorDialog，改 base_color 并转成自定义材质（id=0 → 新建）。
  auto color_style = [](const QColor& c) {
    return QStringLiteral("background-color: %1; border: 1px solid #555; min-height: 22px;")
        .arg(c.name());
  };
  auto* color_button = new QPushButton(parent);
  color_button->setCursor(Qt::PointingHandCursor);
  color_button->setStyleSheet(color_style(
      QColor::fromRgbF(current_sp->base_color.x, current_sp->base_color.y, current_sp->base_color.z)));

  auto* rough_spin = new QDoubleSpinBox(parent);
  rough_spin->setRange(0.0, 1.0);
  rough_spin->setDecimals(2);
  rough_spin->setSingleStep(0.05);
  rough_spin->setKeyboardTracking(false);
  rough_spin->setValue(current_sp->roughness);

  auto* metal_spin = new QDoubleSpinBox(parent);
  metal_spin->setRange(0.0, 1.0);
  metal_spin->setDecimals(2);
  metal_spin->setSingleStep(0.05);
  metal_spin->setKeyboardTracking(false);
  metal_spin->setValue(current_sp->metallic);

  // 材质下拉：选库材质 → 引用（保留 id）；列表含自定义材质（空名显示 "(Custom)"）。
  auto* combo = new QComboBox(parent);
  std::vector<std::uint64_t> ids;
  ids.reserve(materials->size());
  for (const auto& [mid, unused] : *materials) {
    (void)unused;
    ids.push_back(mid);
  }
  std::sort(ids.begin(), ids.end());  // 稳定顺序：Default（id=1）排最前
  int selected = -1;
  for (const std::uint64_t mid : ids) {
    const Material& mat = materials->at(mid);
    const QString label = material_display_name(mat.name);
    combo->addItem(label, static_cast<qulonglong>(mid));
    if (entity->material_id == mid) {
      selected = combo->count() - 1;
    }
  }
  if (selected >= 0) {
    combo->setCurrentIndex(selected);
  }

  // 刷新颜色色块 + 数值 spinbox（不触发它们的 valueChanged/clicked 回环）。
  auto refresh_widgets = [color_button, rough_spin, metal_spin, color_style](const Material& m) {
    const QSignalBlocker b0(color_button);
    const QSignalBlocker b1(rough_spin);
    const QSignalBlocker b2(metal_spin);
    color_button->setStyleSheet(
        color_style(QColor::fromRgbF(m.base_color.x, m.base_color.y, m.base_color.z)));
    rough_spin->setValue(m.roughness);
    metal_spin->setValue(m.metallic);
  };

  connect(color_button, &QPushButton::clicked, this,
          [this, entity_id, current_sp, color_button, color_style](bool) {
            const QColor initial = QColor::fromRgbF(current_sp->base_color.x,
                                                    current_sp->base_color.y,
                                                    current_sp->base_color.z);
            const QColor chosen = QColorDialog::getColor(initial, this, tr("Material Color"));
            if (!chosen.isValid()) {
              return;
            }
            current_sp->base_color = {static_cast<float>(chosen.redF()),
                                      static_cast<float>(chosen.greenF()),
                                      static_cast<float>(chosen.blueF())};
            current_sp->id = 0;  // 改色 → 新建自定义材质
            current_sp->name.clear();
            color_button->setStyleSheet(color_style(chosen));
            emit material_edited(entity_id, *current_sp);
          });

  connect(rough_spin, &QDoubleSpinBox::valueChanged, this,
          [this, entity_id, current_sp](double value) {
            current_sp->roughness = static_cast<float>(value);
            current_sp->id = 0;
            current_sp->name.clear();
            emit material_edited(entity_id, *current_sp);
          });

  connect(metal_spin, &QDoubleSpinBox::valueChanged, this,
          [this, entity_id, current_sp](double value) {
            current_sp->metallic = static_cast<float>(value);
            current_sp->id = 0;
            current_sp->name.clear();
            emit material_edited(entity_id, *current_sp);
          });

  connect(combo, &QComboBox::currentIndexChanged, this,
          [this, combo, entity_id, current_sp, materials, refresh_widgets](int index) {
            if (index < 0) {
              return;
            }
            const auto mid = static_cast<std::uint64_t>(combo->itemData(index).toULongLong());
            const auto it = materials->find(mid);
            if (it == materials->end()) {
              return;
            }
            *current_sp = it->second;  // 引用库材质（保留 id）
            refresh_widgets(*current_sp);
            emit material_edited(entity_id, *current_sp);
          });

  form->addRow(tr("Preset"), combo);
  form->addRow(tr("Color"), color_button);
  form->addRow(tr("Roughness"), rough_spin);
  form->addRow(tr("Metallic"), metal_spin);

  // Albedo 贴图：选图 → QImage 解码成 RGBA8 → add_texture 入库 → 引用该纹理 id。
  auto albedo_label = [](std::uint64_t id) {
    return id == 0 ? tr("None") : tr("Texture #%1").arg(id);
  };
  auto* tex_button = new QPushButton(albedo_label(current_sp->albedo_texture_id), parent);
  tex_button->setCursor(Qt::PointingHandCursor);
  auto* tex_clear = new QPushButton(tr("Clear"), parent);
  tex_clear->setEnabled(current_sp->albedo_texture_id != 0);

  connect(tex_button, &QPushButton::clicked, this,
          [this, entity_id, current_sp, document, tex_button, tex_clear, albedo_label](bool) {
            const QString path =
                QFileDialog::getOpenFileName(this, tr("Select albedo texture"), QString(),
                                             tr("Images (*.png *.jpg *.jpeg *.bmp)"));
            if (path.isEmpty()) {
              return;
            }
            QImage image(path);
            if (image.isNull()) {
              return;
            }
            image = image.convertToFormat(QImage::Format_RGBA8888);
            TextureAsset asset{};
            asset.width = static_cast<std::uint32_t>(image.width());
            asset.height = static_cast<std::uint32_t>(image.height());
            const auto* bits = image.constBits();
            asset.rgba.assign(bits, bits + image.sizeInBytes());
            const std::uint64_t tid = document->add_texture(std::move(asset)).id;
            current_sp->albedo_texture_id = tid;
            current_sp->id = 0;  // 改贴图 → 新建自定义材质
            current_sp->name.clear();
            tex_button->setText(albedo_label(tid));
            tex_clear->setEnabled(true);
            emit material_edited(entity_id, *current_sp);
          });

  connect(tex_clear, &QPushButton::clicked, this,
          [this, entity_id, current_sp, tex_button, tex_clear, albedo_label](bool) {
            current_sp->albedo_texture_id = 0;
            current_sp->id = 0;
            current_sp->name.clear();
            tex_button->setText(albedo_label(0));
            tex_clear->setEnabled(false);
            emit material_edited(entity_id, *current_sp);
          });

  auto* tex_row = new QWidget(parent);
  auto* tex_layout = new QHBoxLayout(tex_row);
  tex_layout->setContentsMargins(0, 0, 0, 0);
  tex_layout->setSpacing(4);
  tex_layout->addWidget(tex_button, 1);
  tex_layout->addWidget(tex_clear);
  form->addRow(tr("Albedo texture"), tex_row);

  column->addLayout(form);
}

}  // namespace tamias
