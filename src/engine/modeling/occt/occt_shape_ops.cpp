#include "engine/modeling/occt/occt_shape_ops.h"

#include "engine/base/fs_utf8.h"
#include "engine/base/log.h"
#include "engine/modeling/occt/occt_error.h"
#include "engine/modeling/occt/occt_mesh.h"
#include "engine/profile/timing_scope.h"

#include <Bnd_Box.hxx>
#include <BRep_Builder.hxx>
#include <BRepBndLib.hxx>
#include <BRepTools.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <IGESCAFControl_Reader.hxx>
#include <Quantity_Color.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <TDF_Label.hxx>
#include <TDF_LabelSequence.hxx>
#include <TDocStd_Document.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

#include <algorithm>

namespace tamias {
namespace {

constexpr Vec3 kDefaultCadColor{0.75f, 0.78f, 0.82f};

std::string lower_ext(const std::filesystem::path& path) {
  return path_extension_lower(path);
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
    TAMIAS_TIMING_SCOPE("Shape::tessellate", TimingCategory::Modeling);
    if (shape_.IsNull()) {
      return Err("OCCT shape is null");
    }
    // 离散主体在 occt_mesh.cpp（跟求值路径共用一份），这里只补 XCAF 的逐面颜色和 UV。
    // guard：BRepMesh 与三角提取会抛 Standard_Failure，调用方（open_file、TessWorker）
    // 不接异常。
    return guard_occt("Shape::tessellate", [&]() -> Result<MeshCpu> {
      Quantity_Color root_qty;
      const bool has_root_color = lookup_color(colors_, shape_, root_qty);
      return tessellate_brep(
          shape_, linear_deflection,
          [&](const TopoDS_Face& face) {
            return resolve_face_color(shapes_, colors_, face, has_root_color, root_qty);
          },
          /*copy_uv=*/true);
    });
  }

