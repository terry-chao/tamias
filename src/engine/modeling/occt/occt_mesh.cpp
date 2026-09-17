#include "engine/modeling/occt/occt_mesh.h"

#include "engine/profile/timing_scope.h"

#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <Poly_Triangulation.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Trsf.hxx>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace tamias {
namespace {

// 退化三角形（三点重合/共线）的叉积是零向量，归一化会灌 NaN 进网格。
// 阈值按两条边长的乘积缩放，所以跟模型尺寸无关；NaN 比较为 false 也会走这里跳过。
bool assign_triangle_normal(MeshCpu& mesh, std::uint32_t i0, std::uint32_t i1, std::uint32_t i2) {
  const Vec3 a = mesh.vertices[i0].position;
  const Vec3 b = mesh.vertices[i1].position;
  const Vec3 c = mesh.vertices[i2].position;
  const Vec3 n = cross(b - a, c - a);
  const float scale = std::max(length(b - a) * length(c - a), 1e-12f);
  if (!(length(n) > 1e-12f * scale)) {
    return false;
  }
  const Vec3 unit = normalize(n);
  mesh.vertices[i0].normal = unit;
  mesh.vertices[i1].normal = unit;
  mesh.vertices[i2].normal = unit;
  return true;
}

}  // namespace

Result<MeshCpu> tessellate_brep(const TopoDS_Shape& shape, double linear_deflection,
                                const std::function<Vec3(const TopoDS_Face&)>& face_color,
                                bool copy_uv) {
  if (shape.IsNull()) {
    return Err("tessellate: shape is null");
  }
  const double deflection = std::max(linear_deflection, 1e-4);
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

    const Vec3 color = face_color ? face_color(face) : Vec3{1.0f, 1.0f, 1.0f};
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
      v.color = color;
      if (tri->HasNormals()) {
        gp_Dir n = tri->Normal(i);
        if (reversed) {
          n.Reverse();
        }
        n.Transform(trsf);
        v.normal = {static_cast<float>(n.X()), static_cast<float>(n.Y()),
                    static_cast<float>(n.Z())};
      }
      if (copy_uv && tri->HasUVNodes()) {
        const gp_Pnt2d uv = tri->UVNode(i);
        v.uv = {static_cast<float>(uv.X()), static_cast<float>(uv.Y())};
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
        assign_triangle_normal(mesh, i0, i1, i2);
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

}  // namespace tamias
