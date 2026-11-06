#include "app/shell/panel/node_canvas.h"

#include "app/shell/panel/node_palette_popup.h"
#include "app/shell/panel/node_ui_text.h"
#include "command/nodes/graph_edit_commands.h"
#include "engine/modeling/nodes/node_eval.h"

#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>

namespace tamias {

namespace {

// —— 卡片尺寸常量（世界单位；MiniCAD NodeEditor 同款观感）——
constexpr double kNodeWidth = 190.0;
constexpr double kHeaderHeight = 26.0;
constexpr double kPortRowHeight = 20.0;
constexpr double kParamRowHeight = 24.0;
constexpr double kPortRadius = 5.5;
constexpr double kSectionGap = 6.0;
constexpr double kBottomPad = 10.0;

// 端口类型色（数值灰、体橙红、向量青、文本黄……连线颜色 = 源端口色）。
QColor port_color(PortType type) {
  switch (type) {
    case PortType::Number:
      return QColor(0x9e, 0xa8, 0xb6);
    case PortType::Integer:
      return QColor(0x7a, 0xa2, 0xc4);
    case PortType::Bool:
      return QColor(0xc4, 0x7a, 0xb0);
    case PortType::Text:
      return QColor(0xc4, 0xb0, 0x7a);
    case PortType::Vector:
      return QColor(0x7a, 0xc4, 0xb0);
    case PortType::Body:
      return QColor(0xd4, 0x6a, 0x4a);
    case PortType::Empty:
      break;
  }
  return QColor(0x70, 0x70, 0x70);
}

// 大类 → 卡片头部色（和 node_category_order 的顺序表一致）。
QColor category_color(const std::string& category) {
  if (category.rfind("输入", 0) == 0) {
    return QColor(0x4a, 0x7d, 0xd4);
  }
  if (category.rfind("实体", 0) == 0) {
    return QColor(0x3f, 0x9d, 0x6b);
  }
  if (category.rfind("变换", 0) == 0) {
    return QColor(0x8a, 0x63, 0xc9);
  }
  if (category.rfind("输出", 0) == 0) {
    return QColor(0xd4, 0x88, 0x4a);
  }
  return QColor(0x80, 0x80, 0x80);
}

// 端口类型在图上的类型（连线的颜色 / 兼容性判断都要它）。
PortType port_type_of(const Graph& graph, NodeId node, int port, bool output) {
  const Graph::Node* n = graph.node(node);
  if (n == nullptr || n->type == nullptr) {
    return PortType::Empty;
  }
  if (output) {
    return static_cast<int>(port) < static_cast<int>(n->type->outputs.size())
               ? n->type->outputs[port].type
               : PortType::Empty;
  }
  return static_cast<int>(port) < static_cast<int>(n->type->inputs.size())
             ? n->type->inputs[port].type
             : PortType::Empty;
}

QString format_number(double value) {
  QString text = QString::number(value, 'f', 2);
  while (text.endsWith(QLatin1Char('0')) && text.contains(QLatin1Char('.'))) {
    text.chop(1);
  }
  if (text.endsWith(QLatin1Char('.'))) {
    text.chop(1);
  }
  return text;
}

QString format_value(const NodeValue& value) {
  switch (value.type()) {
    case PortType::Number:
      return format_number(value.as_number());
    case PortType::Integer:
      return QString::number(value.as_integer());
    case PortType::Bool:
      return value.as_bool() ? QStringLiteral("on") : QStringLiteral("off");
    case PortType::Text:
      return QString::fromStdString(value.as_text());
    case PortType::Vector: {
      const Vec3 v = value.as_vector();
      return format_number(v.x) + QStringLiteral(",") + format_number(v.y) +
             QStringLiteral(",") + format_number(v.z);
    }
    case PortType::Body:
      return QStringLiteral("◈ body");
    case PortType::Empty:
      break;
  }
  return QStringLiteral("—");
}

// 立方贝塞尔采样成折线（QPainter 没有直接的 bezier 图元，MiniCAD 也是采样画）。
// 控制点距离按世界单位算：整张图由 painter 的 translate/scale 统一缩放。
std::vector<QPointF> sample_wire(const QPointF& a, const QPointF& b) {
  const double dx = std::max(40.0, std::abs(b.x() - a.x()) * 0.5);
  const QPointF c1(a.x() + dx, a.y());
  const QPointF c2(b.x() - dx, b.y());
  std::vector<QPointF> points;
  points.reserve(25);
  for (int i = 0; i <= 24; ++i) {
    const double t = static_cast<double>(i) / 24.0;
    const double u = 1.0 - t;
    const double w0 = u * u * u;
    const double w1 = 3.0 * u * u * t;
    const double w2 = 3.0 * u * t * t;
    const double w3 = t * t * t;
    points.emplace_back(w0 * a.x() + w1 * c1.x() + w2 * c2.x() + w3 * b.x(),
                        w0 * a.y() + w1 * c1.y() + w2 * c2.y() + w3 * b.y());
  }
  return points;
}

}  // namespace

NodeCanvas::NodeCanvas(QWidget* parent) : QWidget(parent) {
  setFocusPolicy(Qt::ClickFocus);
  setMouseTracking(true);
  setMinimumSize(200, 160);

  palette_ = new NodePalettePopup(this);
  connect(palette_, &NodePalettePopup::node_chosen, this,
          [this](const NodeTypeInfo* type) { finish_palette_add(type); });

  inline_editor_ = new QLineEdit(this);
  inline_editor_->hide();
  inline_editor_->setFrame(false);
  connect(inline_editor_, &QLineEdit::returnPressed, this,
          &NodeCanvas::commit_inline_editor);
  inline_editor_->installEventFilter(this);
}

bool NodeCanvas::eventFilter(QObject* watched, QEvent* event) {
  // 内联编辑器里按 Esc 放弃编辑。
  if (watched == inline_editor_ && event->type() == QEvent::KeyPress) {
    auto* key = static_cast<QKeyEvent*>(event);
    if (key->key() == Qt::Key_Escape) {
      cancel_inline_editor();
      return true;
    }
  }
  return QWidget::eventFilter(watched, event);
}

void NodeCanvas::set_session(Session* session) {
  session_ = session;
  selection_.clear();
  cancel_inline_editor();
  wire_.active = false;
  drag_ = Drag::None;
  move_origins_.clear();
  scrub_node_ = kInvalidNodeId;
  hover_ = Hit{};
  update();
}

Graph* NodeCanvas::graph() {
  return session_ == nullptr ? nullptr : session_->document().find_node_graph();
}

const Graph* NodeCanvas::graph() const {
  return session_ == nullptr ? nullptr : session_->document().find_node_graph();
}

Document* NodeCanvas::document() { return session_ == nullptr ? nullptr : &session_->document(); }

QPointF NodeCanvas::device_to_world(const QPointF& device) const {
  return QPointF((device.x() - offset_.x()) / zoom_, (device.y() - offset_.y()) / zoom_);
}

QPointF NodeCanvas::world_to_device(const QPointF& world) const {
  return QPointF(world.x() * zoom_ + offset_.x(), world.y() * zoom_ + offset_.y());
}

NodeCanvas::CardLayout NodeCanvas::layout_for(const Graph::Node& node) const {
  CardLayout out;
  double y = node.y + kHeaderHeight + 4;
  const std::size_t outputs = node.type != nullptr ? node.type->outputs.size() : 0;
  const std::size_t inputs = node.type != nullptr ? node.type->inputs.size() : 0;
  const std::size_t params = node.type != nullptr ? node.type->params.size() : 0;
  for (std::size_t i = 0; i < outputs; ++i) {
    out.out_ys.push_back(y + kPortRowHeight * 0.5);
    y += kPortRowHeight;
  }
  if (outputs != 0) {
    y += kSectionGap;
  }
  for (std::size_t i = 0; i < params; ++i) {
    out.param_rects.emplace_back(node.x + 60.0, y + 2.0, kNodeWidth - 60.0 - 8.0,
                                 kParamRowHeight - 4.0);
    y += kParamRowHeight;
  }
  if (params != 0) {
    y += kSectionGap;
  }
  for (std::size_t i = 0; i < inputs; ++i) {
    out.in_ys.push_back(y + kPortRowHeight * 0.5);
    out.input_rects.emplace_back(node.x + 66.0, y + 1.0, kNodeWidth - 66.0 - 10.0,
                                 kPortRowHeight - 2.0);
    y += kPortRowHeight;
  }
  y += kBottomPad;
  out.height = std::max(y - node.y, kHeaderHeight + 30.0);
  out.eye_center = QPointF(node.x + kNodeWidth - 14.0, node.y + kHeaderHeight * 0.5);
  return out;
}

NodeCanvas::Hit NodeCanvas::hit_test(const QPointF& device_pos) const {
  const Graph* g = graph();
  if (g == nullptr) {
    return Hit{};
  }
  const QPointF world = device_to_world(device_pos);
  // 点击余量按屏幕像素折算成世界单位：缩放后端口变小，手感不变。
  const double slack = 3.0 / std::max(zoom_, 0.05);
  // z 序 = 节点 vector 序，倒着找（最上层优先）。
  for (auto it = g->nodes().rbegin(); it != g->nodes().rend(); ++it) {
    const Graph::Node& node = **it;
    const CardLayout layout = layout_for(node);
    // 眼睛。
    if (QLineF(world, layout.eye_center).length() <= layout.eye_radius + slack) {
      return Hit{Hit::Kind::Eye, node.id, 0};
    }
    // 输出端口（右缘）。
    for (std::size_t i = 0; i < layout.out_ys.size(); ++i) {
      if (std::abs(world.y() - layout.out_ys[i]) <= kPortRadius + slack &&
          std::abs(world.x() - (node.x + kNodeWidth)) <= kPortRadius + slack) {
        return Hit{Hit::Kind::OutPort, node.id, static_cast<int>(i)};
      }
    }
    // 输入端口（左缘）。
    for (std::size_t i = 0; i < layout.in_ys.size(); ++i) {
      if (std::abs(world.y() - layout.in_ys[i]) <= kPortRadius + slack &&
          std::abs(world.x() - node.x) <= kPortRadius + slack) {
        return Hit{Hit::Kind::InPort, node.id, static_cast<int>(i)};
      }
    }
    // 参数控件。
    for (std::size_t i = 0; i < layout.param_rects.size(); ++i) {
      if (layout.param_rects[i].contains(world)) {
        return Hit{Hit::Kind::Param, node.id, static_cast<int>(i)};
      }
    }
    // 未连线输入的内联值。
    if (node.type != nullptr) {
      for (std::size_t i = 0; i < layout.input_rects.size() &&
                              i < node.type->inputs.size() && i < layout.in_ys.size();
           ++i) {
        if (g->link_into(node.id, static_cast<std::uint16_t>(i)) != nullptr) {
          continue;  // 有连线：内联值不显示也不可点
        }
        if (layout.input_rects[i].contains(world)) {
          return Hit{Hit::Kind::InField, node.id, static_cast<int>(i)};
        }
      }
    }
    // 头部 / 主体。
    if (world.y() >= node.y && world.y() <= node.y + kHeaderHeight &&
        world.x() >= node.x && world.x() <= node.x + kNodeWidth) {
      return Hit{Hit::Kind::Header, node.id, 0};
    }
    if (world.y() > node.y + kHeaderHeight && world.y() <= node.y + layout.height &&
        world.x() >= node.x && world.x() <= node.x + kNodeWidth) {
      return Hit{Hit::Kind::Body, node.id, 0};
    }
  }
  return Hit{};
}

// Alt+点击的连线命中：对每根线采样后找最近点（阈值随缩放）。
const Link* NodeCanvas::hit_link_at(const QPointF& device_pos) const {
  const Graph* g = graph();
  if (g == nullptr) {
    return nullptr;
  }
  const QPointF world = device_to_world(device_pos);
  const double threshold = 8.0 / std::max(zoom_, 0.1);
  const Link* best = nullptr;
  double best_distance = threshold;
  for (const Link& link : g->links()) {
    const Graph::Node* from = g->node(link.from_node);
    const Graph::Node* to = g->node(link.to_node);
    if (from == nullptr || to == nullptr) {
      continue;
    }
    const CardLayout from_layout = layout_for(*from);
    const CardLayout to_layout = layout_for(*to);
    if (link.from_port >= from_layout.out_ys.size() ||
        link.to_port >= to_layout.in_ys.size()) {
      continue;
    }
    const QPointF a(from->x + kNodeWidth, from_layout.out_ys[link.from_port]);
    const QPointF b(to->x, to_layout.in_ys[link.to_port]);
    for (const QPointF& point : sample_wire(a, b)) {
      const double distance = QLineF(point, world).length();
      if (distance < best_distance) {
        best_distance = distance;
        best = &link;
      }
    }
  }
  return best;
}

template <typename CommandT, typename... Args>
bool NodeCanvas::run_command(Args&&... args) {
  Document* doc = document();
  if (doc == nullptr || session_ == nullptr) {
    return false;
  }
  auto command = std::make_unique<CommandT>(*doc, std::forward<Args>(args)...);
  auto result = command->execute();
  if (!result) {
    emit status_message(node_command_error(result.error()));
    return false;
  }
  session_->command_system().push_executed(std::move(command));
  emit graph_edited();
  update();
  return true;
}

void NodeCanvas::paintEvent(QPaintEvent*) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);

  // 背景 + 网格点。
  painter.fillRect(rect(), palette().color(QPalette::Base));
  const double grid = 24.0 * zoom_;
  if (grid >= 8.0) {
    QPen dot(palette().color(QPalette::Mid));
    painter.setPen(dot);
    for (double x = std::fmod(offset_.x(), grid); x < width(); x += grid) {
      for (double y = std::fmod(offset_.y(), grid); y < height(); y += grid) {
        painter.drawPoint(QPointF(x, y));
      }
    }
  }

  // 没文档和「文档里还没有图」是两回事：面板没接上视口时点 Add node 是没反应的，
  // 画布上直接写清楚，别让人对着空网格猜。
  if (session_ == nullptr) {
    draw_empty_state(painter, tr("No document"),
                     tr("Open or create one to edit nodes"));
    return;
  }

  const Graph* g = graph();
  if (g == nullptr || g->nodes().empty()) {
    draw_empty_state(painter, tr("No nodes yet"),
                     tr("Add one from the toolbar, or right-click the canvas"));
    return;
  }

  // —— 连线（卡片下面）——
  // 从这里开始画的是世界坐标：统一 translate + scale，缩放/平移才真的生效，
  // 也才和 hit_test 的 device_to_world 对得上（以前只画不缩放，一缩放就点不中）。
  painter.save();
  painter.translate(offset_);
  painter.scale(zoom_, zoom_);

  for (const Link& link : g->links()) {
    const Graph::Node* from = g->node(link.from_node);
    const Graph::Node* to = g->node(link.to_node);
    if (from == nullptr || to == nullptr) {
      continue;
    }
    const CardLayout from_layout = layout_for(*from);
    const CardLayout to_layout = layout_for(*to);
    if (link.from_port >= from_layout.out_ys.size() ||
        link.to_port >= to_layout.in_ys.size()) {
      continue;
    }
    const QPointF a(from->x + kNodeWidth, from_layout.out_ys[link.from_port]);
    const QPointF b(to->x, to_layout.in_ys[link.to_port]);
    const QColor color = port_color(port_type_of(*g, link.from_node, link.from_port, true));
    painter.setPen(QPen(color, 2.0));
    painter.setBrush(Qt::NoBrush);
    const std::vector<QPointF> points = sample_wire(a, b);
    painter.drawPolyline(reinterpret_cast<const QPointF*>(points.data()),
                         static_cast<int>(points.size()));
  }

  // —— 拖线 ——
  if (wire_.active) {
    QColor color = port_color(
        port_type_of(*g, wire_.node, wire_.port, wire_.from_output));
    QPen pen(color, 2.0, Qt::DashLine);
    painter.setPen(pen);
    const std::vector<QPointF> points = sample_wire(wire_.world_pos, wire_cursor_world_);
    painter.drawPolyline(reinterpret_cast<const QPointF*>(points.data()),
                         static_cast<int>(points.size()));
  }

  // —— 卡片 ——
  for (const auto& node_ptr : g->nodes()) {
    const Graph::Node& node = *node_ptr;
    const CardLayout layout = layout_for(node);

    // 卡身。
    QRectF card(node.x, node.y, kNodeWidth, layout.height);
    const bool selected = selection_.count(node.id) != 0;
    QColor body_color = palette().color(QPalette::Base).lighter(108);
    QColor border = palette().color(QPalette::Mid);
    if (node.state == NodeState::Error) {
      border = QColor(0xd4, 0x4a, 0x3a);
    } else if (node.state == NodeState::Warning) {
      border = QColor(0xd4, 0x8a, 0x3a);
    } else if (selected) {
      border = palette().color(QPalette::Highlight);
    }
    painter.setBrush(body_color);
    painter.setPen(QPen(border, selected ? 2.0 : 1.0));
    painter.drawRoundedRect(card, 6.0, 6.0);

    // 头部（裁剪成圆角）。
    QPainterPath clip;
    clip.addRoundedRect(card, 6.0, 6.0);
    painter.save();
    painter.setClipPath(clip);
    QColor header = category_color(node.type != nullptr ? node.type->category : std::string());
    painter.fillRect(QRectF(node.x, node.y, kNodeWidth, kHeaderHeight), header);
    painter.restore();

    // 状态点。
    QColor state_color(0x3a, 0xd4, 0x6a);
    if (node.state == NodeState::Warning) {
      state_color = QColor(0xd4, 0x8a, 0x3a);
    } else if (node.state == NodeState::Error) {
      state_color = QColor(0xd4, 0x4a, 0x3a);
    }
    painter.setBrush(state_color);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(QPointF(node.x + 10.0, node.y + kHeaderHeight * 0.5), 3.0, 3.0);

    // 标题 + 类型名（丢失节点显示 type id）。
    painter.setPen(Qt::white);
    QFont title_font = font();
    title_font.setBold(true);
    painter.setFont(title_font);
    const QString title = node.type != nullptr
                              ? QString::fromStdString(node.type->name)
                              : QString::fromStdString(node.type_id);
    painter.drawText(QRectF(node.x + 18.0, node.y, kNodeWidth - 34.0, kHeaderHeight),
                     Qt::AlignVCenter | Qt::AlignLeft,
                     fontMetrics().elidedText(title, Qt::ElideRight,
                                              static_cast<int>(kNodeWidth - 40.0)));
    painter.setFont(font());

    // 预览眼睛。
    painter.setPen(QPen(node.preview ? QColor(0xff, 0xd4, 0x6a) : Qt::white, 1.5));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(layout.eye_center, layout.eye_radius, layout.eye_radius);
    if (node.preview) {
      painter.setBrush(QColor(0xff, 0xd4, 0x6a));
      painter.setPen(Qt::NoPen);
      painter.drawEllipse(layout.eye_center, 2.2, 2.2);
    }

    const QColor text_color = palette().color(QPalette::Text);
    const QColor field_color = palette().color(QPalette::Base).darker(120);

    // 输出端口（右缘圆点 + 名称右对齐）。
    if (node.type != nullptr) {
      for (std::size_t i = 0; i < node.type->outputs.size() && i < layout.out_ys.size(); ++i) {
        const PortSpec& spec = node.type->outputs[i];
        const double y = layout.out_ys[i];
        painter.setBrush(port_color(spec.type));
        painter.setPen(QPen(palette().color(QPalette::Base), 1.0));
        painter.drawEllipse(QPointF(node.x + kNodeWidth, y), kPortRadius, kPortRadius);
        painter.setPen(text_color);
        painter.drawText(QRectF(node.x + 8.0, y - kPortRowHeight * 0.5,
                                kNodeWidth - 20.0, kPortRowHeight),
                         Qt::AlignVCenter | Qt::AlignRight,
                         QString::fromStdString(spec.name));
      }

      // 参数。
      for (std::size_t i = 0; i < node.type->params.size() &&
                              i < layout.param_rects.size() && i < node.params.size();
           ++i) {
        const NodeParamSpec& spec = node.type->params[i];
        const QRectF rect = layout.param_rects[i];
        painter.setPen(text_color);
        painter.drawText(QRectF(node.x + 8.0, rect.top(), 48.0, rect.height()),
                         Qt::AlignVCenter | Qt::AlignLeft,
                         fontMetrics().elidedText(QString::fromStdString(spec.key),
                                                  Qt::ElideRight, 46));
        if (spec.kind == WidgetKind::Toggle) {
          const bool on = node.params[i].as_bool();
          painter.setBrush(on ? QColor(0x4a, 0x9d, 0xd4) : field_color);
          painter.setPen(QPen(palette().color(QPalette::Mid), 1.0));
          painter.drawRoundedRect(rect, rect.height() * 0.5, rect.height() * 0.5);
          painter.setBrush(Qt::white);
          painter.setPen(Qt::NoPen);
          painter.drawEllipse(
              QPointF(on ? rect.right() - rect.height() * 0.5 : rect.left() + rect.height() * 0.5,
                      rect.center().y()),
              rect.height() * 0.32, rect.height() * 0.32);
        } else if (spec.kind == WidgetKind::Choice) {
          painter.setBrush(field_color);
          painter.setPen(QPen(palette().color(QPalette::Mid), 1.0));
          painter.drawRoundedRect(rect, 4.0, 4.0);
          painter.setPen(text_color);
          painter.drawText(rect.adjusted(6, 0, -14, 0), Qt::AlignVCenter | Qt::AlignLeft,
                           QString::fromStdString(node.params[i].as_text()));
          painter.drawText(rect.adjusted(0, 0, -6, 0), Qt::AlignVCenter | Qt::AlignRight,
                           QStringLiteral("▾"));
        } else {
          // Field / Slider。
          painter.setBrush(field_color);
          painter.setPen(QPen(palette().color(QPalette::Mid), 1.0));
          painter.drawRoundedRect(rect, 4.0, 4.0);
          painter.setPen(text_color);
          painter.drawText(rect.adjusted(6, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft,
                           format_value(node.params[i]));
        }
      }

      // 输入端口（左缘圆点 + 名称 + 未连线内联值）。
      for (std::size_t i = 0; i < node.type->inputs.size() && i < layout.in_ys.size() &&
                              i < layout.input_rects.size();
           ++i) {
        const PortSpec& spec = node.type->inputs[i];
        const double y = layout.in_ys[i];
        const bool wired = g->link_into(node.id, static_cast<std::uint16_t>(i)) != nullptr;
        painter.setBrush(wired ? port_color(spec.type) : field_color);
        painter.setPen(QPen(port_color(spec.type), 1.5));
        painter.drawEllipse(QPointF(node.x, y), kPortRadius, kPortRadius);
        painter.setPen(wired ? text_color : palette().color(QPalette::PlaceholderText));
        painter.drawText(QRectF(node.x + 10.0, y - kPortRowHeight * 0.5, 52.0, kPortRowHeight),
                         Qt::AlignVCenter | Qt::AlignLeft,
                         fontMetrics().elidedText(QString::fromStdString(spec.name),
                                                  Qt::ElideRight, 50));
        if (!wired && i < node.input_values.size()) {
          const QRectF rect = layout.input_rects[i];
          painter.setBrush(field_color);
          painter.setPen(QPen(palette().color(QPalette::Mid), 1.0));
          painter.drawRoundedRect(rect, 4.0, 4.0);
          painter.setPen(text_color);
          painter.drawText(rect.adjusted(6, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft,
                           format_value(node.input_values[i]));
        }
      }
    } else {
      // 丢失节点：一行说明。
      painter.setPen(palette().color(QPalette::PlaceholderText));
      painter.drawText(QRectF(node.x + 8.0, node.y + kHeaderHeight + 4.0, kNodeWidth - 16.0,
                              kPortRowHeight),
                       Qt::AlignVCenter | Qt::AlignLeft, tr("unknown type"));
    }

    // 错误 / 警告消息（卡片下方）。
    if (node.state != NodeState::Ok && !node.message.empty()) {
      painter.setPen(node.state == NodeState::Error ? QColor(0xd4, 0x4a, 0x3a)
                                                    : QColor(0xd4, 0x8a, 0x3a));
      const QString message = node_state_message(node.message);
      painter.drawText(QRectF(node.x, node.y + layout.height + 2.0, kNodeWidth + 80.0, 30.0),
                       Qt::AlignTop | Qt::AlignLeft | Qt::TextWordWrap,
                       fontMetrics().elidedText(message, Qt::ElideRight,
                                                static_cast<int>(kNodeWidth + 80.0)));
    }
  }

  painter.restore();

  // 框选矩形（设备坐标：用鼠标位置直接画，不跟缩放走）。
  if (drag_ == Drag::Box) {
    painter.setPen(palette().color(QPalette::Highlight));
    QColor fill = palette().color(QPalette::Highlight);
    fill.setAlpha(40);
    painter.setBrush(fill);
    painter.drawRect(box_rect_device_.normalized());
  }
}

void NodeCanvas::draw_empty_state(QPainter& painter, const QString& title,
                                  const QString& hint) const {
  QFont title_font = font();
  title_font.setPointSizeF(title_font.pointSizeF() + 1.5);
  title_font.setBold(true);
  const QFontMetrics title_metrics(title_font);
  const QFontMetrics hint_metrics(font());

  const QRect area = rect().adjusted(24, 24, -24, -24);
  const int gap = 6;
  const int total = title_metrics.height() + gap + hint_metrics.height();
  const int top = area.center().y() - total / 2;

  painter.setFont(title_font);
  painter.setPen(palette().color(QPalette::Text));
  painter.drawText(QRect(area.left(), top, area.width(), title_metrics.height()),
                   Qt::AlignHCenter | Qt::AlignVCenter,
                   title_metrics.elidedText(title, Qt::ElideRight, area.width()));

  painter.setFont(font());
  painter.setPen(palette().color(QPalette::PlaceholderText));
  painter.drawText(
      QRect(area.left(), top + title_metrics.height() + gap, area.width(),
            hint_metrics.height()),
      Qt::AlignHCenter | Qt::AlignVCenter,
      hint_metrics.elidedText(hint, Qt::ElideRight, area.width()));
}

void NodeCanvas::mousePressEvent(QMouseEvent* event) {
  const QPointF device = event->position();
  // Alt+点击连线 = 删除那根线。
  if ((event->modifiers() & Qt::AltModifier) != 0 && event->button() == Qt::LeftButton) {
    if (const Link* link = hit_link_at(device)) {
      run_command<NodeDisconnectInputCommand>(link->to_node, link->to_port);
    }
    return;
  }

  if (event->button() == Qt::MiddleButton || event->button() == Qt::RightButton) {
    drag_ = Drag::Pan;
    drag_start_device_ = device;
    pan_start_offset_ = offset_;
    right_button_down_ = event->button() == Qt::RightButton;
    right_moved_ = false;
    return;
  }

  if (event->button() != Qt::LeftButton) {
    return;
  }

  // 图是懒建的（全新文档 / 还没加过节点时 find_node_graph() 是 nullptr）。
  // 空图上左键没有可点、可选的东西：直接吞掉。绝不能让 Box 拖拽起飞——
  // 松手时 graph() 还是 nullptr，mouseReleaseEvent 里就空指针了。
  Graph* g = graph();
  if (g == nullptr) {
    drag_ = Drag::None;
    wire_.active = false;
    if (!selection_.empty()) {
      selection_.clear();
      emit selection_changed();
    }
    update();
    return;
  }

  const Hit hit = hit_test(device);
  switch (hit.kind) {
    case Hit::Kind::Eye:
      toggle_preview_flag(hit.node);
      return;
    case Hit::Kind::OutPort: {
      wire_.active = true;
      wire_.from_output = true;
      wire_.node = hit.node;
      wire_.port = hit.index;
      const Graph::Node* node = g->node(hit.node);
      const CardLayout layout = layout_for(*node);
      wire_.world_pos =
          QPointF(node->x + kNodeWidth, layout.out_ys[static_cast<std::size_t>(hit.index)]);
      wire_cursor_world_ = device_to_world(device);
      drag_ = Drag::Wire;
      return;
    }
    case Hit::Kind::InPort: {
      if (const Link* existing = g->link_into(hit.node, static_cast<std::uint16_t>(hit.index))) {
        // 拔线重接：先断开，再从上游输出端口拖。
        const Link removed = *existing;
        run_command<NodeDisconnectInputCommand>(hit.node, hit.index);
        wire_.active = true;
        wire_.from_output = true;
        wire_.node = removed.from_node;
        wire_.port = removed.from_port;
        const Graph::Node* from = g->node(removed.from_node);
        const CardLayout layout = layout_for(*from);
        wire_.world_pos =
            QPointF(from->x + kNodeWidth, layout.out_ys[removed.from_port]);
      } else {
        wire_.active = true;
        wire_.from_output = false;
        wire_.node = hit.node;
        wire_.port = hit.index;
        const Graph::Node* node = g->node(hit.node);
        const CardLayout layout = layout_for(*node);
        wire_.world_pos =
            QPointF(node->x, layout.in_ys[static_cast<std::size_t>(hit.index)]);
      }
      wire_cursor_world_ = device_to_world(device);
      drag_ = Drag::Wire;
      return;
    }
    case Hit::Kind::Param: {
      const Graph::Node* node = g->node(hit.node);
      const NodeParamSpec& spec = node->type->params[static_cast<std::size_t>(hit.index)];
      if (spec.kind == WidgetKind::Toggle) {
        run_command<NodeSetParamCommand>(hit.node, static_cast<std::uint16_t>(hit.index),
                                         NodeValue::boolean(!node->params[hit.index].as_bool()));
      } else if (spec.kind == WidgetKind::Choice) {
        open_choice_menu(*node, hit.index, event->globalPosition().toPoint());
      } else if (spec.default_value.type() == PortType::Number ||
                 spec.default_value.type() == PortType::Integer) {
        drag_ = Drag::Value;
        scrub_param_ = true;
        scrub_node_ = hit.node;
        scrub_index_ = hit.index;
        scrub_start_ = node->params[hit.index];
        drag_start_device_ = device;
      }
      return;
    }
    case Hit::Kind::InField: {
      const Graph::Node* node = g->node(hit.node);
      const std::size_t index = static_cast<std::size_t>(hit.index);
      const PortSpec& spec = node->type->inputs[index];
      if (spec.type == PortType::Bool) {
        run_command<NodeSetInputCommand>(
            hit.node, static_cast<std::uint16_t>(hit.index),
            NodeValue::boolean(!node->input_values[index].as_bool()));
      } else if (spec.type == PortType::Number || spec.type == PortType::Integer) {
        drag_ = Drag::Value;
        scrub_param_ = false;
        scrub_node_ = hit.node;
        scrub_index_ = hit.index;
        scrub_start_ = node->input_values[index];
        drag_start_device_ = device;
      }
      return;
    }
    case Hit::Kind::Header:
    case Hit::Kind::Body: {
      const bool ctrl = (event->modifiers() & Qt::ControlModifier) != 0;
      if (ctrl) {
        if (selection_.count(hit.node) != 0) {
          selection_.erase(hit.node);
        } else {
          selection_.insert(hit.node);
        }
      } else if (selection_.count(hit.node) == 0) {
        selection_.clear();
        selection_.insert(hit.node);
      }
      g->raise_node(hit.node);
      move_origins_.clear();
      for (NodeId id : selection_) {
        if (const Graph::Node* node = g->node(id)) {
          move_origins_[id] = {node->x, node->y};
        }
      }
      drag_ = Drag::Nodes;
      drag_start_device_ = device;
      emit selection_changed();
      update();
      return;
    }
    case Hit::Kind::None:
    default: {
      if ((event->modifiers() & Qt::ControlModifier) == 0) {
        selection_.clear();
        emit selection_changed();
      }
      drag_ = Drag::Box;
      drag_start_device_ = device;
      box_rect_device_ = QRectF(device, device);
      update();
      return;
    }
  }
}

void NodeCanvas::mouseMoveEvent(QMouseEvent* event) {
  const QPointF device = event->position();
  if (drag_ == Drag::None) {
    apply_hover_cursor(device);
    return;
  }
  Graph* g = graph();
  switch (drag_) {
    case Drag::Pan: {
      const QPointF delta = device - drag_start_device_;
      if (delta.manhattanLength() > 3.0) {
        right_moved_ = true;
      }
      offset_ = pan_start_offset_ + delta;
      update();
      break;
    }
    case Drag::Nodes: {
      if (g == nullptr) {
        break;  // 文档换了 / 图没了：这次拖拽作废（松手时也不会写回）。
      }
      const QPointF world_delta =
          device_to_world(device) - device_to_world(drag_start_device_);
      for (auto& [id, origin] : move_origins_) {
        if (Graph::Node* node = g->node(id)) {
          node->x = origin.first + world_delta.x();
          node->y = origin.second + world_delta.y();
        }
      }
      update();
      break;
    }
    case Drag::Wire: {
      wire_cursor_world_ = device_to_world(device);
      update();
      break;
    }
    case Drag::Box: {
      box_rect_device_ = QRectF(drag_start_device_, device);
      update();
      break;
    }
    case Drag::Value: {
      const double dx = device.x() - drag_start_device_.x();
      const double step = (event->modifiers() & Qt::ShiftModifier) != 0 ? 0.01 : 0.1;
      Graph::Node* node = g == nullptr ? nullptr : g->node(scrub_node_);
      if (node == nullptr) {
        return;
      }
      NodeValue value = scrub_start_;
      if (value.type() == PortType::Integer) {
        value = NodeValue::integer(value.as_integer() + static_cast<std::int64_t>(std::lround(dx * step * 10.0)));
      } else {
        double next = value.as_number() + dx * step;
        if ((event->modifiers() & Qt::ControlModifier) != 0) {
          next = std::round(next);
        }
        value = NodeValue::number(next);
      }
      if (scrub_param_) {
        if (static_cast<std::size_t>(scrub_index_) < node->params.size()) {
          node->params[static_cast<std::size_t>(scrub_index_)] = value;
        }
      } else if (static_cast<std::size_t>(scrub_index_) < node->input_values.size()) {
        node->input_values[static_cast<std::size_t>(scrub_index_)] = value;
      }
      emit graph_edited();
      update();
      break;
    }
    case Drag::None:
      break;
  }
}

void NodeCanvas::mouseReleaseEvent(QMouseEvent* event) {
  const QPointF device = event->position();
  Graph* g = graph();
  switch (drag_) {
    case Drag::Pan: {
      if (right_button_down_ && !right_moved_ && event->button() == Qt::RightButton) {
        // 右键点空（没拖动）：菜单。点在节点上时交给平台（不弹）。
        if (hit_test(device).kind == Hit::Kind::None) {
          QMenu menu(this);
          QAction* add = menu.addAction(tr("Add node…"));
          QAction* frame = menu.addAction(tr("Frame all"));
          QAction* chosen = menu.exec(event->globalPosition().toPoint());
          if (chosen == add) {
            wire_.active = false;
            spawn_palette(event->globalPosition().toPoint());
          } else if (chosen == frame) {
            frame_all();
          }
        }
      }
      break;
    }
    case Drag::Nodes: {
      std::vector<NodeMoveCommand::Move> moves;
      for (const auto& [id, origin] : move_origins_) {
        const Graph::Node* node = g == nullptr ? nullptr : g->node(id);
        if (node == nullptr) {
          continue;
        }
        if (std::abs(node->x - origin.first) < 0.001 &&
            std::abs(node->y - origin.second) < 0.001) {
          continue;  // 没动就不进撤销栈
        }
        NodeMoveCommand::Move move;
        move.node_id = id;
        move.from_x = origin.first;
        move.from_y = origin.second;
        move.to_x = node->x;
        move.to_y = node->y;
        moves.push_back(move);
      }
      if (!moves.empty()) {
        run_command<NodeMoveCommand>(std::move(moves));
        emit graph_touched();
      }
      move_origins_.clear();
      break;
    }
    case Drag::Wire: {
      const Hit hit = hit_test(device);
      // 落在输入端口圆点上，或者落在它右边那个内联值框上，都算接这个输入——
      // 端口圆点只有十几个像素，以前必须精确点中它才连得上。
      const bool on_input = hit.kind == Hit::Kind::InPort || hit.kind == Hit::Kind::InField;
      if (hit.node == wire_.node) {
        wire_.active = false;  // 拖回自己身上 = 取消
      } else if (on_input && wire_.from_output) {
        run_command<NodeConnectCommand>(wire_.node, static_cast<std::uint16_t>(wire_.port),
                                        hit.node, static_cast<std::uint16_t>(hit.index));
        wire_.active = false;
      } else if (hit.kind == Hit::Kind::OutPort && !wire_.from_output) {
        run_command<NodeConnectCommand>(hit.node, static_cast<std::uint16_t>(hit.index),
                                        wire_.node, static_cast<std::uint16_t>(wire_.port));
        wire_.active = false;
      } else if (hit.kind == Hit::Kind::None) {
        // 拖到空白：弹搜索面板，选完自动连最佳端口（wire_ 保留给 finish_palette_add）。
        spawn_palette(event->globalPosition().toPoint());
      } else {
        emit status_message(tr("Incompatible ports"));
        wire_.active = false;
      }
      break;
    }
    case Drag::Box: {
      // 图可能从来没有过（空文档），也可能在拖框期间被换掉 —— 没图就没有可选的。
      if (g == nullptr) {
        break;
      }
      const QRectF world_rect(device_to_world(box_rect_device_.topLeft()),
                              device_to_world(box_rect_device_.bottomRight()));
      for (const auto& node : g->nodes()) {
        const CardLayout layout = layout_for(*node);
        const QRectF card(node->x, node->y, kNodeWidth, layout.height);
        if (card.intersects(world_rect.normalized())) {
          selection_.insert(node->id);
        }
      }
      emit selection_changed();
      break;
    }
    case Drag::Value: {
      Graph::Node* node = g == nullptr ? nullptr : g->node(scrub_node_);
      if (node != nullptr) {
        const NodeValue final_value = scrub_param_
                                          ? node->params[static_cast<std::size_t>(scrub_index_)]
                                          : node->input_values[static_cast<std::size_t>(scrub_index_)];
        if (scrub_param_) {
          run_command<NodeSetParamCommand>(scrub_node_, static_cast<std::uint16_t>(scrub_index_),
                                           final_value, scrub_start_);
        } else {
          run_command<NodeSetInputCommand>(scrub_node_, static_cast<std::uint16_t>(scrub_index_),
                                           final_value, scrub_start_);
        }
      }
      break;
    }
    case Drag::None:
      break;
  }
  drag_ = Drag::None;
  right_button_down_ = false;
  update();
}

void NodeCanvas::mouseDoubleClickEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton) {
    return;
  }
  const Hit hit = hit_test(event->position());
  if (hit.kind == Hit::Kind::InField || hit.kind == Hit::Kind::Param) {
    open_inline_editor(hit);
  }
}

