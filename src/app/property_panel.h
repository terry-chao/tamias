#pragma once

#include "entity/entity.h"
#include "engine/modeling/feature.h"
#include "engine/render/material.h"

#include <QString>
#include <QWidget>
#include <cstdint>

class QVBoxLayout;

namespace tamias {

class Document;

// 右侧「属性」面板：展示选中实体的特征树参数与材质，可编辑。
// 参数编辑提交后发 param_edited；材质编辑发 material_edited，由 MainWindow 转成命令（可撤销）。
class PropertyPanel final : public QWidget {
  Q_OBJECT
 public:
  explicit PropertyPanel(QWidget* parent = nullptr);

  // 展示实体：非空则重建编辑器；空则显示 fallback_note 占位提示。
  void show_entity(const Entity* entity, const Document* document, const QString& fallback_note);

 signals:
  // 用户提交了一个参数修改（spinbox valueChanged，keyboard tracking 已关）。
  void param_edited(std::uint64_t entity_id, std::uint64_t feature_id,
                    const QString& param_name, double value);
  // 用户改了材质（下拉选库材质 → 引用；改色/roughness/metallic → 新建自定义材质）。
  void material_edited(std::uint64_t entity_id, Material material);

 private:
  static QString entity_label(EntityKind kind);
  // 参数在实体语义下的显示名（墙：厚度/长度/高度；盒子：宽/深/高；圆柱：半径/高）。
  static QString param_label(EntityKind entity_kind, FeatureKind feature_kind,
                             const QString& param_name);
  // 追加材质编辑区（下拉 / 颜色 / roughness / metallic）到 column。
  void add_material_editor(QWidget* parent, QVBoxLayout* column, std::uint64_t entity_id,
                           const Entity* entity, const Document* document);

  QVBoxLayout* root_ = nullptr;
  QWidget* content_ = nullptr;  // 每次刷新整块重建，避免 removeRow 语义踩坑。
};

}  // namespace tamias
