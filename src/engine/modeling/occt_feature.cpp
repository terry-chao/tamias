#include "occt_feature.h"

#include "engine/modeling/curve_geom.h"
#include "engine/modeling/edge_fingerprint.h"
#include "engine/profile/timing_scope.h"

#include <Bnd_Box.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Tool.hxx>
#include <GeomLProp_SLProps.hxx>
#include <Geom_Surface.hxx>
#include <Poly_Triangulation.hxx>
#include <ShapeAnalysis_Surface.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#include <Standard_Failure.hxx>
#include <Standard_Type.hxx>

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace tamias {
namespace {

// 矩形轮廓面（在 XY 平面，中心在原点，宽 x 高）。
TopoDS_Face make_rect_face(double width, double height) {
  const double hw = width * 0.5;
  const double hh = height * 0.5;
  const gp_Pnt p1(-hw, -hh, 0.0);
  const gp_Pnt p2(hw, -hh, 0.0);
  const gp_Pnt p3(hw, hh, 0.0);
  const gp_Pnt p4(-hw, hh, 0.0);
  BRepBuilderAPI_MakeWire wire(BRepBuilderAPI_MakeEdge(p1, p2), BRepBuilderAPI_MakeEdge(p2, p3),
                               BRepBuilderAPI_MakeEdge(p3, p4), BRepBuilderAPI_MakeEdge(p4, p1));
  return BRepBuilderAPI_MakeFace(wire).Face();
}

// 多边形轮廓面。点是 Tamias 局部 XZ（Y-up），求值器稍后把 OCCT Z-up 转到 Y-up：
// Tamias (x, 0, z) ↔ OCCT (x, -z, 0)。
TopoDS_Face make_polygon_face(const std::vector<Vec3>& points) {
  BRepBuilderAPI_MakePolygon poly;
  for (const Vec3& p : points) {
    poly.Add(gp_Pnt(static_cast<double>(p.x), static_cast<double>(-p.z), 0.0));
  }
  poly.Close();
  if (!poly.IsDone()) {
    return {};
  }
  BRepBuilderAPI_MakeFace face(poly.Wire());
  if (!face.IsDone()) {
    return {};
  }
  return face.Face();
}

// 圆形轮廓面（在 XY 平面，中心在原点，半径 radius）。
TopoDS_Face make_circle_face(double radius) {
  const gp_Pnt center(0.0, 0.0, 0.0);
  const gp_Circ circle(gp_Ax2(center, gp_Dir(0.0, 0.0, 1.0)), radius);
  BRepBuilderAPI_MakeWire wire{BRepBuilderAPI_MakeEdge(circle)};
  return BRepBuilderAPI_MakeFace(wire).Face();
}

// Tamias Y-up 局部 (x,y,z) ↔ OCCT Z-up (x,-z,y)；与最终 tessellate 的转换互逆。
gp_Pnt tamias_point_to_occt(Vec3 p) {
  return gp_Pnt(static_cast<double>(p.x), -static_cast<double>(p.z),
                static_cast<double>(p.y));
}

gp_Dir tamias_dir_to_occt(Vec3 d) {
  return gp_Dir(static_cast<double>(d.x), -static_cast<double>(d.z),
                static_cast<double>(d.y));
}

// ---------------------------------------------------------------------------
// 边的几何指纹：把「第 N 条边」升级成「长什么样的那条边」。
// 采集与匹配都在求值器内部的 OCCT 空间做；位置按包围盒归一化，所以与尺寸无关。
// ---------------------------------------------------------------------------

// 指纹加权代价阈值。越大越像；超过 kEdgeMatchCost 就认定「不是同一条边」。
constexpr double kEdgeMatchCost = 0.6;     // 接受一条候选边的代价上限
constexpr double kEdgeMatchMargin = 0.10;  // 最优与次优太接近 → 不敢认，报错
constexpr double kEdgeMatchSlack = 0.05;   // 索引候选和最优差不多时，优先信索引

struct ShapeFrame {
  gp_Pnt min;
  double dx = 1.0;
  double dy = 1.0;
  double dz = 1.0;
  double diag = 1.0;
};

struct EdgeSignature {
  bool valid = false;
  Vec3 mid{};   // 归一化中点/质心
  Vec3 dir{};
  bool has_dir = false;
  double length = 0.0;  // 边长 / 包围盒对角线
  Vec3 normal1{};
  Vec3 normal2{};
  bool has_normal1 = false;
  bool has_normal2 = false;
};

ShapeFrame shape_frame(const TopoDS_Shape& shape) {
  ShapeFrame frame{gp_Pnt(0.0, 0.0, 0.0), 1.0, 1.0, 1.0, 1.0};
  Bnd_Box box;
  BRepBndLib::Add(shape, box);
  if (box.IsVoid()) {
    return frame;
  }
  double xmin = 0.0;
  double ymin = 0.0;
  double zmin = 0.0;
  double xmax = 0.0;
  double ymax = 0.0;
  double zmax = 0.0;
  box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
  frame.min = gp_Pnt(xmin, ymin, zmin);
  frame.dx = std::max(xmax - xmin, 1e-9);
  frame.dy = std::max(ymax - ymin, 1e-9);
  frame.dz = std::max(zmax - zmin, 1e-9);
  frame.diag = std::sqrt(frame.dx * frame.dx + frame.dy * frame.dy + frame.dz * frame.dz);
  return frame;
}

std::vector<TopoDS_Edge> collect_edges(const TopoDS_Shape& shape) {
  std::vector<TopoDS_Edge> edges;
  for (TopExp_Explorer exp(shape, TopAbs_EDGE); exp.More(); exp.Next()) {
    edges.push_back(TopoDS::Edge(exp.Current()));
  }
  return edges;
}

Vec3 occt_dir_to_vec(const gp_Dir& d) {
  return Vec3{static_cast<float>(d.X()), static_cast<float>(d.Y()), static_cast<float>(d.Z())};
}

// 相邻面在 p 处的法线（按面朝向修正，指向外面）。平面、圆柱面都能取；取不到返回 false。
bool face_normal_at(const TopoDS_Face& face, const gp_Pnt& p, gp_Dir& out) {
  const Handle(Geom_Surface) surface = BRep_Tool::Surface(face);
  if (surface.IsNull()) {
    return false;
  }
  const gp_Pnt2d uv = ShapeAnalysis_Surface(surface).ValueOfUV(p, 1e-6);
  GeomLProp_SLProps props(surface, uv.X(), uv.Y(), 1, 1e-9);
  if (!props.IsNormalDefined()) {
    return false;
  }
  gp_Dir normal = props.Normal();
  if (face.Orientation() == TopAbs_REVERSED) {
    normal.Reverse();
  }
  out = normal;
  return true;
}

// 一条边的几何签名。闭合边（圆）没有稳定方向，改用整圈采样质心 + 总长。
// with_normals = false 时跳过相邻面法线（粗筛用，省掉昂贵的曲面投影）。
EdgeSignature edge_signature(const TopoDS_Edge& edge, const ShapeFrame& frame,
                             const TopTools_IndexedDataMapOfShapeListOfShape* ancestors,
                             bool with_normals) {
  EdgeSignature sig;
  if (edge.IsNull()) {
    return sig;
  }
  BRepAdaptor_Curve curve(edge);
  const double u0 = curve.FirstParameter();
  const double u1 = curve.LastParameter();
  const bool closed = curve.IsClosed();

  constexpr int kSamples = 24;
  const int count = closed ? kSamples : kSamples + 1;
  std::vector<gp_Pnt> points;
  points.reserve(static_cast<std::size_t>(count));
  gp_XYZ sum(0.0, 0.0, 0.0);
  for (int i = 0; i < count; ++i) {
    const double t = static_cast<double>(i) / static_cast<double>(kSamples);
    const gp_Pnt p = curve.Value(u0 + (u1 - u0) * t);
    points.push_back(p);
    sum += p.XYZ();
  }
  const gp_Pnt centroid(sum / static_cast<double>(count));

  double length = 0.0;
  const std::size_t segments = closed ? points.size() : points.size() - 1;
  for (std::size_t i = 0; i < segments; ++i) {
    length += points[i].Distance(points[(i + 1) % points.size()]);
  }

  sig.valid = true;
  sig.mid = Vec3{static_cast<float>((centroid.X() - frame.min.X()) / frame.dx),
                 static_cast<float>((centroid.Y() - frame.min.Y()) / frame.dy),
                 static_cast<float>((centroid.Z() - frame.min.Z()) / frame.dz)};
  sig.length = length / frame.diag;

  if (!closed) {
    gp_Pnt p;
    gp_Vec d1;
    curve.D1(0.5 * (u0 + u1), p, d1);
    if (d1.Magnitude() > 1e-12) {
      sig.dir = occt_dir_to_vec(gp_Dir(d1));
      sig.has_dir = true;
    }
  }

  if (with_normals && ancestors != nullptr && ancestors->Contains(edge)) {
    // 法线在「边上的点」取（中点参数处必在曲线上）；位置签名才用采样质心，
    // 因为闭合边（圆）的质心是圆心，不在曲线上。
    const gp_Pnt probe = curve.Value(0.5 * (u0 + u1));
    int found = 0;
    for (const TopoDS_Shape& face_shape : ancestors->FindFromKey(edge)) {
      gp_Dir normal;
      if (!face_normal_at(TopoDS::Face(face_shape), probe, normal)) {
        continue;
      }
      if (found == 0) {
        sig.normal1 = occt_dir_to_vec(normal);
        sig.has_normal1 = true;
      } else {
        sig.normal2 = occt_dir_to_vec(normal);
        sig.has_normal2 = true;
      }
      if (++found == 2) {
        break;
      }
    }
  }
  return sig;
}

double normal_cost(bool has_a, Vec3 a, bool has_b, Vec3 b) {
  if (!has_a) {
    return 0.0;  // 采集时就没取到法线，不拿它当依据
  }
  if (!has_b) {
    return 0.1;  // 候选边取不到法线：轻微扣分，别让它白捡
  }
  const double d = std::min(static_cast<double>(std::fabs(dot(a, b))), 1.0);
  return 1.0 - d;
}

// 两个签名的加权代价：0 = 一模一样。位置和方向权重最高。
double edge_signature_cost(const EdgeSignature& a, const EdgeSignature& b) {
  if (!a.valid || !b.valid) {
    return std::numeric_limits<double>::infinity();
  }
  const double dx = static_cast<double>(a.mid.x - b.mid.x);
  const double dy = static_cast<double>(a.mid.y - b.mid.y);
  const double dz = static_cast<double>(a.mid.z - b.mid.z);
  double cost = 2.0 * std::sqrt(dx * dx + dy * dy + dz * dz);
  if (a.has_dir && b.has_dir) {
    const double d = std::min(static_cast<double>(std::fabs(dot(a.dir, b.dir))), 1.0);
    cost += 2.0 * (1.0 - d);
  } else {
    cost += 0.25;
  }
  cost += 0.5 * std::fabs(a.length - b.length);
  cost += normal_cost(a.has_normal1, a.normal1, b.has_normal1, b.normal1);
  cost += normal_cost(a.has_normal2, a.normal2, b.has_normal2, b.normal2);
  return cost;
}

EdgeSignature signature_from_params(const Feature& f) {
  const auto get = [&f](const char* key, double fallback) {
    const auto it = f.params.find(key);
    return it == f.params.end() ? fallback : it->second;
  };
  EdgeSignature sig;
  sig.valid = true;
  sig.mid = Vec3{static_cast<float>(get("edge_mid_x", 0.0)),
                 static_cast<float>(get("edge_mid_y", 0.0)),
                 static_cast<float>(get("edge_mid_z", 0.0))};
  sig.length = get("edge_len", 0.0);
  if (f.params.find("edge_dir_x") != f.params.end()) {
    sig.dir = Vec3{static_cast<float>(get("edge_dir_x", 0.0)),
                   static_cast<float>(get("edge_dir_y", 0.0)),
                   static_cast<float>(get("edge_dir_z", 0.0))};
    sig.has_dir = true;
  }
  if (f.params.find("edge_n1_x") != f.params.end()) {
    sig.normal1 = Vec3{static_cast<float>(get("edge_n1_x", 0.0)),
                       static_cast<float>(get("edge_n1_y", 0.0)),
                       static_cast<float>(get("edge_n1_z", 0.0))};
    sig.has_normal1 = true;
  }
  if (f.params.find("edge_n2_x") != f.params.end()) {
    sig.normal2 = Vec3{static_cast<float>(get("edge_n2_x", 0.0)),
                       static_cast<float>(get("edge_n2_y", 0.0)),
                       static_cast<float>(get("edge_n2_z", 0.0))};
    sig.has_normal2 = true;
  }
  return sig;
}

EdgeFingerprint fingerprint_from_signature(const EdgeSignature& sig) {
  EdgeFingerprint fp;
  fp.mid = sig.mid;
  fp.dir = sig.dir;
  fp.has_dir = sig.has_dir;
  fp.length = sig.length;
  fp.normal1 = sig.normal1;
  fp.has_normal1 = sig.has_normal1;
  fp.normal2 = sig.normal2;
  fp.has_normal2 = sig.has_normal2;
  return fp;
}

// 解析「倒哪条边」：索引只是首选，指纹才是依据。
//   1. 没有指纹（旧文件/脚本只给索引）→ 保持旧的纯索引行为；
//   2. 索引那条边的指纹对得上 → 就用它（没改上游时的快路）；
//   3. 对不上 → 全量找最像的一条；
//   4. 找不到、或两条一样像 → 报错，绝不静默倒到别的棱上。
Result<TopoDS_Edge> resolve_edge(const TopoDS_Shape& shape, const Feature& f) {
  const std::string what = feature_kind_name(f.kind);
  const std::vector<TopoDS_Edge> edges = collect_edges(shape);
  if (edges.empty()) {
    return Err(what + ": shape has no edges");
  }
  const auto edge_index_it = f.params.find("edge");
  const int index =
      static_cast<int>(edge_index_it != f.params.end() ? edge_index_it->second : 0.0);

  if (!has_edge_fingerprint(f.params)) {
    if (index < 0 || index >= static_cast<int>(edges.size())) {
      return Err(what + ": edge index out of range");
    }
    return edges[static_cast<std::size_t>(index)];
  }

  const EdgeSignature stored = signature_from_params(f);
  const ShapeFrame frame = shape_frame(shape);
  const bool want_normals = stored.has_normal1 || stored.has_normal2;
  TopTools_IndexedDataMapOfShapeListOfShape ancestors;
  if (want_normals) {
    TopExp::MapShapesAndAncestors(shape, TopAbs_EDGE, TopAbs_FACE, ancestors);
  }
  const auto* ancestors_ptr = want_normals ? &ancestors : nullptr;

  // 两遍扫描：先只算位置/方向/长度做粗筛（省掉昂贵的相邻面投影），再对可能胜出的
  // 少数候选补法线。法线只会让代价变大，所以「粗筛代价 > 阈值」的边不可能赢，剪掉是安全的。
  EdgeSignature stored_cheap = stored;
  stored_cheap.has_normal1 = false;
  stored_cheap.has_normal2 = false;
  std::vector<double> cheap_cost(edges.size(), std::numeric_limits<double>::infinity());
  for (int i = 0; i < static_cast<int>(edges.size()); ++i) {
    const EdgeSignature cheap =
        edge_signature(edges[static_cast<std::size_t>(i)], frame, nullptr, false);
    cheap_cost[static_cast<std::size_t>(i)] = edge_signature_cost(stored_cheap, cheap);
  }

  std::vector<EdgeSignature> full_signatures(edges.size());
  std::vector<bool> have_full(edges.size(), false);
  const auto cost_at = [&](int i) {
    const auto slot = static_cast<std::size_t>(i);
    if (!want_normals || cheap_cost[slot] > kEdgeMatchCost) {
      return cheap_cost[slot];  // 粗筛已出局：它的代价是下界，够用了
    }
    if (!have_full[slot]) {
      full_signatures[slot] = edge_signature(edges[slot], frame, ancestors_ptr, true);
      have_full[slot] = true;
    }
    return edge_signature_cost(stored, full_signatures[slot]);
  };

  const bool index_in_range = index >= 0 && index < static_cast<int>(edges.size());
  const double index_cost =
      index_in_range ? cost_at(index) : std::numeric_limits<double>::infinity();
  if (index_in_range && index_cost <= kEdgeMatchCost) {
    return edges[static_cast<std::size_t>(index)];
  }

  int best = -1;
  double best_cost = std::numeric_limits<double>::infinity();
  for (int i = 0; i < static_cast<int>(edges.size()); ++i) {
    const double cost = (index_in_range && i == index) ? index_cost : cost_at(i);
    if (cost < best_cost) {
      best_cost = cost;
      best = i;
    }
  }
  if (best < 0 || best_cost > kEdgeMatchCost) {
    return Err(what + ": edge " + std::to_string(index) +
               " cannot be located after the model changed (geometry no longer matches)");
  }

  // 唯一性检查：和最优一样像的另一条边（同一条边的重复出现不算）会让结果不可信。
  const TopoDS_Edge& best_edge = edges[static_cast<std::size_t>(best)];
  double second_cost = std::numeric_limits<double>::infinity();
  for (int i = 0; i < static_cast<int>(edges.size()); ++i) {
    const TopoDS_Edge& candidate = edges[static_cast<std::size_t>(i)];
    if (i == best || candidate.IsSame(best_edge)) {
      continue;
    }
    const double cost = (index_in_range && i == index) ? index_cost : cost_at(i);
    second_cost = std::min(second_cost, cost);
  }
  if (index_in_range && index_cost <= best_cost + kEdgeMatchSlack) {
    return edges[static_cast<std::size_t>(index)];
  }
  if (second_cost - best_cost < kEdgeMatchMargin) {
    return Err(what + ": edge " + std::to_string(index) +
               " is ambiguous after the model changed (two edges match equally well)");
  }
  return edges[static_cast<std::size_t>(best)];
}

const char* feature_scope_name(const FeatureModel& model, const Feature& f) {
  if (f.kind == FeatureKind::Boolean) {
    const int op = static_cast<int>(model.param(f.id, "operation", 0.0));
    switch (op) {
      case 1:
        return "Boolean Common";
      case 2:
        return "Boolean Cut";
      case 0:
      default:
        return "Boolean Fuse";
    }
  }
  return feature_kind_name(f.kind);
}

// 简化的 BRep → 三角网（无 XCAF 颜色逻辑；与 occt_shape_ops 的 tessellate 职责不同）。
Result<MeshCpu> tessellate_shape(const TopoDS_Shape& shape, double deflection) {
  TAMIAS_TIMING_SCOPE("tessellate_shape", TimingCategory::Modeling);
  {
    TAMIAS_TIMING_SCOPE("BRepMesh", TimingCategory::Modeling);
    BRepMesh_IncrementalMesh mesher(shape, deflection, Standard_False, 0.5, Standard_True);
    mesher.Perform();
    if (!mesher.IsDone()) {
      return Err("tessellate: BRepMesh failed");
    }
  }

  TAMIAS_TIMING_SCOPE("extract_triangles", TimingCategory::Modeling);
  MeshCpu mesh;
  for (TopExp_Explorer exp(shape, TopAbs_FACE); exp.More(); exp.Next()) {
    const TopoDS_Face face = TopoDS::Face(exp.Current());
    TopLoc_Location loc;
    const Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
    if (tri.IsNull()) {
      continue;
    }
    const gp_Trsf trsf = loc.Transformation();
    const bool reversed = face.Orientation() == TopAbs_REVERSED;
    const int base = static_cast<int>(mesh.vertices.size());

    const int n_nodes = tri->NbNodes();
    mesh.vertices.reserve(mesh.vertices.size() + static_cast<std::size_t>(n_nodes));
    for (int i = 1; i <= n_nodes; ++i) {
      gp_Pnt p = tri->Node(i);
      p.Transform(trsf);
      Vertex v{};
      v.position = {static_cast<float>(p.X()), static_cast<float>(p.Y()),
                    static_cast<float>(p.Z())};
      v.color = {1.0f, 1.0f, 1.0f};  // 白：让材质 base_color 透出（材质走 push constant）
      if (tri->HasNormals()) {
        gp_Dir n = tri->Normal(i);
        if (reversed) {
          n.Reverse();
        }
        n.Transform(trsf);
        v.normal = {static_cast<float>(n.X()), static_cast<float>(n.Y()),
                    static_cast<float>(n.Z())};
      }
      mesh.vertices.push_back(v);
    }

    const int n_tris = tri->NbTriangles();
    mesh.indices.reserve(mesh.indices.size() + static_cast<std::size_t>(n_tris) * 3);
    const std::uint32_t face_first = static_cast<std::uint32_t>(mesh.indices.size());
    for (int i = 1; i <= n_tris; ++i) {
      int n1 = 0;
      int n2 = 0;
      int n3 = 0;
      tri->Triangle(i).Get(n1, n2, n3);
      if (reversed) {
        std::swap(n2, n3);
      }
      const std::uint32_t i0 = static_cast<std::uint32_t>(base + n1 - 1);
      const std::uint32_t i1 = static_cast<std::uint32_t>(base + n2 - 1);
      const std::uint32_t i2 = static_cast<std::uint32_t>(base + n3 - 1);
      mesh.indices.push_back(i0);
      mesh.indices.push_back(i1);
      mesh.indices.push_back(i2);

      // OCCT 的 Poly_Triangulation 不保证 HasNormals()。若法线缺失，按三角形
      // 绕序回退计算；否则 Vertex.normal 保持零向量，mesh.frag 的背面剔除与
      // 光照会把实体渲成黑色。
      if (!tri->HasNormals()) {
        const Vec3 a = mesh.vertices[i0].position;
        const Vec3 b = mesh.vertices[i1].position;
        const Vec3 c = mesh.vertices[i2].position;
        const Vec3 n = normalize(cross(b - a, c - a));
        mesh.vertices[i0].normal = n;
        mesh.vertices[i1].normal = n;
        mesh.vertices[i2].normal = n;
      }
    }
    MeshFaceRange range{};
    range.first_index = face_first;
    range.index_count = static_cast<std::uint32_t>(mesh.indices.size()) - face_first;
    if (range.index_count != 0) {
      mesh.faces.push_back(range);
    }
  }

  if (mesh.indices.empty()) {
    return Err("tessellate: no triangles produced");
  }
  // OCCT 是 Z-up；Tamias 视口是 Y-up（glTF）。绕 X 转 -90°：(x,y,z)->(x,z,-y)。
  for (auto& v : mesh.vertices) {
    const Vec3 p = v.position;
    v.position = {p.x, p.z, -p.y};
    const Vec3 n = v.normal;
    v.normal = {n.x, n.z, -n.y};
  }
  recompute_bounds(mesh);
  recompute_face_bounds(mesh);
  return mesh;
}

}  // namespace

// 按拓扑序把特征算成 BRep 形状。stop_id != 0 时算到那个特征就停（采集指纹用，
// 不必把下游也跑一遍）。不捕获异常，由外层统一转成 Result 错误。
static Result<std::unordered_map<std::uint64_t, TopoDS_Shape>> build_shapes(
    const FeatureModel& model, std::uint64_t stop_id) {
  std::unordered_map<std::uint64_t, TopoDS_Shape> shapes;
  for (const auto& f : model.features()) {
    TAMIAS_TIMING_SCOPE(feature_scope_name(model, f), TimingCategory::Modeling);
    TopoDS_Shape s;
    switch (f.kind) {
      case FeatureKind::RectProfile: {
        const double w = model.param(f.id, "width", 1.0);
        const double h = model.param(f.id, "height", 1.0);
        s = make_rect_face(w, h);
        break;
      }
      case FeatureKind::PolygonProfile: {
        const std::vector<Vec3> pts = polyline_points(model, f);
        if (pts.size() < 3) {
          return Err("PolygonProfile needs at least 3 points");
        }
        s = make_polygon_face(pts);
        if (s.IsNull()) {
          return Err("PolygonProfile: failed to make face");
        }
        break;
      }
      case FeatureKind::CircleProfile: {
        const double r = model.param(f.id, "radius", 0.5);
        s = make_circle_face(r);
        break;
      }
      case FeatureKind::Extrude: {
        if (f.inputs.empty()) {
          return Err("Extrude feature has no profile input");
        }
        const auto it = shapes.find(f.inputs[0]);
        if (it == shapes.end()) {
          return Err("Extrude references a missing profile feature");
        }
        const double depth = model.param(f.id, "depth", 1.0);
        s = BRepPrimAPI_MakePrism(it->second, gp_Vec(0.0, 0.0, depth)).Shape();
        break;
      }
      case FeatureKind::Boolean: {
        if (f.inputs.size() < 2) {
          return Err("Boolean feature needs two shape inputs");
        }
        const auto a = shapes.find(f.inputs[0]);
        const auto b = shapes.find(f.inputs[1]);
        if (a == shapes.end() || b == shapes.end()) {
          return Err("Boolean references a missing shape");
        }
        const int op = static_cast<int>(model.param(f.id, "operation", 0.0));
        switch (op) {
          case 1:
            s = BRepAlgoAPI_Common(a->second, b->second).Shape();
            break;
          case 2:
            s = BRepAlgoAPI_Cut(a->second, b->second).Shape();
            break;
          case 0:
          default:
            s = BRepAlgoAPI_Fuse(a->second, b->second).Shape();
            break;
        }
        break;
      }
      case FeatureKind::Fillet: {
        if (f.inputs.empty()) {
          return Err("Fillet feature has no shape input");
        }
        const auto it = shapes.find(f.inputs[0]);
        if (it == shapes.end()) {
          return Err("Fillet references a missing shape");
        }
        const double radius = model.param(f.id, "radius", 0.1);
        auto edge = resolve_edge(it->second, f);
        if (!edge) {
          return Err(edge.error());
        }
        BRepFilletAPI_MakeFillet fillet(it->second);
        fillet.Add(radius, *edge);
        fillet.Build();
        if (!fillet.IsDone()) {
          return Err("Fillet failed");
        }
        s = fillet.Shape();
        break;
      }
      case FeatureKind::Chamfer: {
        if (f.inputs.empty()) {
          return Err("Chamfer feature has no shape input");
        }
        const auto it = shapes.find(f.inputs[0]);
        if (it == shapes.end()) {
          return Err("Chamfer references a missing shape");
        }
        const double distance = model.param(f.id, "distance", 0.1);
        auto edge = resolve_edge(it->second, f);
        if (!edge) {
          return Err(edge.error());
        }
        BRepFilletAPI_MakeChamfer chamfer(it->second);
        chamfer.Add(distance, *edge);
        chamfer.Build();
        if (!chamfer.IsDone()) {
          return Err("Chamfer failed");
        }
        s = chamfer.Shape();
        break;
      }
      case FeatureKind::Transform: {
        if (f.inputs.empty()) {
          return Err("Transform feature has no shape input");
        }
        const auto it = shapes.find(f.inputs[0]);
        if (it == shapes.end()) {
          return Err("Transform references a missing shape");
        }
        // Tamias Y-up (tx,ty,tz) → OCCT Z-up (tx, -tz, ty)，与 tessellate 的逆变换一致。
        const double tx = model.param(f.id, "tx", 0.0);
        const double ty = model.param(f.id, "ty", 0.0);
        const double tz = model.param(f.id, "tz", 0.0);
        gp_Trsf tr;
        tr.SetTranslation(gp_Vec(tx, -tz, ty));
        s = BRepBuilderAPI_Transform(it->second, tr, Standard_True).Shape();
        break;
      }
      case FeatureKind::Cylinder: {
        const double radius = std::max(model.param(f.id, "radius", 0.05), 1e-4);
        const double height = std::max(model.param(f.id, "height", 0.1), 1e-4);
        Vec3 center{static_cast<float>(model.param(f.id, "cx", 0.0)),
                    static_cast<float>(model.param(f.id, "cy", 0.0)),
                    static_cast<float>(model.param(f.id, "cz", 0.0))};
        Vec3 axis{static_cast<float>(model.param(f.id, "ax", 0.0)),
                  static_cast<float>(model.param(f.id, "ay", 1.0)),
                  static_cast<float>(model.param(f.id, "az", 0.0))};
        axis = length(axis) > 1e-8f ? normalize(axis) : Vec3{0.f, 1.f, 0.f};
        const Vec3 base = center - axis * (static_cast<float>(height) * 0.5f);
        s = BRepPrimAPI_MakeCylinder(gp_Ax2(tamias_point_to_occt(base),
                                            tamias_dir_to_occt(axis)),
                                     radius, height)
                .Shape();
        break;
      }
      default:
        return Err("unknown feature kind");
    }
    shapes[f.id] = s;
    if (stop_id != 0 && f.id == stop_id) {
      break;
    }
  }
  return shapes;
}

// 求值实现：不捕获异常，由外层 evaluate_feature_model 统一转成 Result 错误。
static Result<MeshCpu> evaluate_feature_model_impl(const FeatureModel& model,
                                                  double linear_deflection) {
  const Feature* out = model.output_feature();
  if (out != nullptr && is_sketch_feature(out->kind)) {
    TAMIAS_TIMING_SCOPE(feature_kind_name(out->kind), TimingCategory::Modeling);
    return mesh_from_sketch_feature(model, *out);
  }
  auto shapes = build_shapes(model, 0);
  if (!shapes) {
    return Err(shapes.error());
  }
  if (out == nullptr) {
    return Err("feature model has no features");
  }
  const auto it = shapes->find(out->id);
  if (it == shapes->end()) {
    return Err("output feature has no shape");
  }
  return tessellate_shape(it->second, linear_deflection);
}

Result<MeshCpu> evaluate_feature_model(const FeatureModel& model, double linear_deflection) {
  TAMIAS_TIMING_SCOPE("evaluate_feature_model", TimingCategory::Modeling);
  try {
    return evaluate_feature_model_impl(model, linear_deflection);
  } catch (const Standard_Failure& e) {
    return Err(std::string("OCCT evaluation failed: ") + e.DynamicType()->Name());
  } catch (const std::exception& e) {
    return Err(std::string("OCCT evaluation failed: ") + e.what());
  }
}

Result<EdgeFingerprint> capture_edge_fingerprint(const FeatureModel& model,
                                                std::uint64_t shape_feature_id,
                                                int edge_index) {
  try {
    std::uint64_t target = shape_feature_id;
    if (target == 0) {
      const Feature* out = model.output_feature();
      if (out == nullptr) {
        return Err("edge fingerprint: feature model is empty");
      }
      target = out->id;
    }
    auto shapes = build_shapes(model, target);
    if (!shapes) {
      return Err(shapes.error());
    }
    const auto it = shapes->find(target);
    if (it == shapes->end()) {
      return Err("edge fingerprint: shape feature not found");
    }
    const std::vector<TopoDS_Edge> edges = collect_edges(it->second);
    if (edge_index < 0 || edge_index >= static_cast<int>(edges.size())) {
      return Err("edge fingerprint: edge index out of range");
    }
    const ShapeFrame frame = shape_frame(it->second);
    TopTools_IndexedDataMapOfShapeListOfShape ancestors;
    TopExp::MapShapesAndAncestors(it->second, TopAbs_EDGE, TopAbs_FACE, ancestors);
    const EdgeSignature sig =
        edge_signature(edges[static_cast<std::size_t>(edge_index)], frame, &ancestors, true);
    if (!sig.valid) {
      return Err("edge fingerprint: edge has no geometry");
    }
    return fingerprint_from_signature(sig);
  } catch (const Standard_Failure& e) {
    return Err(std::string("edge fingerprint failed: ") + e.DynamicType()->Name());
  } catch (const std::exception& e) {
    return Err(std::string("edge fingerprint failed: ") + e.what());
  }
}

}  // namespace tamias