void NodeCanvas::wheelEvent(QWheelEvent* event) {
  const QPointF before = device_to_world(event->position());
  const double factor = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
  zoom_ = std::clamp(zoom_ * factor, 0.15, 4.0);
  // 缩放对准光标：世界点保持在同一设备位置。
  offset_ = event->position() - before * zoom_;
  update();
}

void NodeCanvas::keyPressEvent(QKeyEvent* event) {
  switch (event->key()) {
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
      delete_selected();
      return;
    case Qt::Key_F:
      frame_all();
      return;
    case Qt::Key_A:
      if ((event->modifiers() & Qt::ControlModifier) != 0 && graph() != nullptr) {
        selection_.clear();
        for (const auto& node : graph()->nodes()) {
          selection_.insert(node->id);
        }
        emit selection_changed();
        update();
      }
      return;
    case Qt::Key_Escape:
      selection_.clear();
      wire_.active = false;
      emit selection_changed();
      update();
      return;
    default:
      QWidget::keyPressEvent(event);
  }
}

void NodeCanvas::resizeEvent(QResizeEvent*) {
  if (inline_editor_ != nullptr && !inline_editor_->isVisible()) {
    return;
  }
  cancel_inline_editor();
}

void NodeCanvas::open_palette_centered() {
  wire_.active = false;
  spawn_palette(mapToGlobal(QPoint(width() / 2, height() / 3)));
}

