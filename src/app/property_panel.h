#pragma once

#include "entity/entity.h"
#include "engine/document/scene.h"
#include "engine/modeling/feature.h"
#include "engine/render/material.h"
#include "engine/render/texture_asset.h"

#include <QString>
#include <QWidget>
#include <cstdint>
#include <string>

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
  void show_entity(const Entity* entity, Document* document, const QString& fallback_note);
  void show_imported_mesh(const SceneNode* node, Document* document);

 signals:
  void param_edited(std::uint64_t entity_id, std::uint64_t feature_id,
                    const QString& param_name, double value);
  // 用户改了材质（下拉选库材质 → 引用；改色/roughness/metallic/贴图/缩放 → 新建自定义材质）。
  void material_edited(std::uint64_t entity_id, Material material);
  void material_shared_updated(Material material);
  void texture_import_requested(quint64 target_id, TextureAsset asset, int slot, bool edit_shared);
  void location_edited(std::uint64_t entity_id, std::uint64_t storey_id,
                       double elevation_offset);

 private:
  static QString entity_label(EntityKind kind);
  static QString param_label(EntityKind entity_kind, FeatureKind feature_kind,
                             const QString& param_name);
  static QString material_display_name(const std::string& name);
  void add_material_editor(QWidget* parent, QVBoxLayout* column, std::uint64_t target_id,
                           std::uint64_t current_material_id, Document* document);

  QVBoxLayout* root_ = nullptr;
  QWidget* content_ = nullptr;
};

}  // namespace tamias
