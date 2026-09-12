#include "property_panel.h"

#include "engine/document/document.h"
#include "texture_image.h"

#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
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
    case EntityKind::StructuralWall:
      return tr("Structural Wall");
    case EntityKind::Foundation:
      return tr("Foundation");
    case EntityKind::CurtainWall:
      return tr("Curtain Wall");
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
  const bool rect_like = feature_kind == FeatureKind::RectProfile ||
                         feature_kind == FeatureKind::PolygonProfile;
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
      if (rect_like) {
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
      if (rect_like) {
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
      if (rect_like) {
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
        if (is("sill")) {
          return tr("Sill Height");
        }
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
    case EntityKind::StructuralWall:
    case EntityKind::CurtainWall:
      // 与墙同造型：RectProfile(width=厚度, height=长度) + Extrude(depth=高度)。
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
    case EntityKind::Foundation:
      // 与板同造型：RectProfile(width=长, height=宽) + Extrude(depth=高)。
      if (rect_like) {
        if (is("width")) {
          return tr("Length");
        }
        if (is("height")) {
          return tr("Width");
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
    // 门把手是内部造型细节，不作为可编辑特征暴露。
    if (feature.params.find("handle_part") != feature.params.end() ||
        feature.params.find("handle_fuse") != feature.params.end()) {
      continue;
    }
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
      if (feature.kind == FeatureKind::PolygonProfile &&
          (key == "n" || (!key.empty() && key[0] == 'p'))) {
        continue;
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
  add_material_editor(content_, column, eid, entity->material_id, document);
  column->addStretch(1);
}

void PropertyPanel::show_imported_mesh(const SceneNode* node, Document* document) {
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
  if (node == nullptr) {
    header->setText(tr("No selection"));
    column->addWidget(header);
    column->addStretch(1);
    return;
  }
  header->setText(tr("Imported mesh — %1").arg(QString::fromStdString(node->name)));
  column->addWidget(header);
  add_material_editor(content_, column, node->id, node->material_id, document);
  column->addStretch(1);
}

void PropertyPanel::add_material_editor(QWidget* parent, QVBoxLayout* column,
                                        std::uint64_t target_id, std::uint64_t current_material_id,
                                        Document* document) {
  auto* header = new QLabel(tr("Material"), parent);
  header->setStyleSheet(QStringLiteral("font-weight: 600; color: #9aa0a6; margin-top: 4px;"));
  column->addWidget(header);

  auto* form = new QFormLayout();
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

  auto materials = std::make_shared<std::unordered_map<std::uint64_t, Material>>();
  Material current;
  std::uint32_t user_count = 0;
  if (document != nullptr) {
    for (const auto& [mid, mat] : document->materials()) {
      (*materials)[mid] = mat;
    }
    if (current_material_id != 0) {
      if (const Material* m = document->material(current_material_id)) {
        current = *m;
      }
      user_count = document->material_user_count(current_material_id);
    }
  }
  auto current_sp = std::make_shared<Material>(current);

  auto* apply_all = new QCheckBox(tr("Apply to all objects using this material"), parent);
  apply_all->setChecked(current.id != 0 && user_count <= 1);
  apply_all->setEnabled(current.id != 0);

  auto commit_edit = [this, target_id, current_sp, apply_all]() {
    if (apply_all->isChecked() && current_sp->id != 0) {
      emit material_shared_updated(*current_sp);
      return;
    }
    current_sp->id = 0;
    current_sp->name.clear();
    emit material_edited(target_id, *current_sp);
  };

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

  auto* world_spin = new QDoubleSpinBox(parent);
  world_spin->setRange(0.05, 50.0);
  world_spin->setDecimals(2);
  world_spin->setSingleStep(0.1);
  world_spin->setKeyboardTracking(false);
  world_spin->setValue(current_sp->tex.world_scale);

  auto* uv_spin = new QDoubleSpinBox(parent);
  uv_spin->setRange(0.01, 100.0);
  uv_spin->setDecimals(2);
  uv_spin->setSingleStep(0.1);
  uv_spin->setKeyboardTracking(false);
  uv_spin->setValue(current_sp->tex.scale.x);

  auto* combo = new QComboBox(parent);
  std::vector<std::uint64_t> ids;
  ids.reserve(materials->size());
  for (const auto& [mid, unused] : *materials) {
    (void)unused;
    ids.push_back(mid);
  }
  std::sort(ids.begin(), ids.end());
  int selected = -1;
  for (const std::uint64_t mid : ids) {
    const Material& mat = materials->at(mid);
    combo->addItem(material_display_name(mat.name), static_cast<qulonglong>(mid));
    if (current_material_id == mid) {
      selected = combo->count() - 1;
    }
  }
  if (selected >= 0) {
    combo->setCurrentIndex(selected);
  }

  auto make_texture_row = [this, target_id, current_sp, document, parent, apply_all](
                              std::uint64_t Material::* field, bool srgb, TextureUsage usage,
                              int slot, const QString& dialog_title) {
    auto* combo = new QComboBox(parent);
    auto fill_combo = [this, document, combo, usage](std::uint64_t current_id) {
      const QSignalBlocker block(combo);
      combo->clear();
      combo->addItem(tr("None"), static_cast<qulonglong>(0));
      if (document == nullptr) {
        combo->setCurrentIndex(0);
        return;
      }
      std::vector<std::uint64_t> ids;
      ids.reserve(document->textures().size());
      for (const auto& [tid, tex] : document->textures()) {
        if (tid == current_id || tex.usage == usage || tex.usage == TextureUsage::Unknown) {
          ids.push_back(tid);
        }
      }
      std::sort(ids.begin(), ids.end());
      for (const std::uint64_t tid : ids) {
        const TextureAsset* tex = document->texture(tid);
        if (tex == nullptr) {
          continue;
        }
        combo->addItem(texture_display_name(*tex), static_cast<qulonglong>(tid));
      }
      int idx = combo->findData(static_cast<qulonglong>(current_id));
      if (idx < 0 && current_id != 0) {
        if (const TextureAsset* tex = document->texture(current_id)) {
          combo->addItem(texture_display_name(*tex), static_cast<qulonglong>(current_id));
          idx = combo->count() - 1;
        }
      }
      combo->setCurrentIndex(idx >= 0 ? idx : 0);
    };
    fill_combo(current_sp.get()->*field);

    auto apply_texture = [this, target_id, current_sp, field, apply_all](std::uint64_t tid) {
      if (current_sp.get()->*field == tid) {
        return;
      }
      current_sp.get()->*field = tid;
      if (apply_all->isChecked() && current_sp->id != 0) {
        emit material_shared_updated(*current_sp);
        return;
      }
      current_sp->id = 0;
      current_sp->name.clear();
      emit material_edited(target_id, *current_sp);
    };

    connect(combo, &QComboBox::currentIndexChanged, this, [combo, apply_texture](int index) {
      if (index < 0) {
        return;
      }
      apply_texture(static_cast<std::uint64_t>(combo->itemData(index).toULongLong()));
    });

    auto* import_btn = new QPushButton(tr("Import..."), parent);
    connect(import_btn, &QPushButton::clicked, this,
            [this, target_id, apply_all, srgb, usage, slot, dialog_title](bool) {
              const QString path = QFileDialog::getOpenFileName(
                  this, dialog_title, QString(), tr("Images (*.png *.jpg *.jpeg *.bmp)"));
              if (path.isEmpty()) {
                return;
              }
              auto asset = load_texture_image(path, usage, srgb);
              if (!asset) {
                return;
              }
              emit texture_import_requested(static_cast<quint64>(target_id), std::move(*asset), slot,
                                            apply_all->isChecked());
            });

    auto* row = new QWidget(parent);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addWidget(combo, 1);
    layout->addWidget(import_btn);
    return std::pair{row, std::pair{combo, fill_combo}};
  };
  const auto albedo_row = make_texture_row(&Material::albedo_texture_id, true, TextureUsage::Albedo,
                                           0, tr("Select albedo texture"));
  const auto normal_row = make_texture_row(&Material::normal_texture_id, false, TextureUsage::Normal,
                                           1, tr("Select normal texture"));
  const auto orm_row =
      make_texture_row(&Material::orm_texture_id, false, TextureUsage::Orm, 2, tr("Select ORM texture"));

  auto refresh_widgets = [color_button, rough_spin, metal_spin, world_spin, uv_spin, color_style,
                          albedo_row, normal_row, orm_row, apply_all](const Material& m) {
    const QSignalBlocker b0(color_button);
    const QSignalBlocker b1(rough_spin);
    const QSignalBlocker b2(metal_spin);
    const QSignalBlocker b3(world_spin);
    const QSignalBlocker b4(uv_spin);
    color_button->setStyleSheet(
        color_style(QColor::fromRgbF(m.base_color.x, m.base_color.y, m.base_color.z)));
    rough_spin->setValue(m.roughness);
    metal_spin->setValue(m.metallic);
    world_spin->setValue(m.tex.world_scale);
    uv_spin->setValue(m.tex.scale.x);
    albedo_row.second.second(m.albedo_texture_id);
    normal_row.second.second(m.normal_texture_id);
    orm_row.second.second(m.orm_texture_id);
    apply_all->setEnabled(m.id != 0);
  };

  connect(color_button, &QPushButton::clicked, this,
          [this, current_sp, color_button, color_style, commit_edit](bool) {
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
            color_button->setStyleSheet(color_style(chosen));
            commit_edit();
          });

  connect(rough_spin, &QDoubleSpinBox::valueChanged, this,
          [current_sp, commit_edit](double value) {
            current_sp->roughness = static_cast<float>(value);
            commit_edit();
          });
  connect(metal_spin, &QDoubleSpinBox::valueChanged, this,
          [current_sp, commit_edit](double value) {
            current_sp->metallic = static_cast<float>(value);
            commit_edit();
          });
  connect(world_spin, &QDoubleSpinBox::valueChanged, this,
          [current_sp, commit_edit](double value) {
            current_sp->tex.world_scale = static_cast<float>(value);
            commit_edit();
          });
  connect(uv_spin, &QDoubleSpinBox::valueChanged, this, [current_sp, commit_edit](double value) {
    const auto s = static_cast<float>(value);
    current_sp->tex.scale = {s, s};
    commit_edit();
  });

  connect(combo, &QComboBox::currentIndexChanged, this,
          [this, combo, target_id, current_sp, materials, refresh_widgets, apply_all](int index) {
            if (index < 0) {
              return;
            }
            const auto mid = static_cast<std::uint64_t>(combo->itemData(index).toULongLong());
            const auto it = materials->find(mid);
            if (it == materials->end()) {
              return;
            }
            *current_sp = it->second;
            apply_all->setChecked(true);
            apply_all->setEnabled(true);
            refresh_widgets(*current_sp);
            emit material_edited(target_id, *current_sp);
          });

  auto* unique_btn = new QPushButton(tr("Make unique copy"), parent);
  connect(unique_btn, &QPushButton::clicked, this, [this, target_id, current_sp, apply_all](bool) {
    apply_all->setChecked(false);
    current_sp->id = 0;
    current_sp->name.clear();
    emit material_edited(target_id, *current_sp);
  });

  form->addRow(tr("Preset"), combo);
  form->addRow(QString(), apply_all);
  form->addRow(QString(), unique_btn);
  form->addRow(tr("Color"), color_button);
  form->addRow(tr("Roughness"), rough_spin);
  form->addRow(tr("Metallic"), metal_spin);
  form->addRow(tr("World scale"), world_spin);
  form->addRow(tr("UV scale"), uv_spin);
  form->addRow(tr("Albedo texture"), albedo_row.first);
  form->addRow(tr("Normal texture"), normal_row.first);
  form->addRow(tr("ORM texture"), orm_row.first);

  column->addLayout(form);
}

}  // namespace tamias
