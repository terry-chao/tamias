#include "engine/modeling/occt/occt_kernel.h"

#include "engine/modeling/feature.h"
#include "engine/profile/timing_scope.h"

#include <Bnd_Box.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
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
#include <Standard_Failure.hxx>
#include <Standard_Type.hxx>
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

#include <algorithm>
#include <cmath>
#include <exception>
#include <functional>
#include <string>
#include <vector>

namespace tamias {
namespace {

// ---- Tamias Y-up ↔ OCCT Z-up -------------------------------------------------
// 视口是 Y-up（glTF），OCCT 是 Z-up：绕 X 转 -90°，(x, y, z) → (x, z, -y)。

gp_Pnt tamias_point_to_occt(Vec3 p) {
  return gp_Pnt(static_cast<double>(p.x), -static_cast<double>(p.z), static_cast<double>(p.y));
}

gp_Dir tamias_dir_to_occt(Vec3 d) {
  return gp_Dir(static_cast<double>(d.x), -static_cast<double>(d.z), static_cast<double>(d.y));
}

Vec3 occt_point_to_tamias(const gp_Pnt& p) {
  return Vec3{static_cast<float>(p.X()), static_cast<float>(p.Z()),
              static_cast<float>(-p.Y())};
}

Vec3 occt_dir_to_tamias(const gp_Dir& d) {
  return Vec3{static_cast<float>(d.X()), static_cast<float>(d.Z()),
              static_cast<float>(-d.Y())};
}

// ---- 轮廓面 -----------------------------------------------------------------

// 轴对齐矩形轮廓面（OCCT XY 平面，中心在原点，宽 × 高）。
TopoDS_Face build_rect_face(double width, double height) {
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

// 多边形轮廓面。点是 Tamias 局部 XZ（Y-up），这里转到 OCCT：Tamias (x, 0, z) ↔ OCCT (x, -z, 0)。
TopoDS_Face build_polygon_face(std::span<const Vec3> points) {
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

// 圆形轮廓面（OCCT XY 平面，中心在原点）。
TopoDS_Face build_circle_face(double radius) {
  const gp_Pnt center(0.0, 0.0, 0.0);
  const gp_Circ circle(gp_Ax2(center, gp_Dir(0.0, 0.0, 1.0)), radius);
  BRepBuilderAPI_MakeWire wire{BRepBuilderAPI_MakeEdge(circle)};
  return BRepBuilderAPI_MakeFace(wire).Face();
}

// ---- 查询 -------------------------------------------------------------------

const TopoDS_Shape* shape_of(const Body& body) {
  const auto* occt_body = dynamic_cast<const OcctBody*>(&body);
  return occt_body == nullptr ? nullptr : &occt_body->shape();
}

std::vector<TopoDS_Edge> collect_edges(const TopoDS_Shape& shape) {
  std::vector<TopoDS_Edge> edges;
  for (TopExp_Explorer exp(shape, TopAbs_EDGE); exp.More(); exp.Next()) {
    edges.push_back(TopoDS::Edge(exp.Current()));
  }
  return edges;
}

// 相邻面在 p 处的法线（按面朝向修正，指向外面）。取不到返回 false。
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

// 一条边的几何测量（Tamias Y-up 空间）。闭合边（圆）没有稳定方向，改用整圈采样质心。
// ancestors 由调用方建一次（全量测量时按 face 建表是 O(N·F)，逐条建会白花时间）。
void measure(const TopoDS_Edge& edge,
             const TopTools_IndexedDataMapOfShapeListOfShape& ancestors, EdgeMeasure& out) {
  // TShape + Location 的哈希（OCCT 7.9 的 std::hash<TopoDS_Shape> 就是这两项，忽略朝向）：
  // 同一条几何边在不同面上的重复出现得到同一个 key。
  out.key = static_cast<std::uint64_t>(std::hash<TopoDS_Shape>{}(edge));
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
  out.mid = occt_point_to_tamias(centroid);

  double length = 0.0;
  const std::size_t segments = closed ? points.size() : points.size() - 1;
  for (std::size_t i = 0; i < segments; ++i) {
    length += points[i].Distance(points[(i + 1) % points.size()]);
  }
  out.length = length;

  if (!closed) {
    gp_Pnt p;
    gp_Vec d1;
    curve.D1(0.5 * (u0 + u1), p, d1);
    if (d1.Magnitude() > 1e-12) {
      out.dir = occt_dir_to_tamias(gp_Dir(d1));
      out.has_dir = true;
    }
  }

  // 法线在「边上的点」取（中点参数处必在曲线上）；位置才用采样质心，
  // 因为闭合边（圆）的质心是圆心，不在曲线上。
  if (!ancestors.Contains(edge)) {
    return;
  }
  const gp_Pnt probe = curve.Value(0.5 * (u0 + u1));
  int found = 0;
  for (const TopoDS_Shape& face_shape : ancestors.FindFromKey(edge)) {
    gp_Dir normal;
    if (!face_normal_at(TopoDS::Face(face_shape), probe, normal)) {
      continue;
    }
    if (found == 0) {
      out.normal1 = occt_dir_to_tamias(normal);
      out.has_normal1 = true;
    } else {
      out.normal2 = occt_dir_to_tamias(normal);
      out.has_normal2 = true;
    }
    if (++found == 2) {
      break;
    }
  }
}

// 简化的 BRep → 三角网（无 XCAF 颜色逻辑；与导入路径的 Shape::tessellate 职责不同）。
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

// 内核动词的公共外壳：取 shape → 干活 → 统一把 OCCT 异常转成 Result 错误。
template <typename Fn>
Result<BodyRef> with_shape(const Body& body, const char* what, Fn&& fn) {
  const TopoDS_Shape* shape = shape_of(body);
  if (shape == nullptr) {
    return Err(std::string(what) + ": body is not an OCCT body");
  }
  try {
    return fn(*shape);
  } catch (const Standard_Failure& e) {
    return Err(std::string(what) + " failed: " + e.DynamicType()->Name());
  } catch (const std::exception& e) {
    return Err(std::string(what) + " failed: " + e.what());
  }
}

}  // namespace

KernelCapabilities OcctKernel::capabilities() const {
  KernelCapabilities caps;
  caps.multi_edge_fillet = true;  // MakeFillet / MakeChamfer 支持一次 Add 多条
  caps.variable_radius_fillet = false;
  caps.step_import = true;
  caps.step_export = false;
  caps.native_brep_io = true;
  return caps;
}

Result<BodyRef> OcctKernel::make_rect_face(double width, double height) const {
  return BodyRef{std::make_shared<OcctBody>(build_rect_face(width, height))};
}

Result<BodyRef> OcctKernel::make_circle_face(double radius) const {
  return BodyRef{std::make_shared<OcctBody>(build_circle_face(radius))};
}

Result<BodyRef> OcctKernel::make_polygon_face(std::span<const Vec3> loop) const {
  if (loop.size() < 3) {
    return Err("make_polygon_face: needs at least 3 points");
  }
  const TopoDS_Face face = build_polygon_face(loop);
  if (face.IsNull()) {
    return Err("make_polygon_face: failed to build a face");
  }
  return BodyRef{std::make_shared<OcctBody>(face)};
}

Result<BodyRef> OcctKernel::extrude(const Body& profile, double depth) const {
  return with_shape(profile, "extrude", [depth](const TopoDS_Shape& shape) -> Result<BodyRef> {
    const TopoDS_Shape solid = BRepPrimAPI_MakePrism(shape, gp_Vec(0.0, 0.0, depth)).Shape();
    if (solid.IsNull()) {
      return Err("extrude: prism failed");
    }
    return BodyRef{std::make_shared<OcctBody>(solid)};
  });
}

Result<BodyRef> OcctKernel::boolean(const Body& a, const Body& b, BooleanOp op) const {
  const TopoDS_Shape* sa = shape_of(a);
  const TopoDS_Shape* sb = shape_of(b);
  if (sa == nullptr || sb == nullptr) {
    return Err("boolean: body is not an OCCT body");
  }
  try {
    TopoDS_Shape result;
    switch (op) {
      case BooleanOp::Common:
        result = BRepAlgoAPI_Common(*sa, *sb).Shape();
        break;
      case BooleanOp::Cut:
        result = BRepAlgoAPI_Cut(*sa, *sb).Shape();
        break;
      case BooleanOp::Fuse:
      default:
        result = BRepAlgoAPI_Fuse(*sa, *sb).Shape();
        break;
    }
    if (result.IsNull()) {
      return Err("boolean: operation produced an empty shape");
    }
    return BodyRef{std::make_shared<OcctBody>(result)};
  } catch (const Standard_Failure& e) {
    return Err(std::string("boolean failed: ") + e.DynamicType()->Name());
  } catch (const std::exception& e) {
    return Err(std::string("boolean failed: ") + e.what());
  }
}

Result<BodyRef> OcctKernel::transform(const Body& body, Vec3 translation) const {
  return with_shape(body, "transform",
                    [translation](const TopoDS_Shape& shape) -> Result<BodyRef> {
                      // Tamias Y-up (tx,ty,tz) → OCCT Z-up (tx, -tz, ty)。
                      gp_Trsf tr;
                      tr.SetTranslation(gp_Vec(translation.x, -translation.z, translation.y));
                      const TopoDS_Shape moved =
                          BRepBuilderAPI_Transform(shape, tr, Standard_True).Shape();
                      return BodyRef{std::make_shared<OcctBody>(moved)};
                    });
}

Result<BodyRef> OcctKernel::cylinder(double radius, double height, Vec3 center,
                                     Vec3 axis) const {
  const double r = std::max(radius, 1e-4);
  const double h = std::max(height, 1e-4);
  const Vec3 safe_axis = length(axis) > 1e-8f ? normalize(axis) : Vec3{0.f, 1.f, 0.f};
  const Vec3 base = center - safe_axis * (static_cast<float>(h) * 0.5f);
  const TopoDS_Shape solid =
      BRepPrimAPI_MakeCylinder(gp_Ax2(tamias_point_to_occt(base), tamias_dir_to_occt(safe_axis)),
                               r, h)
          .Shape();
  if (solid.IsNull()) {
    return Err("cylinder: failed");
  }
  return BodyRef{std::make_shared<OcctBody>(solid)};
}

Result<std::vector<EdgeId>> OcctKernel::edges(const Body& body) const {
  const TopoDS_Shape* shape = shape_of(body);
  if (shape == nullptr) {
    return Err("edges: body is not an OCCT body");
  }
  const std::vector<TopoDS_Edge> list = collect_edges(*shape);
  std::vector<EdgeId> ids;
  ids.reserve(list.size());
  for (std::size_t i = 0; i < list.size(); ++i) {
    ids.push_back(static_cast<EdgeId>(i));
  }
  return ids;
}

Result<std::vector<EdgeMeasure>> OcctKernel::measure_edges(const Body& body) const {
  const TopoDS_Shape* shape = shape_of(body);
  if (shape == nullptr) {
    return Err("measure_edges: body is not an OCCT body");
  }
  const std::vector<TopoDS_Edge> list = collect_edges(*shape);
  try {
    TopTools_IndexedDataMapOfShapeListOfShape ancestors;
    TopExp::MapShapesAndAncestors(*shape, TopAbs_EDGE, TopAbs_FACE, ancestors);
    std::vector<EdgeMeasure> out(list.size());
    for (std::size_t i = 0; i < list.size(); ++i) {
      measure(list[i], ancestors, out[i]);
    }
    return out;
  } catch (const Standard_Failure& e) {
    return Err(std::string("measure_edges failed: ") + e.DynamicType()->Name());
  } catch (const std::exception& e) {
    return Err(std::string("measure_edges failed: ") + e.what());
  }
}

Result<BodyRef> OcctKernel::fillet(const Body& body, std::span<const EdgeId> edges_in,
                                   double radius) const {
  if (edges_in.empty()) {
    return Err("fillet: no edges given");
  }
  return with_shape(body, "fillet", [&](const TopoDS_Shape& shape) -> Result<BodyRef> {
    const std::vector<TopoDS_Edge> list = collect_edges(shape);
    BRepFilletAPI_MakeFillet fillet(shape);
    for (const EdgeId id : edges_in) {
      if (id >= list.size()) {
        return Err("fillet: edge id out of range");
      }
      fillet.Add(radius, list[id]);
    }
    fillet.Build();
    if (!fillet.IsDone()) {
      return Err("fillet: OCCT could not build the fillet");
    }
    return BodyRef{std::make_shared<OcctBody>(fillet.Shape())};
  });
}

Result<BodyRef> OcctKernel::chamfer(const Body& body, std::span<const EdgeId> edges_in,
                                    double distance) const {
  if (edges_in.empty()) {
    return Err("chamfer: no edges given");
  }
  return with_shape(body, "chamfer", [&](const TopoDS_Shape& shape) -> Result<BodyRef> {
    const std::vector<TopoDS_Edge> list = collect_edges(shape);
    BRepFilletAPI_MakeChamfer chamfer(shape);
    for (const EdgeId id : edges_in) {
      if (id >= list.size()) {
        return Err("chamfer: edge id out of range");
      }
      chamfer.Add(distance, list[id]);
    }
    chamfer.Build();
    if (!chamfer.IsDone()) {
      return Err("chamfer: OCCT could not build the chamfer");
    }
    return BodyRef{std::make_shared<OcctBody>(chamfer.Shape())};
  });
}

Result<MeshCpu> OcctKernel::tessellate(const Body& body, double linear_deflection) const {
  const TopoDS_Shape* shape = shape_of(body);
  if (shape == nullptr) {
    return Err("tessellate: body is not an OCCT body");
  }
  try {
    return tessellate_shape(*shape, linear_deflection);
  } catch (const Standard_Failure& e) {
    return Err(std::string("OCCT tessellation failed: ") + e.DynamicType()->Name());
  } catch (const std::exception& e) {
    return Err(std::string("OCCT tessellation failed: ") + e.what());
  }
}

Result<Aabb> OcctKernel::bounds(const Body& body) const {
  const TopoDS_Shape* shape = shape_of(body);
  if (shape == nullptr) {
    return Err("bounds: body is not an OCCT body");
  }
  Bnd_Box box;
  BRepBndLib::Add(*shape, box);
  if (box.IsVoid()) {
    return Aabb{};
  }
  double xmin = 0.0;
  double ymin = 0.0;
  double zmin = 0.0;
  double xmax = 0.0;
  double ymax = 0.0;
  double zmax = 0.0;
  box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
  // OCCT Z-up 盒的 8 个角转到 Tamias Y-up 后再取包围盒（旋转后不再是轴对齐盒）。
  Aabb out{};
  for (int i = 0; i < 8; ++i) {
    const gp_Pnt corner((i & 1) != 0 ? xmax : xmin, (i & 2) != 0 ? ymax : ymin,
                        (i & 4) != 0 ? zmax : zmin);
    out.expand(occt_point_to_tamias(corner));
  }
  return out;
}

const void* OcctKernel::native_handle(const Body& body) const {
  return shape_of(body);
}

void register_occt_kernel_backend() {
  register_kernel_backend(KernelModule{
      KernelBackend::Occt,
      [](const KernelCreateInfo&) -> Result<std::unique_ptr<ModelKernel>> {
        return std::make_unique<OcctKernel>();
      }});
}

const TopoDS_Shape* occt_shape_of(const Body& body) {
  return shape_of(body);
}

}  // namespace tamias
