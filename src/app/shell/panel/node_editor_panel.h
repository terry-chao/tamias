#pragma once

#include <QWidget>

#include <cstdint>
#include <map>

class QLabel;
class QTimer;
class QToolButton;

namespace tamias {

class Body;
class DocumentViewport;
class NodeCanvas;

// 节点编辑面板（底部 dock）：工具栏 + 画布 + 状态栏，外加两件画布不管的事——
//
//  - 求值调度：画布编辑 → 30ms 防抖 → 预算内同步求值（P1 全在 UI 线程，
//    estimated_millis 超预算就挂「待计算」不硬算；P2 换后台线程）；
//  - 预览同步：收集「预览开着的节点 + output 节点」的 Body 输出 → 细分 →
//    Document::set_node_preview，视口下一帧就能看到。细分结果按 Body 指针
//    身份缓存——没重算的节点不重细分。
class NodeEditorPanel final : public QWidget {
  Q_OBJECT
 public:
  explicit NodeEditorPanel(QWidget* parent = nullptr);

  // 文档标签切换时由 MainWindow 调；nullptr = 没有打开的文档。
  void set_viewport(DocumentViewport* viewport);

 private:
  void schedule_evaluation();
  void evaluate_now(bool force = false);
  void rebuild_preview();
  void bake();
  void poll_graph_changes();
  void update_status_label();
  [[nodiscard]] std::uint64_t graph_signature() const;

  NodeCanvas* canvas_ = nullptr;
  QToolButton* add_button_ = nullptr;
  QToolButton* frame_button_ = nullptr;
  QToolButton* bake_button_ = nullptr;
  QToolButton* solve_button_ = nullptr;
  QLabel* status_label_ = nullptr;

  QTimer* eval_timer_ = nullptr;   // 编辑防抖
  QTimer* poll_timer_ = nullptr;   // 轮询外部变化（undo / redo / 加载）
  DocumentViewport* viewport_ = nullptr;
  std::uint64_t last_signature_ = 0;
  bool has_signature_ = false;

  // Body 指针身份 → 细分出的 mesh 资产 id。干净节点的 Body 不变 → 命中缓存。
  std::map<const Body*, std::uint64_t> tess_cache_;
};

}  // namespace tamias
