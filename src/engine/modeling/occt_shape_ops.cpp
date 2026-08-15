#include "occt_shape_ops.h"

#if defined(TAMIAS_HAS_OCCT)

#include "engine/core/log.h"

#include <BRep_Builder.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRep_Tool.hxx>
#include <BRepTools.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <IGESCAFControl_Reader.hxx>
#include <Poly_Triangulation.hxx>
#include <Quantity_Color.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <TDF_Label.hxx>
#include <TDF_LabelSequence.hxx>
#include <TDocStd_Document.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <gp_Trsf.hxx>

#include <algorithm>
#include <cctype>

namespace tamias {
namespace {

constexpr Vec3 kDefaultCadColor{0.75f, 0.78f, 0.82f};

std::string lower_ext(const std::filesystem::path& path) {
  std::string ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return ext;
}

bool lookup_color(const Handle(XCAFDoc_ColorTool)& colors, const TopoDS_Shape& shape,
                  Quantity_Color& out) {
  if (colors.IsNull() || shape.IsNull()) {
    return false;
  }
  return colors->GetColor(shape, XCAFDoc_ColorSurf, out) ||
         colors->GetColor(shape, XCAFDoc_ColorGen, out) ||
         colors->GetColor(shape, XCAFDoc_ColorCurv, out);
}

bool lookup_color(const Handle(XCAFDoc_ColorTool)& colors, const TDF_Label& label,
                  Quantity_Color& out) {
  if (colors.IsNull() || label.IsNull()) {
    return false;
  }
  return colors->GetColor(label, XCAFDoc_ColorSurf, out) ||
         colors->GetColor(label, XCAFDoc_ColorGen, out) ||
         colors->GetColor(label, XCAFDoc_ColorCurv, out);
}

Vec3 to_vec3(const Quantity_Color& c) {
  return {static_cast<float>(c.Red()), static_cast<float>(c.Green()),
          static_cast<float>(c.Blue())};
}

Vec3 resolve_face_color(const Handle(XCAFDoc_ShapeTool)& shapes,
                        const Handle(XCAFDoc_ColorTool)& colors, const TopoDS_Face& face,
                        bool has_root_color, const Quantity_Color& root_qty) {
  Quantity_Color qty;
  if (lookup_color(colors, face, qty)) {
    return to_vec3(qty);
  }
  if (!shapes.IsNull()) {
    TDF_Label label;
    if (shapes->Search(face, label)) {
      for (TDF_Label cur = label; !cur.IsNull(); cur = cur.Father()) {
        if (lookup_color(colors, cur, qty)) {
          return to_vec3(qty);
        }
        // Stop at document root children depth; Father of Main is null-ish.
        if (cur.Depth() <= 1) {
          break;
        }
      }
    }
  }
  if (has_root_color) {
    return to_vec3(root_qty);
  }
  return kDefaultCadColor;
}

class OcctShape final : public Shape {
 public:
  OcctShape(TopoDS_Shape shape, Handle(TDocStd_Document) doc)
      : shape_(std::move(shape)), doc_(std::move(doc)) {
    if (!doc_.IsNull()) {
      shapes_ = XCAFDoc_DocumentTool::ShapeTool(doc_->Main());
      colors_ = XCAFDoc_DocumentTool::ColorTool(doc_->Main());
    }
  }

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

    Quantity_Color root_qty;
    const bool has_root_color = lookup_color(colors_, shape_, root_qty);

    MeshCpu mesh;
    for (TopExp_Explorer exp(shape_, TopAbs_FACE); exp.More(); exp.Next()) {
      const TopoDS_Face face = TopoDS::Face(exp.Current());
      TopLoc_Location loc;
      Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
      if (tri.IsNull()) {
        continue;
      }

      Vec3 face_color =
          resolve_face_color(shapes_, colors_, face, has_root_color, root_qty);

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
        v.color = face_color;
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
    // OCCT is Z-up; Tamias viewport is Y-up (glTF). Rotate -90° about X:
    // (x, y, z) -> (x, z, -y).
    for (auto& v : mesh.vertices) {
      const Vec3 p = v.position;
      v.position = {p.x, p.z, -p.y};
      const Vec3 n = v.normal;
      v.normal = {n.x, n.z, -n.y};
    }
    recompute_bounds(mesh);
    return mesh;
  }

