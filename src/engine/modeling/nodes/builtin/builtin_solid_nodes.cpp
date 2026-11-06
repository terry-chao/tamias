#include "engine/modeling/nodes/builtin/builtin_nodes.h"

#include "engine/modeling/feature/feature.h"  // BooleanOp（kernel.h 只前置声明）
#include "engine/modeling/nodes/node_eval.h"

#include <charconv>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

namespace tamias {
namespace {

// 体输入的公共守卫：缺体 / 没内核直接报节点错误。
BodyRef require_body(SolveArgs& a, int port, const char* what) {
  if (a.kernel() == nullptr) {
    a.error("no modeling kernel");
    return nullptr;
  }
  const BodyRef body = a.in_body(port);
  if (body == nullptr) {
    a.error(std::string(what) + ": missing body input");
  }
  return body;
}

// 解析 "0,3,5" 形式的边索引列表；空串 = 全部边。越界 / 格式错返回 false。
[[nodiscard]] bool parse_edges(const std::string& text, std::size_t edge_count,
                               std::vector<EdgeId>& out, std::string& err) {
  out.clear();
  if (text.empty()) {
    out.reserve(edge_count);
    for (std::size_t i = 0; i < edge_count; ++i) {
      out.push_back(static_cast<EdgeId>(i));
    }
    return true;
  }
  std::istringstream stream(text);
  std::string token;
  while (std::getline(stream, token, ',')) {
    int value = 0;
    const auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), value);
    if (ec != std::errc() || ptr != token.data() + token.size()) {
      err = "bad edge index: " + token;
      return false;
    }
    if (value < 0 || static_cast<std::size_t>(value) >= edge_count) {
      err = "edge index out of range: " + token;
      return false;
    }
    out.push_back(static_cast<EdgeId>(value));
  }
  if (out.empty()) {
    err = "no edges selected";
    return false;
  }
  return true;
}

// 解析 "x,z;x,z;…" 的 XZ 平面点列（y=0；与内核矩形/多边形轮廓面的约定一致）。
[[nodiscard]] bool parse_xz_points(const std::string& text, std::vector<Vec3>& out,
                                   std::string& err) {
  out.clear();
  std::istringstream outer(text);
  std::string pair;
  while (std::getline(outer, pair, ';')) {
    std::istringstream coords(pair);
    std::string token;
    std::vector<double> xy;
    while (std::getline(coords, token, ',')) {
      try {
        xy.push_back(std::stod(token));
      } catch (...) {
        err = "bad point: " + pair;
        return false;
      }
    }
    if (xy.size() != 2) {
      err = "point needs x,z: " + pair;
      return false;
    }
    out.push_back(Vec3{static_cast<float>(xy[0]), 0.0f, static_cast<float>(xy[1])});
  }
  if (out.size() < 3) {
    err = "polygon needs at least 3 points";
    return false;
  }
  return true;
}

}  // namespace

