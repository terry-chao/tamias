#pragma once

#include "engine/math/math.h"
#include "engine/render/debug_vertex_overlay.h"
#include "engine/render/render_scene.h"

#include <QWidget>
#include <cstdint>

class QCheckBox;
class QFormLayout;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSplitter;
class QTableWidget;
class QTabWidget;

namespace tamias {

// 可视化调试：场景摘要 + 绘制清单 + 选中项属性 / 贴图 / 顶点。
// 点选绘制画 AABB，点选顶点画标记；可保存快照、钉金样、写出 OBJ/PPM。
class RenderSceneInspector final : public QWidget {
  Q_OBJECT
 public:
  explicit RenderSceneInspector(QWidget* parent = nullptr);

  void show_scene(const RenderScene& scene);
  void clear();
  void select_node(quint64 node_id);
  [[nodiscard]] int current_draw_index() const;
  [[nodiscard]] const RenderScene* current_scene() const {
    return has_scene_ ? &scene_ : nullptr;
  }

 signals:
  void overlay_requested(Aabb box, quint64 node_id, bool isolate);
  void overlay_cleared();
  void vertex_overlay_requested(DebugVertexOverlay vertex);
  void vertex_overlay_cleared();
  void refresh_requested();
  void save_requested();
  void pin_requested();
  void dump_requested();
  void dump_selected_requested();

 private:
  void rebuild();
  void show_empty();
  void apply_filter();
  void on_draw_selected();
  void on_vertex_selected();
  void fill_selected_draw(int index);
  void fill_vertices(std::uint64_t mesh_id);
  void set_vertices_tab_title(int count);
  void set_texture_thumb(QLabel* image, QLabel* caption, std::uint64_t tex_id, const QString& kind);
  void emit_overlay();
  void emit_vertex_overlay();
  void set_color_chip(QLabel* chip, const Vec3& color);

  RenderScene scene_;
  bool has_scene_ = false;

  QLabel* empty_ = nullptr;
  QWidget* body_ = nullptr;

  QLabel* summary_mode_ = nullptr;
  QLabel* summary_counts_ = nullptr;
  QLabel* summary_digest_ = nullptr;
  QLabel* summary_camera_ = nullptr;

  QLineEdit* filter_ = nullptr;
  QTableWidget* draws_ = nullptr;
  QCheckBox* isolate_ = nullptr;

  QLabel* field_node_ = nullptr;
  QLabel* field_mesh_ = nullptr;
  QLabel* field_flags_ = nullptr;
  QLabel* field_aabb_ = nullptr;
  QLabel* field_translation_ = nullptr;
  QLabel* field_transform_ = nullptr;
  QLabel* field_pbr_ = nullptr;
  QLabel* color_chip_ = nullptr;
  QLabel* category_chip_ = nullptr;
  QLabel* albedo_image_ = nullptr;
  QLabel* albedo_caption_ = nullptr;
  QLabel* normal_image_ = nullptr;
  QLabel* normal_caption_ = nullptr;

  QTableWidget* verts_ = nullptr;
  QWidget* verts_page_ = nullptr;
  QTabWidget* extras_ = nullptr;
  QPlainTextEdit* dump_ = nullptr;
};

}  // namespace tamias
