#include "app/shell/panel/node_ui_text.h"

#include <QCoreApplication>

namespace tamias {
namespace {

// 统一的翻译上下文：.ts 里是 <name>NodeUiText</name>。
QString ui_text(const char* source) {
  return QCoreApplication::translate("NodeUiText", source);
}

// Graph::connect 的报错里端口类型是英文枚举名（Number / Body…）。
QString port_type_label(const QString& english) {
  if (english == QLatin1String("Number")) {
    return ui_text("number");
  }
  if (english == QLatin1String("Integer")) {
    return ui_text("integer");
  }
  if (english == QLatin1String("Bool")) {
    return ui_text("bool");
  }
  if (english == QLatin1String("Text")) {
    return ui_text("text");
  }
  if (english == QLatin1String("Vector")) {
    return ui_text("vector");
  }
  if (english == QLatin1String("Body")) {
    return ui_text("body");
  }
  if (english == QLatin1String("Empty")) {
    return ui_text("empty");
  }
  return english;
}

// 内核动作前缀（"extrude: …"）→ 中文动词。
QString kernel_op_label(const QString& op) {
  if (op == QLatin1String("make_rect_face")) {
    return ui_text("rect face");
  }
  if (op == QLatin1String("make_polygon_face")) {
    return ui_text("polygon face");
  }
  if (op == QLatin1String("extrude")) {
    return ui_text("extrude");
  }
  if (op == QLatin1String("boolean")) {
    return ui_text("boolean");
  }
  if (op == QLatin1String("fillet")) {
    return ui_text("fillet");
  }
  if (op == QLatin1String("chamfer")) {
    return ui_text("chamfer");
  }
  if (op == QLatin1String("edges")) {
    return ui_text("edges");
  }
  if (op == QLatin1String("cylinder")) {
    return ui_text("cylinder");
  }
  if (op == QLatin1String("transform")) {
    return ui_text("transform");
  }
  return op;
}

// "extrude: BRep_API: command not done" → ("extrude", "BRep_API: …")。
bool split_kernel_failure(const QString& text, QString* op, QString* detail) {
  const int sep = text.indexOf(QLatin1String(": "));
  if (sep <= 0) {
    return false;
  }
  const QString head = text.left(sep);
  static const char* const kOps[] = {"make_rect_face", "make_polygon_face", "extrude",
                                     "boolean",         "fillet",           "chamfer",
                                     "edges",           "cylinder",         "transform"};
  for (const char* candidate : kOps) {
    if (head == QLatin1String(candidate)) {
      *op = head;
      *detail = text.mid(sep + 2);
      return true;
    }
  }
  return false;
}

// 命令错误形如 "NodeConnectCommand: cannot connect Body to Number"：
// 去掉命令名前缀，只翻译人话原因。
QString strip_command_prefix(const QString& text) {
  const int sep = text.indexOf(QLatin1String(": "));
  if (sep > 0 && text.left(sep).endsWith(QLatin1String("Command"))) {
    return text.mid(sep + 2);
  }
  return text;
}

}  // namespace

QString node_state_message(const std::string& raw) {
  const QString text = QString::fromStdString(raw);
  if (text.isEmpty()) {
    return text;
  }

  if (text.startsWith(QLatin1String("missing input: "))) {
    return ui_text("Missing input: %1").arg(text.mid(15));
  }
  const QString missing_body = QLatin1String(": missing body input");
  if (text.endsWith(missing_body)) {
    return ui_text("Missing body input: %1").arg(text.left(text.size() - missing_body.size()));
  }
  if (text.startsWith(QLatin1String("unknown node type: "))) {
    return ui_text("Unknown node type: %1").arg(text.mid(19));
  }
  if (text == QLatin1String("no modeling kernel")) {
    return ui_text("No modeling kernel");
  }
  if (text == QLatin1String("box dimensions must be positive")) {
    return ui_text("Box dimensions must be positive");
  }
  if (text == QLatin1String("cylinder dimensions must be positive")) {
    return ui_text("Cylinder dimensions must be positive");
  }
  if (text == QLatin1String("depth must be positive")) {
    return ui_text("Depth must be positive");
  }
  if (text == QLatin1String("radius must be positive")) {
    return ui_text("Radius must be positive");
  }
  if (text == QLatin1String("distance must be positive")) {
    return ui_text("Distance must be positive");
  }
  if (text == QLatin1String("no edges selected")) {
    return ui_text("No edges selected");
  }
  if (text.startsWith(QLatin1String("bad edge index: "))) {
    return ui_text("Bad edge index: %1").arg(text.mid(16));
  }
  if (text.startsWith(QLatin1String("edge index out of range: "))) {
    return ui_text("Edge index out of range: %1").arg(text.mid(25));
  }
  if (text.startsWith(QLatin1String("bad point: "))) {
    return ui_text("Bad point: %1").arg(text.mid(11));
  }
  if (text.startsWith(QLatin1String("point needs x,z: "))) {
    return ui_text("Point needs x,z: %1").arg(text.mid(17));
  }
  if (text == QLatin1String("polygon needs at least 3 points")) {
    return ui_text("Polygon needs at least 3 points");
  }
  QString op;
  QString detail;
  if (split_kernel_failure(text, &op, &detail)) {
    return ui_text("%1 failed: %2").arg(kernel_op_label(op), detail);
  }
  if (text.startsWith(QLatin1String("exception: "))) {
    return ui_text("Node evaluation exception: %1").arg(text.mid(11));
  }
  if (text == QLatin1String("unknown exception in node solve")) {
    return ui_text("Unknown exception in node solve");
  }
  return text;  // 认不出的（新消息 / 内核原文）原样显示，方便报告问题
}

QString node_command_error(const std::string& raw) {
  const QString text = strip_command_prefix(QString::fromStdString(raw));
  if (text.startsWith(QLatin1String("cannot connect "))) {
    const QString rest = text.mid(15);
    const int to = rest.indexOf(QLatin1String(" to "));
    if (to > 0) {
      return ui_text("Cannot connect %1 to %2")
          .arg(port_type_label(rest.left(to)), port_type_label(rest.mid(to + 4)));
    }
  }
  if (text == QLatin1String("input port already connected")) {
    return ui_text("That input already has a connection");
  }
  if (text == QLatin1String("connection would create a cycle")) {
    return ui_text("That would create a cycle");
  }
  if (text == QLatin1String("duplicate link")) {
    return ui_text("That link already exists");
  }
  if (text == QLatin1String("port index out of range")) {
    return ui_text("Port index out of range");
  }
  if (text == QLatin1String("endpoint node missing") ||
      text == QLatin1String("node not found")) {
    return ui_text("The node no longer exists");
  }
  if (text == QLatin1String("nothing to delete")) {
    return ui_text("Nothing to delete");
  }
  if (text == QLatin1String("port has no link")) {
    return ui_text("That input is not connected");
  }
  if (text == QLatin1String("document has no node graph")) {
    return ui_text("This document has no node graph");
  }
  if (text == QLatin1String("no evaluated outputs to bake")) {
    return ui_text("Nothing to bake — enable a node's preview first");
  }
  if (text.startsWith(QLatin1String("tessellate failed: "))) {
    return ui_text("Tessellation failed: %1").arg(text.mid(19));
  }
  if (text == QLatin1String("evaluation cancelled")) {
    return ui_text("Evaluation cancelled");
  }
  if (text == QLatin1String("no document")) {
    return ui_text("No document");
  }
  return text;
}

}  // namespace tamias