void NodeCanvas::frame_all() {
  const Graph* g = graph();
  if (g == nullptr || g->nodes().empty()) {
    return;
  }
  double min_x = std::numeric_limits<double>::max();
  double min_y = std::numeric_limits<double>::max();
  double max_x = std::numeric_limits<double>::lowest();
  double max_y = std::numeric_limits<double>::lowest();
  for (const auto& node : g->nodes()) {
    const CardLayout layout = layout_for(*node);
    min_x = std::min(min_x, node->x);
    min_y = std::min(min_y, node->y);
    max_x = std::max(max_x, node->x + kNodeWidth);
    max_y = std::max(max_y, node->y + layout.height);
  }
  const double padding = 40.0;
  const double w = std::max(max_x - min_x, 1.0);
  const double h = std::max(max_y - min_y, 1.0);
  zoom_ = std::clamp(std::min((width() - padding * 2) / w, (height() - padding * 2) / h),
                     0.15, 2.0);
  const QPointF center((min_x + max_x) * 0.5, (min_y + max_y) * 0.5);
  offset_ = QPointF(width() * 0.5, height() * 0.5) - center * zoom_;
  update();
}

// 新节点不叠在已有卡片上：工具栏「添加节点」每次都在画布同一个点落，
// 叠上去以后下面那张既看不见也点不到（命中只认最上层那张）。
QPointF NodeCanvas::free_node_spot(const NodeTypeInfo* type, const QPointF& desired) const {
  QPointF spot = desired;
  const Graph* g = graph();
  if (g == nullptr || type == nullptr) {
    return spot;
  }
  Graph::Node probe;
  probe.type = type;
  const double height = layout_for(probe).height;
  const auto collides = [&](const QPointF& point) {
    const QRectF candidate(point.x(), point.y(), kNodeWidth, height);
    for (const auto& node : g->nodes()) {
      const CardLayout card_layout = layout_for(*node);
      const QRectF card(node->x, node->y, kNodeWidth, card_layout.height);
      if (card.adjusted(-6.0, -6.0, 6.0, 6.0).intersects(candidate)) {
        return true;
      }
    }
    return false;
  };
  // 优先在右边空位排开（节点图是从左往右读的），右边排满了再往下换一行。
  for (int step = 0; step < 32 && collides(spot); ++step) {
    spot.setX(spot.x() + kNodeWidth + 48.0);
  }
  for (int step = 0; step < 32 && collides(spot); ++step) {
    spot += QPointF(0.0, 140.0);
  }
  return spot;
}

