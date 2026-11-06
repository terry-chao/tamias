#include "app/shell/panel/node_editor_panel.h"

#include "app/shell/panel/node_canvas.h"
#include "app/viewport/canvas/document_viewport.h"
#include "engine/modeling/linked_kernels.h"
#include "engine/modeling/nodes/node_eval.h"
#include "engine/modeling/nodes/node_type.h"
#include "host/session.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <set>
#include <vector>

namespace tamias {

namespace {

// 同步求值的预算：超过就挂「待计算」，不硬算卡 UI（P2 换后台线程后取消）。
constexpr double kEvalBudgetMillis = 150.0;
constexpr double kPreviewDeflection = 0.05;

}  // namespace

NodeEditorPanel::NodeEditorPanel(QWidget* parent) : QWidget(parent) {
  canvas_ = new NodeCanvas(this);

  // 顶部一条工具栏：按钮横排靠左、状态靠右，画布吃满剩余高度。
  // 以前是竖排按钮列（4 个按钮 + 状态文字挤在一列）——底部 dock 里又窄又高，
  // 画布被挤得只剩一小块。
  auto* header = new QWidget(this);
  header->setObjectName(QStringLiteral("nodeEditorHeader"));
  auto* header_layout = new QHBoxLayout(header);
  header_layout->setContentsMargins(6, 3, 8, 3);
  header_layout->setSpacing(4);

  const auto make_button = [this](const QString& text, const QString& tip) {
    auto* button = new QToolButton(this);
    button->setText(text);
    button->setToolTip(tip);
    button->setAutoRaise(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::NoFocus);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    return button;
  };

  add_button_ = make_button(tr("Add node"), tr("Search and place a node"));
  frame_button_ = make_button(tr("Frame"), tr("Fit the whole graph in view (F)"));
  solve_button_ =
      make_button(tr("Solve"), tr("Run evaluation now, ignoring the time budget"));
  bake_button_ =
      make_button(tr("Bake"), tr("Bake previewed + output nodes into normal geometry"));

  auto* divider = new QFrame(header);
  divider->setFrameShape(QFrame::VLine);
  divider->setFrameShadow(QFrame::Sunken);

  header_layout->addWidget(add_button_);
  header_layout->addWidget(frame_button_);
  header_layout->addWidget(divider);
  header_layout->addWidget(solve_button_);
  header_layout->addWidget(bake_button_);
  header_layout->addSpacing(8);

  // 状态文字靠右；窗口窄的时候宁可裁字也不能把按钮挤走（Ignored 允许缩到 0）。
  status_label_ = new QLabel(header);
  status_label_->setTextFormat(Qt::PlainText);
  status_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  status_label_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  status_label_->setMinimumWidth(0);
  QFont status_font = status_label_->font();
  status_font.setPointSizeF(std::max(8.0, status_font.pointSizeF() - 1.0));
  status_label_->setFont(status_font);
  QPalette status_palette = status_label_->palette();
  status_palette.setColor(QPalette::WindowText,
                          status_palette.color(QPalette::PlaceholderText));
  status_label_->setPalette(status_palette);
  header_layout->addWidget(status_label_, 1);

  auto* header_line = new QFrame(this);
  header_line->setFrameShape(QFrame::HLine);
  header_line->setFrameShadow(QFrame::Sunken);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(header);
  layout->addWidget(header_line);
  layout->addWidget(canvas_, 1);

  eval_timer_ = new QTimer(this);
  eval_timer_->setSingleShot(true);
  eval_timer_->setInterval(30);
  connect(eval_timer_, &QTimer::timeout, this, [this]() { evaluate_now(); });

  poll_timer_ = new QTimer(this);
  poll_timer_->setInterval(250);
  connect(poll_timer_, &QTimer::timeout, this, &NodeEditorPanel::poll_graph_changes);
  poll_timer_->start();

  connect(canvas_, &NodeCanvas::graph_edited, this,
          &NodeEditorPanel::schedule_evaluation);
  connect(canvas_, &NodeCanvas::graph_touched, this, [this]() {
    rebuild_preview();
    if (viewport_ != nullptr) {
      // 眼睛开关 / 移动都可能带来新的预览网格：上 GPU + 重画。
      viewport_->resync_all_meshes();
      viewport_->update();
    }
    update_status_label();
  });
  connect(canvas_, &NodeCanvas::status_message, status_label_,
          [this](const QString& text) { status_label_->setText(text); });

  connect(add_button_, &QToolButton::clicked, canvas_, &NodeCanvas::open_palette_centered);
  connect(frame_button_, &QToolButton::clicked, canvas_, &NodeCanvas::frame_all);
  connect(solve_button_, &QToolButton::clicked, this, [this]() { evaluate_now(true); });
  connect(bake_button_, &QToolButton::clicked, this, &NodeEditorPanel::bake);

  update_status_label();
}

void NodeEditorPanel::set_viewport(DocumentViewport* viewport) {
  // 一次页签切换里 currentChanged 和 show_documents 都会调到这儿：同一份视口
  // 只接一次，别重复清缓存 + 强制求值。
  if (viewport_ == viewport) {
    return;
  }
  viewport_ = viewport;
  canvas_->set_session(viewport != nullptr ? &viewport->session() : nullptr);
  tess_cache_.clear();
  has_signature_ = false;
  evaluate_now(true);
}

void NodeEditorPanel::schedule_evaluation() {
  if (viewport_ == nullptr) {
    return;
  }
  eval_timer_->start();
}

void NodeEditorPanel::evaluate_now(bool force) {
  if (viewport_ == nullptr) {
    return;
  }
  Document& document = viewport_->session().document();
  Graph* graph = document.find_node_graph();
  if (graph == nullptr) {
    document.clear_node_preview();
    update_status_label();
    return;
  }

  // 成本门：预估太贵且不是强制 → 挂起，等用户点「求解」或继续编辑（可能变便宜）。
  int dirty_count = 0;
  for (const auto& node : graph->nodes()) {
    if (node->dirty) {
      ++dirty_count;
    }
  }
  if (dirty_count == 0) {
    rebuild_preview();
    update_status_label();
    return;
  }
  const double estimated = graph->estimated_millis();
  if (!force && estimated > kEvalBudgetMillis) {
    status_label_->setText(tr("Pending: %1 node(s), click Solve")
                               .arg(QString::number(dirty_count)));
    return;
  }

  auto kernel = default_kernel();
  if (!kernel) {
    status_label_->setText(
        tr("No modeling kernel: %1").arg(QString::fromStdString(kernel.error())));
    return;
  }
  SolveContext context;
  context.kernel = *kernel;
  (void)evaluate_graph(*graph, context);  // 取消时保留上一轮输出（P1 不会取消）

  rebuild_preview();
  // 新预览网格上 GPU + 重画视口。
  viewport_->resync_all_meshes();
  viewport_->update();
  update_status_label();
}

void NodeEditorPanel::rebuild_preview() {
  if (viewport_ == nullptr) {
    return;
  }
  Document& document = viewport_->session().document();
  Graph* graph = document.find_node_graph();
  if (graph == nullptr) {
    tess_cache_.clear();
    document.clear_node_preview();
    return;
  }
  auto kernel = default_kernel();
  if (!kernel) {
    return;
  }

  // 收集要预览的体：眼睛开着的节点，且真算出了体。
  // （output 节点不再特殊对待——它建出来默认就开着眼睛，烘焙后也会一起关掉。）
  std::vector<NodePreviewEntry> entries;
  std::set<const Body*> live;
  for (const auto& node : graph->nodes()) {
    if (!node->preview || node->outputs.empty()) {
      continue;
    }
    const BodyRef body = node->outputs[0].as_body();
    if (body == nullptr) {
      continue;
    }
    live.insert(body.get());
    std::uint64_t mesh_id = 0;
    const auto cached = tess_cache_.find(body.get());
    if (cached != tess_cache_.end() && document.mesh(cached->second) != nullptr) {
      mesh_id = cached->second;
    } else {
      auto mesh = (*kernel)->tessellate(*body, kPreviewDeflection);
      if (!mesh) {
        continue;  // 出错的体不进预览（节点卡片上有错误信息）
      }
      MeshAsset& stored = document.intern_mesh("node_preview", std::move(*mesh));
      mesh_id = stored.id;
      tess_cache_[body.get()] = mesh_id;
    }
    NodePreviewEntry entry;
    entry.mesh_asset_id = mesh_id;
    entries.push_back(entry);
  }
  // 缓存收缩：不在本次收集里的体删掉（防止撤销 / 删节点后无限增长）。
  for (auto it = tess_cache_.begin(); it != tess_cache_.end();) {
    if (live.count(it->first) == 0) {
      it = tess_cache_.erase(it);
    } else {
      ++it;
    }
  }
  document.set_node_preview(std::move(entries));
}

void NodeEditorPanel::bake() {
  if (viewport_ == nullptr) {
    return;
  }
  // 先自己看一眼能不能烘：命令层只会 log_error，面板上什么都不显示，
  // 用户点完 Bake 毫无反馈（还会以为按钮坏了）。
  Document& document = viewport_->session().document();
  Graph* graph = document.find_node_graph();
  if (graph == nullptr) {
    status_label_->setText(tr("Empty graph"));
    return;
  }
  bool has_output = false;
  for (const auto& node : graph->nodes()) {
    const bool wants =
        node->preview || (node->type != nullptr && node->type->id == "output.preview");
    if (wants && !node->outputs.empty() && node->outputs[0].as_body() != nullptr) {
      has_output = true;
      break;
    }
  }
  if (!has_output) {
    status_label_->setText(tr("Nothing to bake — enable a node's preview first"));
    return;
  }

  // 先记下现有场景节点：烘焙走注册表 dispatch（控制台有回显），
  // 事后用 id 差集认出这次新加出来的实体。
  std::vector<std::uint64_t> before;
  for (const SceneNode& node : document.scene().nodes()) {
    before.push_back(node.id);
  }

  viewport_->run_command("node_bake", {});

  std::vector<std::uint64_t> baked;
  for (const SceneNode& node : document.scene().nodes()) {
    if (std::find(before.begin(), before.end(), node.id) == before.end()) {
      baked.push_back(node.id);
    }
  }
  if (baked.empty()) {
    status_label_->setText(tr("Bake failed — see the console for details"));
    return;
  }

  // 烘焙 = 把橙色预览固化成真几何。这里顺手选中新实体、关掉预览眼睛：
  // 否则橙色预览还盖在同一个位置，看着像「点完什么都没发生」。
  document.clear_selection();
  for (const std::uint64_t id : baked) {
    document.select(id);
  }
  for (const auto& node : graph->nodes()) {
    node->preview = false;
  }
  rebuild_preview();                 // 预览条目清掉
  viewport_->refresh_after_edit();   // 上传网格 + 重算场景 + 重画 + 通知面板
  // 预览开关变化会改签名；同步一下，免得 250ms 轮询用「N nodes · X ms」把提示顶掉。
  last_signature_ = graph_signature();
  has_signature_ = true;
  status_label_->setText(
      tr("Baked %1 object(s) — selected in the viewport")
          .arg(QString::number(static_cast<int>(baked.size()))));
}

void NodeEditorPanel::poll_graph_changes() {
  if (viewport_ == nullptr || !isVisible()) {
    return;
  }
  const std::uint64_t signature = graph_signature();
  if (has_signature_ && signature == last_signature_) {
    return;
  }
  last_signature_ = signature;
  has_signature_ = true;
  // 撤销 / 重做 / 加载 / 切档都能从签名变化里捕捉到 → 重算 + 重画。
  evaluate_now(true);
  canvas_->update();
}

std::uint64_t NodeEditorPanel::graph_signature() const {
  if (viewport_ == nullptr) {
    return 0;
  }
  const Graph* graph = viewport_->session().document().find_node_graph();
  if (graph == nullptr) {
    return 1;
  }
  std::uint64_t signature = graph->version() * 31 + graph->eval_version();
  for (const auto& node : graph->nodes()) {
    signature = signature * 131 + node->id + (node->preview ? 7 : 0) + node->stamp * 17;
  }
  return signature;
}

void NodeEditorPanel::update_status_label() {
  if (viewport_ == nullptr) {
    status_label_->setText(tr("No document"));
    return;
  }
  const Graph* graph = viewport_->session().document().find_node_graph();
  if (graph == nullptr || graph->nodes().empty()) {
    status_label_->setText(tr("Empty graph"));
    return;
  }
  int dirty = 0;
  int errors = 0;
  double millis = 0.0;
  for (const auto& node : graph->nodes()) {
    if (node->dirty) {
      ++dirty;
    }
    if (node->state == NodeState::Error) {
      ++errors;
    }
    millis += node->last_millis;
  }
  QString text = tr("%1 nodes · %2 ms").arg(QString::number(graph->nodes().size()),
                                            QString::number(millis, 'f', 1));
  if (dirty > 0) {
    text += tr(" · pending %1").arg(QString::number(dirty));
  }
  if (errors > 0) {
    text += tr(" · %1 error(s)").arg(QString::number(errors));
  }
  status_label_->setText(text);
}

}  // namespace tamias
