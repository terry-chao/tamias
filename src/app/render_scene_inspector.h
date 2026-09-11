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
  void set_actions_visible(bool visible);
  [[nodiscard]] int current_draw_index() const;
  [[nodiscard]] const RenderScene* current_scene() const {
    return has_scene_ ? &scene_ : nullptr;
  }

 signals:
  void overlay_requested(Aabb box, quint64 node_id, bool isolate);
  void overlay_cleared();
  void vertex_overlay_requested(DebugVertexOverlay vertex);
  void vertex_overlay_cleared();
  // 选中三角面时，传世界空间三顶点；视口画三边高亮。
  void triangle_overlay_requested(Vec3 v0, Vec3 v1, Vec3 v2);
  void triangle_overlay_cleared();
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
  void on_triangle_selected();
  void on_face_selected();
  void fill_selected_draw(int index);
  void fill_vertices(std::uint64_t mesh_id);
  void fill_triangles(std::uint64_t mesh_id);
  void fill_faces(std::uint64_t mesh_id);
  void set_vertices_tab_title(int count);
  void set_triangles_tab_title(int count);
  void set_faces_tab_title(int count);
  void set_texture_thumb(QLabel* image, QLabel* caption, std::uint64_t tex_id, const QString& kind);
  void emit_overlay();
  void emit_vertex_overlay();
  void emit_triangle_overlay();
  void set_color_chip(QLabel* chip, const Vec3& color);

  RenderScene scene_;
  bool has_scene_ = false;

  QLabel* empty_ = nullptr;
  QWidget* body_ = nullptr;

  QLabel* summary_mode_ = nullptr;
  QLabel* summary_counts_ = nullptr;
  QLabel* summary_digest_ = nullptr;
  QLabel* summary_camera_ = nullptr;
  QLabel* summary_camera2_ = nullptr;
  QLabel* summary_source_ = nullptr;
  QWidget* actions_ = nullptr;

  QLineEdit* filter_ = nullptr;
  QTableWidget* draws_ = nullptr;
  QCheckBox* isolate_ = nullptr;

  QLabel* field_node_ = nullptr;
  QLabel* field_mesh_ = nullptr;
  QLabel* field_flags_ = nullptr;
  QLabel* field_aabb_ = nullptr;
  QLabel* field_translation_ = nullptr;
  QTableWidget* field_transform_ = nullptr;
  QLabel* field_pbr_ = nullptr;
  QLabel* field_tex_ = nullptr;
  QLabel* color_chip_ = nullptr;
  QLabel* category_chip_ = nullptr;
  QLabel* albedo_image_ = nullptr;
  QLabel* albedo_caption_ = nullptr;
  QLabel* normal_image_ = nullptr;
  QLabel* normal_caption_ = nullptr;
  QLabel* orm_image_ = nullptr;
  QLabel* orm_caption_ = nullptr;

  QTableWidget* verts_ = nullptr;
  QWidget* verts_page_ = nullptr;
  QTableWidget* tris_ = nullptr;
  QWidget* tris_page_ = nullptr;
  QTableWidget* faces_ = nullptr;
  QWidget* faces_page_ = nullptr;
  QTabWidget* extras_ = nullptr;
  QPlainTextEdit* dump_ = nullptr;
};

}  // namespace tamias
