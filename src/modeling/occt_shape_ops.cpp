#include "occt_shape_ops.h"

#if defined(TAMIAS_HAS_OCCT)

#include "core/log.h"

#include <BRep_Builder.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRep_Tool.hxx>
#include <BRepTools.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <IGESControl_Reader.hxx>
#include <Poly_Triangulation.hxx>
#include <STEPControl_Reader.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Trsf.hxx>

#include <algorithm>
#include <cctype>

namespace tamias {
namespace {

std::string lower_ext(const std::filesystem::path& path) {
  std::string ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return ext;
}

class OcctShape final : public Shape {
 public:
  explicit OcctShape(TopoDS_Shape shape) : shape_(std::move(shape)) {}

  [[nodiscard]] std::string backend_name() const override { return "occt"; }

  [[nodiscard]] Result<MeshCpu> tessellate(double linear_deflection) const override {
    if (shape_.IsNull()) {
      return Err("OCCT shape is null");
    }
    const double deflection = std::max(linear_deflection, 1e-4);
    BRepMesh_IncrementalMesh mesher(shape_, deflection, Standard_False, 0.5, Standard_True);
    mesher.Perform();
    if (!mesher.IsDone()) {
      return Err("BRepMesh_IncrementalMesh failed");
    }

    MeshCpu mesh;
    for (TopExp_Explorer exp(shape_, TopAbs_FACE); exp.More(); exp.Next()) {
      const TopoDS_Face face = TopoDS::Face(exp.Current());
      TopLoc_Location loc;
      Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
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
        if (tri->HasNormals()) {
          gp_Dir n = tri->Normal(i);
          if (reversed) {
            n.Reverse();
          }
          n.Transform(trsf);
          v.normal = {static_cast<float>(n.X()), static_cast<float>(n.Y()),
                      static_cast<float>(n.Z())};
        }
        if (tri->HasUVNodes()) {
          const gp_Pnt2d uv = tri->UVNode(i);
          v.uv = {static_cast<float>(uv.X()), static_cast<float>(uv.Y())};
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
        const std::uint32_t i0 = static_cast<std::uint32_t>(base + n1 - 1);
        const std::uint32_t i1 = static_cast<std::uint32_t>(base + n2 - 1);
        const std::uint32_t i2 = static_cast<std::uint32_t>(base + n3 - 1);
        mesh.indices.push_back(i0);
        mesh.indices.push_back(i1);
        mesh.indices.push_back(i2);

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
    }

    if (mesh.indices.empty()) {
      return Err("OCCT tessellation produced no triangles");
    }
    recompute_bounds(mesh);
    return mesh;
  }

  [[nodiscard]] const TopoDS_Shape& shape() const { return shape_; }

 private:
  TopoDS_Shape shape_;
};

class OcctShapeOps final : public IShapeOps {
 public:
  [[nodiscard]] std::string name() const override { return "occt"; }

  [[nodiscard]] Result<std::unique_ptr<Shape>> read_file(
      const std::filesystem::path& path) const override {
    const std::string ext = lower_ext(path);
    TopoDS_Shape shape;
    if (ext == ".step" || ext == ".stp") {
      STEPControl_Reader reader;
      if (reader.ReadFile(path.string().c_str()) != IFSelect_RetDone) {
        return Err("STEPControl_Reader::ReadFile failed: " + path.string());
      }
      reader.TransferRoots();
      shape = reader.OneShape();
    } else if (ext == ".iges" || ext == ".igs") {
      IGESControl_Reader reader;
      if (reader.ReadFile(path.string().c_str()) != IFSelect_RetDone) {
        return Err("IGESControl_Reader::ReadFile failed: " + path.string());
      }
      reader.TransferRoots();
      shape = reader.OneShape();
    } else if (ext == ".brep") {
      BRep_Builder builder;
      if (!BRepTools::Read(shape, path.string().c_str(), builder)) {
        return Err("BRepTools::Read failed: " + path.string());
      }
    } else {
      return Err("OCCT unsupported extension: " + ext);
    }
    if (shape.IsNull()) {
      return Err("OCCT produced an empty shape: " + path.string());
    }
    return std::unique_ptr<Shape>(std::make_unique<OcctShape>(std::move(shape)));
  }
};

}  // namespace

bool occt_supports_extension(const std::filesystem::path& path) {
  const std::string ext = lower_ext(path);
  return ext == ".step" || ext == ".stp" || ext == ".iges" || ext == ".igs" || ext == ".brep";
}

void register_occt_shape_ops() {
  log_info("Registering OCCT ShapeOps (STEP/IGES/BREP)");
  ShapeOpsRegistry::instance().register_ops(std::make_unique<OcctShapeOps>());
}

Result<MeshCpu> tessellate_occt_box_for_tests() {
  BRepPrimAPI_MakeBox box(10.0, 10.0, 10.0);
  OcctShape shape(box.Shape());
  return shape.tessellate(0.5);
}

}  // namespace tamias

#endif  // TAMIAS_HAS_OCCT