void NodeCanvas::spawn_palette(const QPoint& global_pos) {  if (session_ == nullptr) {
    // 面板没拿到视口（比如从首页打开文档的那条路径漏了 set_viewport）：
    // 这里以前是静默 return，用户看到的就是「点 Add node 毫无反应」。
    emit status_message(tr("No document"));
    return;
  }
  palette_drop_world_ = device_to_world(mapFromGlobal(global_pos));
  const Graph* g = graph();
  const PortType filter = (wire_.active && g != nullptr)
                              ? port_type_of(*g, wire_.node, wire_.port, wire_.from_output)
                              : PortType::Empty;
  // 从输出端口拖出 → 找「有匹配输入端口」的节点（as_input = true）；
  // 从输入端口拖出 → 找「有匹配输出端口」的节点。
  palette_->spawn(global_pos, filter, wire_.active && wire_.from_output);
}

void NodeCanvas::finish_palette_add(const NodeTypeInfo* type) {
  Document* doc = document();
  if (doc == nullptr || type == nullptr) {
    return;
  }
  const QPointF spot = free_node_spot(type, palette_drop_world_);
  auto add = std::make_unique<NodeAddCommand>(*doc, type->id, spot.x(), spot.y());
  if (!add->execute()) {
    wire_.active = false;
    return;
  }
  const NodeId new_id = add->node_id();
  session_->command_system().push_executed(std::move(add));

  // 从端口拖出的：自动连最佳端口。
  if (wire_.active && graph() != nullptr) {
    Graph* g = graph();
    const Graph::Node* created = g->node(new_id);
    const PortType wire_type = port_type_of(*g, wire_.node, wire_.port, wire_.from_output);
    if (created != nullptr && created->type != nullptr && wire_type != PortType::Empty) {
      int best = -1;
      PortMatch best_match = PortMatch::None;
      if (wire_.from_output) {
        for (std::size_t i = 0; i < created->type->inputs.size(); ++i) {
          const PortMatch m = port_match(wire_type, created->type->inputs[i].type);
          if (m > best_match) {
            best_match = m;
            best = static_cast<int>(i);
          }
        }
        if (best >= 0) {
          run_command<NodeConnectCommand>(wire_.node, static_cast<std::uint16_t>(wire_.port),
                                          new_id, static_cast<std::uint16_t>(best));
        }
      } else {
        for (std::size_t i = 0; i < created->type->outputs.size(); ++i) {
          const PortMatch m = port_match(created->type->outputs[i].type, wire_type);
          if (m > best_match) {
            best_match = m;
            best = static_cast<int>(i);
          }
        }
        if (best >= 0) {
          run_command<NodeConnectCommand>(new_id, static_cast<std::uint16_t>(best), wire_.node,
                                          static_cast<std::uint16_t>(wire_.port));
        }
      }
    }
  }
  wire_.active = false;
  selection_.clear();
  selection_.insert(new_id);
  emit selection_changed();
  emit graph_edited();
  update();
}

