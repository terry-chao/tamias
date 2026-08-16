#include "occt_feature.h"

#if defined(TAMIAS_HAS_OCCT)

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Tool.hxx>
#include <Poly_Triangulation.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <unordered_map>

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

// 圆形轮廓面（在 XY 平面，中心在原点，半径 radius）。
TopoDS_Face make_circle_face(double radius) {
  const gp_Pnt center(0.0, 0.0, 0.0);
  const gp_Circ circle(gp_Ax2(center, gp_Dir(0.0, 0.0, 1.0)), radius);
  BRepBuilderAPI_MakeWire wire{BRepBuilderAPI_MakeEdge(circle)};
  return BRepBuilderAPI_MakeFace(wire).Face();
}

// 简化的 BRep → 三角网（无 XCAF 颜色逻辑；与 occt_shape_ops 的 tessellate 职责不同）。
Result<MeshCpu> tessellate_shape(const TopoDS_Shape& shape, double deflection) {
  BRepMesh_IncrementalMesh mesher(shape, deflection, Standard_False, 0.5, Standard_True);
  mesher.Perform();
  if (!mesher.IsDone()) {
    return Err("tessellate: BRepMesh failed");
  }

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
      v.color = {0.75f, 0.78f, 0.82f};
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
    for (int i = 1; i <= n_tris; ++i) {
      int n1 = 0;
      int n2 = 0;
      int n3 = 0;
      tri->Triangle(i).Get(n1, n2, n3);
      if (reversed) {
        std::swap(n2, n3);
      }
      mesh.indices.push_back(static_cast<std::uint32_t>(base + n1 - 1));
      mesh.indices.push_back(static_cast<std::uint32_t>(base + n2 - 1));
      mesh.indices.push_back(static_cast<std::uint32_t>(base + n3 - 1));
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
  return mesh;
}

}  // namespace

Result<MeshCpu> evaluate_feature_model(const FeatureModel& model, double linear_deflection) {
  std::unordered_map<std::uint64_t, TopoDS_Shape> shapes;
  for (const auto& f : model.features()) {
    TopoDS_Shape s;
    switch (f.kind) {
      case FeatureKind::RectProfile: {
        const double w = model.param(f.id, "width", 1.0);
        const double h = model.param(f.id, "height", 1.0);
        s = make_rect_face(w, h);
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
      default:
        return Err("unknown feature kind");
    }
    shapes[f.id] = s;
  }

  const Feature* out = model.output_feature();
  if (out == nullptr) {
    return Err("feature model has no features");
  }
  const auto it = shapes.find(out->id);
  if (it == shapes.end()) {
    return Err("output feature has no shape");
  }
  return tessellate_shape(it->second, linear_deflection);
}

}  // namespace tamias

#endif  // TAMIAS_HAS_OCCT