  [[nodiscard]] const TopoDS_Shape& shape() const { return shape_; }

 private:
  TopoDS_Shape shape_;
  Handle(TDocStd_Document) doc_;
  Handle(XCAFDoc_ShapeTool) shapes_;
  Handle(XCAFDoc_ColorTool) colors_;
};

Handle(TDocStd_Document) new_xcaf_document() {
  Handle(TDocStd_Document) doc;
  XCAFApp_Application::GetApplication()->NewDocument("MDTV-XCAF", doc);
  return doc;
}

TopoDS_Shape compound_free_shapes(const Handle(XCAFDoc_ShapeTool)& shapes) {
  TDF_LabelSequence free_shapes;
  shapes->GetFreeShapes(free_shapes);
  if (free_shapes.Length() == 0) {
    return {};
  }
  if (free_shapes.Length() == 1) {
    return shapes->GetShape(free_shapes.Value(1));
  }
  TopoDS_Compound compound;
  BRep_Builder builder;
  builder.MakeCompound(compound);
  for (int i = 1; i <= free_shapes.Length(); ++i) {
    builder.Add(compound, shapes->GetShape(free_shapes.Value(i)));
  }
  return compound;
}

class OcctShapeOps final : public IShapeOps {
 public:
  [[nodiscard]] std::string name() const override { return "occt"; }

  [[nodiscard]] Result<std::unique_ptr<Shape>> read_file(
      const std::filesystem::path& path) const override {
    const std::string ext = lower_ext(path);
    if (ext == ".step" || ext == ".stp") {
      Handle(TDocStd_Document) doc = new_xcaf_document();
      if (doc.IsNull()) {
        return Err("failed to create XCAF document");
      }
      STEPCAFControl_Reader reader;
      reader.SetColorMode(true);
      reader.SetNameMode(true);
      if (reader.ReadFile(path.string().c_str()) != IFSelect_RetDone) {
        return Err("STEPCAFControl_Reader::ReadFile failed: " + path.string());
      }
      if (!reader.Transfer(doc)) {
        return Err("STEPCAFControl_Reader::Transfer failed: " + path.string());
      }
      Handle(XCAFDoc_ShapeTool) shapes = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
      TopoDS_Shape shape = compound_free_shapes(shapes);
      if (shape.IsNull()) {
        return Err("OCCT produced an empty shape: " + path.string());
      }
      return std::unique_ptr<Shape>(std::make_unique<OcctShape>(std::move(shape), doc));
    }
    if (ext == ".iges" || ext == ".igs") {
      Handle(TDocStd_Document) doc = new_xcaf_document();
      if (doc.IsNull()) {
        return Err("failed to create XCAF document");
      }
      IGESCAFControl_Reader reader;
      reader.SetColorMode(true);
      reader.SetNameMode(true);
      if (reader.ReadFile(path.string().c_str()) != IFSelect_RetDone) {
        return Err("IGESCAFControl_Reader::ReadFile failed: " + path.string());
      }
      if (!reader.Transfer(doc)) {
        return Err("IGESCAFControl_Reader::Transfer failed: " + path.string());
      }
      Handle(XCAFDoc_ShapeTool) shapes = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
      TopoDS_Shape shape = compound_free_shapes(shapes);
      if (shape.IsNull()) {
        return Err("OCCT produced an empty shape: " + path.string());
      }
      return std::unique_ptr<Shape>(std::make_unique<OcctShape>(std::move(shape), doc));
    }
    if (ext == ".brep") {
      TopoDS_Shape shape;
      BRep_Builder builder;
      if (!BRepTools::Read(shape, path.string().c_str(), builder)) {
        return Err("BRepTools::Read failed: " + path.string());
      }
      if (shape.IsNull()) {
        return Err("OCCT produced an empty shape: " + path.string());
      }
      return std::unique_ptr<Shape>(
          std::make_unique<OcctShape>(std::move(shape), Handle(TDocStd_Document){}));
    }
    return Err("OCCT unsupported extension: " + ext);
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
  OcctShape shape(box.Shape(), Handle(TDocStd_Document){});
  return shape.tessellate(0.5);
}

}  // namespace tamias

#endif  // TAMIAS_HAS_OCCT