void NodeCanvas::open_choice_menu(const Graph::Node& node, int param_index,
                                  const QPoint& global_pos) {
  const NodeParamSpec& spec = node.type->params[static_cast<std::size_t>(param_index)];
  const std::string current = node.params[static_cast<std::size_t>(param_index)].as_text();
  QMenu menu(this);
  for (const std::string& choice : spec.choices) {
    QAction* action = menu.addAction(QString::fromStdString(choice));
    action->setCheckable(true);
    action->setChecked(choice == current);
    connect(action, &QAction::triggered, this, [this, id = node.id,
                                                index = static_cast<std::uint16_t>(param_index),
                                                choice]() {
      run_command<NodeSetParamCommand>(id, index, NodeValue::text(choice));
    });
  }
  menu.exec(global_pos);
}

void NodeCanvas::open_inline_editor(const Hit& hit) {
  const Graph* g = graph();
  const Graph::Node* node = g == nullptr ? nullptr : g->node(hit.node);
  if (node == nullptr || node->type == nullptr) {
    return;
  }
  const CardLayout layout = layout_for(*node);
  const NodeValue* value = nullptr;
  QRectF world_rect;
  if (hit.kind == Hit::Kind::Param) {
    if (static_cast<std::size_t>(hit.index) >= node->params.size() ||
        static_cast<std::size_t>(hit.index) >= layout.param_rects.size()) {
      return;
    }
    value = &node->params[static_cast<std::size_t>(hit.index)];
    world_rect = layout.param_rects[static_cast<std::size_t>(hit.index)];
  } else {
    if (static_cast<std::size_t>(hit.index) >= node->input_values.size() ||
        static_cast<std::size_t>(hit.index) >= layout.input_rects.size()) {
      return;
    }
    value = &node->input_values[static_cast<std::size_t>(hit.index)];
    world_rect = layout.input_rects[static_cast<std::size_t>(hit.index)];
  }
  inline_editor_target_ = hit;
  const QRectF device_rect(world_to_device(world_rect.topLeft()),
                           world_rect.size() * zoom_);
  inline_editor_->setGeometry(device_rect.toRect());
  inline_editor_->setText(format_value(*value));
  inline_editor_->selectAll();
  inline_editor_->show();
  inline_editor_->setFocus();
}