void register_solid_nodes(NodeRegistry& registry) {
  // —— 实体/创建 ——

  NodeTypeInfo box;
  box.id = "solid.box";
  box.name = "Box";
  box.category = "实体/创建";
  box.keywords = "盒体 方块 矩形体 长方体 box cuboid";
  box.inputs.push_back(PortSpec{"Width", PortType::Number, NodeValue::number(1.0)});
  box.inputs.push_back(PortSpec{"Height", PortType::Number, NodeValue::number(1.0)});
  box.inputs.push_back(PortSpec{"Depth", PortType::Number, NodeValue::number(1.0)});
  box.outputs.push_back(PortSpec{"Body", PortType::Body});
  box.solve = [](SolveArgs& a) {
    ModelKernel* k = a.kernel();
    if (k == nullptr) {
      a.error("no modeling kernel");
      return;
    }
    const double w = a.in_number(0, 1.0);
    const double h = a.in_number(1, 1.0);
    const double d = a.in_number(2, 1.0);
    if (w <= 0.0 || h <= 0.0 || d <= 0.0) {
      a.error("box dimensions must be positive");
      return;
    }
    // 轮廓面在 XZ 平面（Width × Depth），再沿 +Y 拉出 Height：
    // 卡片上的 Width / Height / Depth 就分别对应 X / Y / Z，底面落在 y = 0。
    auto face = k->make_rect_face(w, d);
    if (!face) {
      a.error("make_rect_face: " + face.error());
      return;
    }
    auto body = k->extrude(**face, h);
    if (!body) {
      a.error("extrude: " + body.error());
      return;
    }
    a.out(0, NodeValue::body(std::move(*body)));
  };
  registry.register_type(std::move(box));

  NodeTypeInfo cylinder;
  cylinder.id = "solid.cylinder";
  cylinder.name = "Cylinder";
  cylinder.category = "实体/创建";
  cylinder.keywords = "圆柱 cylinder";
  cylinder.inputs.push_back(PortSpec{"Radius", PortType::Number, NodeValue::number(0.5)});
  cylinder.inputs.push_back(PortSpec{"Height", PortType::Number, NodeValue::number(1.0)});
  cylinder.outputs.push_back(PortSpec{"Body", PortType::Body});
  cylinder.solve = [](SolveArgs& a) {
    ModelKernel* k = a.kernel();
    if (k == nullptr) {
      a.error("no modeling kernel");
      return;
    }
    const double r = a.in_number(0, 0.5);
    const double h = a.in_number(1, 1.0);
    if (r <= 0.0 || h <= 0.0) {
      a.error("cylinder dimensions must be positive");
      return;
    }
    // 立在原点、沿 +Y（Tamias Y-up 局部）。
    auto body = k->cylinder(r, h, Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f});
    if (!body) {
      a.error("cylinder: " + body.error());
      return;
    }
    a.out(0, NodeValue::body(std::move(*body)));
  };
  registry.register_type(std::move(cylinder));

  NodeTypeInfo polygon;
  polygon.id = "solid.polygon_extrude";
  polygon.name = "Polygon Extrude";
  polygon.category = "实体/创建";
  polygon.keywords = "多边形 轮廓 拉伸 polygon extrude";
  polygon.inputs.push_back(
      PortSpec{"Points", PortType::Text, NodeValue::text("-1,-1;1,-1;1,1;-1,1")});
  polygon.inputs.push_back(PortSpec{"Depth", PortType::Number, NodeValue::number(1.0)});
  polygon.outputs.push_back(PortSpec{"Body", PortType::Body});
  polygon.solve = [](SolveArgs& a) {
    ModelKernel* k = a.kernel();
    if (k == nullptr) {
      a.error("no modeling kernel");
      return;
    }
    std::vector<Vec3> points;
    std::string err;
    if (!parse_xz_points(a.in_text(0), points, err)) {
      a.error(err);
      return;
    }
    const double depth = a.in_number(1, 1.0);
    if (depth <= 0.0) {
      a.error("depth must be positive");
      return;
    }
    auto face = k->make_polygon_face(points);
    if (!face) {
      a.error("make_polygon_face: " + face.error());
      return;
    }
    auto body = k->extrude(**face, depth);
    if (!body) {
      a.error("extrude: " + body.error());
      return;
    }
    a.out(0, NodeValue::body(std::move(*body)));
  };
  registry.register_type(std::move(polygon));

  // —— 实体/布尔 ——

  NodeTypeInfo boolean;
  boolean.id = "solid.boolean";
  boolean.name = "Boolean";
  boolean.category = "实体/布尔";
  boolean.keywords = "布尔 并 差 交 union cut common boolean";
  boolean.inputs.push_back(PortSpec{"A", PortType::Body});
  boolean.inputs.push_back(PortSpec{"B", PortType::Body});
  boolean.outputs.push_back(PortSpec{"Body", PortType::Body});
  boolean.params.push_back(NodeParamSpec{
      "op", NodeValue::text("fuse"), WidgetKind::Choice, 0.0, 0.0, {"fuse", "cut", "common"}});
  boolean.solve = [](SolveArgs& a) {
    ModelKernel* k = a.kernel();
    if (k == nullptr) {
      a.error("no modeling kernel");
      return;
    }
    const BodyRef x = require_body(a, 0, "Boolean A");
    if (x == nullptr) {
      return;
    }
    const BodyRef y = require_body(a, 1, "Boolean B");
    if (y == nullptr) {
      return;
    }
    BooleanOp op = BooleanOp::Fuse;
    const std::string& choice = a.param_text("op");
    if (choice == "cut") {
      op = BooleanOp::Cut;
    } else if (choice == "common") {
      op = BooleanOp::Common;
    }
    auto body = k->boolean(*x, *y, op);
    if (!body) {
      a.error("boolean: " + body.error());
      return;
    }
    a.out(0, NodeValue::body(std::move(*body)));
  };
  registry.register_type(std::move(boolean));

  // —— 实体/修改 ——

  NodeTypeInfo fillet;
  fillet.id = "solid.fillet";
  fillet.name = "Fillet";
  fillet.category = "实体/修改";
  fillet.keywords = "圆角 倒圆角 fillet round";
  fillet.inputs.push_back(PortSpec{"Body", PortType::Body});
  fillet.inputs.push_back(PortSpec{"Radius", PortType::Number, NodeValue::number(0.1)});
  fillet.outputs.push_back(PortSpec{"Body", PortType::Body});
  fillet.params.push_back(
      NodeParamSpec{"edges", NodeValue::text(""), WidgetKind::Field, 0.0, 0.0, {}});
  fillet.solve = [](SolveArgs& a) {
    ModelKernel* k = a.kernel();
    if (k == nullptr) {
      a.error("no modeling kernel");
      return;
    }
    const BodyRef input = require_body(a, 0, "Fillet");
    if (input == nullptr) {
      return;
    }
    const double radius = a.in_number(1, 0.1);
    if (radius <= 0.0) {
      a.error("radius must be positive");
      return;
    }
    auto all = k->edges(*input);
    if (!all) {
      a.error("edges: " + all.error());
      return;
    }
    std::vector<EdgeId> selected;
    std::string err;
    if (!parse_edges(a.param_text("edges"), all->size(), selected, err)) {
      a.error(err);
      return;
    }
    auto body = k->fillet(*input, selected, radius);
    if (!body) {
      a.error("fillet: " + body.error());
      return;
    }
    a.out(0, NodeValue::body(std::move(*body)));
  };
  registry.register_type(std::move(fillet));

  NodeTypeInfo chamfer;
  chamfer.id = "solid.chamfer";
  chamfer.name = "Chamfer";
  chamfer.category = "实体/修改";
  chamfer.keywords = "斜角 倒斜角 chamfer";
  chamfer.inputs.push_back(PortSpec{"Body", PortType::Body});
  chamfer.inputs.push_back(PortSpec{"Distance", PortType::Number, NodeValue::number(0.1)});
  chamfer.outputs.push_back(PortSpec{"Body", PortType::Body});
  chamfer.params.push_back(
      NodeParamSpec{"edges", NodeValue::text(""), WidgetKind::Field, 0.0, 0.0, {}});
  chamfer.solve = [](SolveArgs& a) {
    ModelKernel* k = a.kernel();
    if (k == nullptr) {
      a.error("no modeling kernel");
      return;
    }
    const BodyRef input = require_body(a, 0, "Chamfer");
    if (input == nullptr) {
      return;
    }
    const double distance = a.in_number(1, 0.1);
    if (distance <= 0.0) {
      a.error("distance must be positive");
      return;
    }
    auto all = k->edges(*input);
    if (!all) {
      a.error("edges: " + all.error());
      return;
    }
    std::vector<EdgeId> selected;
    std::string err;
    if (!parse_edges(a.param_text("edges"), all->size(), selected, err)) {
      a.error(err);
      return;
    }
    auto body = k->chamfer(*input, selected, distance);
    if (!body) {
      a.error("chamfer: " + body.error());
      return;
    }
    a.out(0, NodeValue::body(std::move(*body)));
  };
  registry.register_type(std::move(chamfer));

  // —— 输出/预览（展示型：无 solve，求值器把输入透传到输出）——

  NodeTypeInfo preview;
  preview.id = "output.preview";
  preview.name = "Preview Output";
  preview.category = "输出/预览";
  preview.keywords = "预览 输出 output preview bake 烘焙";
  preview.inputs.push_back(PortSpec{"Body", PortType::Body});
  preview.outputs.push_back(PortSpec{"Body", PortType::Body});
  registry.register_type(std::move(preview));
}

}  // namespace tamias