  [[nodiscard]] Aabb bounds() const override {
    if (shape_.IsNull()) {
      return {};
    }
    // Shape::bounds() 没有 Result 通道（打开大件时只算一次，用来摆相机），所以出错
    // 返回空盒而不是抛出去。
    Aabb out{};
    try {
      Bnd_Box box;
      BRepBndLib::Add(shape_, box);
      if (box.IsVoid()) {
        return {};
      }
      Standard_Real xmin = 0;
      Standard_Real ymin = 0;
      Standard_Real zmin = 0;
      Standard_Real xmax = 0;
      Standard_Real ymax = 0;
      Standard_Real zmax = 0;
      box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
      const Vec3 corners[8] = {
          {static_cast<float>(xmin), static_cast<float>(ymin), static_cast<float>(zmin)},
          {static_cast<float>(xmax), static_cast<float>(ymin), static_cast<float>(zmin)},
          {static_cast<float>(xmin), static_cast<float>(ymax), static_cast<float>(zmin)},
          {static_cast<float>(xmax), static_cast<float>(ymax), static_cast<float>(zmin)},
          {static_cast<float>(xmin), static_cast<float>(ymin), static_cast<float>(zmax)},
          {static_cast<float>(xmax), static_cast<float>(ymin), static_cast<float>(zmax)},
          {static_cast<float>(xmin), static_cast<float>(ymax), static_cast<float>(zmax)},
          {static_cast<float>(xmax), static_cast<float>(ymax), static_cast<float>(zmax)},
      };
      // OCCT Z-up 盒的 8 个角转到 Tamias Y-up 后再取包围盒（旋转后不再是轴对齐盒）。
      for (const Vec3& c : corners) {
        out.expand({c.x, c.z, -c.y});
      }
    } catch (const Standard_Failure&) {
      return {};
    }
    return out;
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

// 装配体在这里被拍平成一个 compound：几何、位置、逐面颜色都保留，装配层级和实例
// 名字不保留 —— Shape 接口（kernel/shape_ops.h）只表达「一个 shape」，没有装配概念。
// 层级本身没丢：XCAF 文档跟着 OcctShape 一起活着，将来要给 BIM/装配树用，从这里
// 加一个新的访问入口即可，不用改这条链路。
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

// 真正干活的部分（异常边界在 OcctShapeOps::read_file）：按扩展名分派到三个 reader。
Result<std::unique_ptr<Shape>> read_cad_file(const std::filesystem::path& path) {
  const std::string ext = lower_ext(path);
  const std::string native = path_to_utf8(path);
  if (ext == ".step" || ext == ".stp") {
    Handle(TDocStd_Document) doc = new_xcaf_document();
    if (doc.IsNull()) {
      return Err("failed to create XCAF document");
    }
    STEPCAFControl_Reader reader;
    reader.SetColorMode(true);
    reader.SetNameMode(true);
    {
      TAMIAS_TIMING_SCOPE("STEPCAF ReadFile", TimingCategory::Modeling);
      if (reader.ReadFile(native.c_str()) != IFSelect_RetDone) {
        return Err("STEPCAFControl_Reader::ReadFile failed: " + native);
      }
    }
    {
      TAMIAS_TIMING_SCOPE("STEPCAF Transfer", TimingCategory::Modeling);
      if (!reader.Transfer(doc)) {
        return Err("STEPCAFControl_Reader::Transfer failed: " + native);
      }
    }
    const Handle(XCAFDoc_ShapeTool) shapes = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
    TopoDS_Shape shape = compound_free_shapes(shapes);
    if (shape.IsNull()) {
      return Err("OCCT produced an empty shape: " + native);
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
    {
      TAMIAS_TIMING_SCOPE("IGESCAF ReadFile", TimingCategory::Modeling);
      if (reader.ReadFile(native.c_str()) != IFSelect_RetDone) {
        return Err("IGESCAFControl_Reader::ReadFile failed: " + native);
      }
    }
    {
      TAMIAS_TIMING_SCOPE("IGESCAF Transfer", TimingCategory::Modeling);
      if (!reader.Transfer(doc)) {
        return Err("IGESCAFControl_Reader::Transfer failed: " + native);
      }
    }
    const Handle(XCAFDoc_ShapeTool) shapes = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
    TopoDS_Shape shape = compound_free_shapes(shapes);
    if (shape.IsNull()) {
      return Err("OCCT produced an empty shape: " + native);
    }
    return std::unique_ptr<Shape>(std::make_unique<OcctShape>(std::move(shape), doc));
  }
  if (ext == ".brep") {
    TopoDS_Shape shape;
    BRep_Builder builder;
    TAMIAS_TIMING_SCOPE("BRepTools::Read", TimingCategory::Modeling);
    if (!BRepTools::Read(shape, native.c_str(), builder)) {
      return Err("BRepTools::Read failed: " + native);
    }
    if (shape.IsNull()) {
      return Err("OCCT produced an empty shape: " + native);
    }
    return std::unique_ptr<Shape>(
        std::make_unique<OcctShape>(std::move(shape), Handle(TDocStd_Document){}));
  }
  return Err("OCCT unsupported extension: " + ext);
}

class OcctShapeOps final : public IShapeOps {
 public:
  [[nodiscard]] std::string name() const override { return "occt"; }

  [[nodiscard]] Result<std::unique_ptr<Shape>> read_file(
      const std::filesystem::path& path) const override {
    TAMIAS_TIMING_SCOPE("ShapeOps::read_file", TimingCategory::Modeling);
    // 文件损坏/解析失败时 OCCT 会抛 Standard_Failure（ReadFile、Transfer、XCAF、
    // BRepTools 都可能），而调用方是 Qt 事件循环里的 open_file —— 异常穿出去就是
    // 崩溃，所以在边界收成 Err。
    return guard_occt("read_file", [&] { return read_cad_file(path); });
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

}  // namespace tamias