void NodeCanvas::commit_inline_editor() {
  if (!inline_editor_->isVisible() || graph() == nullptr) {
    cancel_inline_editor();
    return;
  }
  const Graph::Node* node = graph()->node(inline_editor_target_.node);
  if (node == nullptr || node->type == nullptr) {
    cancel_inline_editor();
    return;
  }
  const QString text = inline_editor_->text().trimmed();
  const PortType expect = inline_editor_target_.kind == Hit::Kind::Param
                              ? node->type->params[static_cast<std::size_t>(
                                    inline_editor_target_.index)]
                                    .default_value.type()
                              : node->type->inputs[static_cast<std::size_t>(
                                      inline_editor_target_.index)]
                                    .type;
  NodeValue value;
  bool ok = false;
  if (expect == PortType::Number || expect == PortType::Integer) {
    const double parsed = text.toDouble(&ok);
    if (ok) {
      value = expect == PortType::Integer ? NodeValue::integer(static_cast<std::int64_t>(parsed))
                                          : NodeValue::number(parsed);
    }
  } else if (expect == PortType::Vector) {
    const QStringList parts = text.split(',');
    if (parts.size() == 3) {
      float xyz[3] = {0, 0, 0};
      bool all = true;
      for (int i = 0; i < 3; ++i) {
        xyz[i] = static_cast<float>(parts[i].trimmed().toDouble(&all));
        if (!all) {
          break;
        }
      }
      if (all) {
        value = NodeValue::vector(Vec3{xyz[0], xyz[1], xyz[2]});
        ok = true;
      }
    }
  } else {
    value = NodeValue::text(text.toStdString());
    ok = true;
  }
  if (ok) {
    if (inline_editor_target_.kind == Hit::Kind::Param) {
      run_command<NodeSetParamCommand>(inline_editor_target_.node,
                                       static_cast<std::uint16_t>(inline_editor_target_.index),
                                       value);
    } else {
      run_command<NodeSetInputCommand>(inline_editor_target_.node,
                                       static_cast<std::uint16_t>(inline_editor_target_.index),
                                       value);
    }
  } else {
    emit status_message(tr("Invalid value"));
  }
  cancel_inline_editor();
}

void NodeCanvas::cancel_inline_editor() {
  inline_editor_->hide();
  inline_editor_target_ = Hit{};
}

void NodeCanvas::toggle_preview_flag(NodeId node) {
  Graph* g = graph();
  if (g == nullptr) {
    return;
  }
  if (Graph::Node* n = g->node(node)) {
    n->preview = !n->preview;
    emit graph_touched();
    update();
  }
}

void NodeCanvas::delete_selected() {
  if (selection_.empty()) {
    return;
  }
  std::vector<std::uint64_t> ids(selection_.begin(), selection_.end());
  if (run_command<NodeDeleteCommand>(std::move(ids))) {
    selection_.clear();
    emit selection_changed();
  }
}

void NodeCanvas::apply_hover_cursor(const QPointF& device_pos) {
  const Hit hit = hit_test(device_pos);
  hover_ = hit;
  switch (hit.kind) {
    case Hit::Kind::InPort:
    case Hit::Kind::OutPort:
    case Hit::Kind::Eye:
      setCursor(Qt::PointingHandCursor);
      break;
    case Hit::Kind::InField:
    case Hit::Kind::Param:
      setCursor(Qt::SizeHorCursor);
      break;
    default:
      setCursor(Qt::ArrowCursor);
      break;
  }
}

}  // namespace tamias
