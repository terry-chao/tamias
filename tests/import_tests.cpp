#include "engine/core/fs_utf8.h"
#include "engine/document/document.h"
#include "engine/io/mesh_io.h"
#include "engine/math/math.h"
#include "engine/modeling/occt_shape_ops.h"
#include "engine/modeling/shape_ops.h"

#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepTools.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <IGESControl_Writer.hxx>
#include <STEPControl_StepModelType.hxx>
#include <STEPControl_Writer.hxx>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace tamias {
namespace {

std::filesystem::path temp_dir() {
  const auto dir = std::filesystem::temp_directory_path() / "tamias_import_tests";
  std::filesystem::create_directories(dir);
  return dir;
}

void write_bytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
  std::ofstream out(path, std::ios::binary);
  ASSERT_TRUE(out);
  out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void append_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>(value));
  out.push_back(static_cast<std::uint8_t>(value >> 8));
  out.push_back(static_cast<std::uint8_t>(value >> 16));
  out.push_back(static_cast<std::uint8_t>(value >> 24));
}

std::vector<std::uint8_t> make_triangle_glb() {
  std::string json =
      R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],)"
      R"("meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],)"
      R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","max":[1,1,0],"min":[0,0,0]},)"
      R"({"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],)"
      R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],)"
      R"("buffers":[{"byteLength":44}]})";
  while (json.size() % 4 != 0) {
    json.push_back(' ');
  }

  std::vector<std::uint8_t> bin(44, 0);
  const float verts[9] = {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f};
  std::memcpy(bin.data(), verts, sizeof(verts));
  const std::uint16_t idx[3] = {0, 1, 2};
  std::memcpy(bin.data() + 36, idx, sizeof(idx));

  std::vector<std::uint8_t> file;
  file.reserve(12 + 8 + json.size() + 8 + bin.size());
  append_u32(file, 0x46546C67u);
  append_u32(file, 2);
  const std::uint32_t total =
      static_cast<std::uint32_t>(12 + 8 + json.size() + 8 + bin.size());
  append_u32(file, total);
  append_u32(file, static_cast<std::uint32_t>(json.size()));
  append_u32(file, 0x4E4F534Au);
  file.insert(file.end(), json.begin(), json.end());
  append_u32(file, static_cast<std::uint32_t>(bin.size()));
  append_u32(file, 0x004E4942u);
  file.insert(file.end(), bin.begin(), bin.end());
  return file;
}

IShapeOps* occt() {
  auto* ops = ShapeOpsRegistry::instance().find("occt");
  if (ops == nullptr) {
    register_occt_shape_ops();
    ops = ShapeOpsRegistry::instance().find("occt");
  }
  return ops;
}

TopoDS_Shape box_shape() {
  BRepPrimAPI_MakeBox box(2.0, 3.0, 4.0);
  return box.Shape();
}

void expect_cad_mesh(const Result<std::unique_ptr<Shape>>& shape, const char* label) {
  ASSERT_TRUE(shape) << label << ": " << shape.error();
  auto mesh = (*shape)->tessellate(0.2);
  ASSERT_TRUE(mesh) << label << ": " << mesh.error();
  EXPECT_FALSE(mesh->indices.empty()) << label;
  EXPECT_TRUE(mesh->bounds.valid()) << label;
}

TEST(MeshIo, LoadsMinimalGlbTriangle) {
  const auto path = temp_dir() / "tri.glb";
  write_bytes(path, make_triangle_glb());
  auto mesh = load_gltf(path);
  ASSERT_TRUE(mesh) << mesh.error();
  EXPECT_EQ(mesh->indices.size(), 3u);
  EXPECT_TRUE(mesh->bounds.valid());

  Document doc("glb-import");
  const std::uint64_t id =
      doc.add_import_mesh("tri", std::move(*mesh), Mat4::identity(), {0.8f, 0.8f, 0.8f});
  EXPECT_NE(id, 0u);
  EXPECT_FALSE(doc.render_items().empty());
}

TEST(MeshIo, RejectsAsciiGltf) {
  const auto path = temp_dir() / "box.gltf";
  {
    std::ofstream out(path);
    ASSERT_TRUE(out);
    out << R"({"asset":{"version":"2.0"}})";
  }
  auto mesh = load_gltf(path);
  ASSERT_FALSE(mesh);
  EXPECT_NE(mesh.error().find("not supported"), std::string::npos);
}

TEST(MeshIo, RejectsBadGlbMagic) {
  const auto path = temp_dir() / "bad.glb";
  write_bytes(path, {0x00, 0x01, 0x02, 0x03});
  auto mesh = load_gltf(path);
  ASSERT_FALSE(mesh);
}

TEST(OcctImport, SupportsCadExtensionsOnly) {
  EXPECT_TRUE(occt_supports_extension("a.step"));
  EXPECT_TRUE(occt_supports_extension("a.stp"));
  EXPECT_TRUE(occt_supports_extension("a.iges"));
  EXPECT_TRUE(occt_supports_extension("a.igs"));
  EXPECT_TRUE(occt_supports_extension("a.brep"));
  EXPECT_FALSE(occt_supports_extension("a.obj"));
  EXPECT_FALSE(occt_supports_extension("a.glb"));
}

TEST(OcctImport, ReadsWrittenBrepStepIgesBox) {
  auto* ops = occt();
  ASSERT_NE(ops, nullptr);
  const auto shape = box_shape();
  const auto dir = temp_dir();
  const auto brep = dir / "box.brep";
  const auto step = dir / "box.step";
  const auto iges = dir / "box.iges";

  ASSERT_TRUE(BRepTools::Write(shape, path_to_utf8(brep).c_str()));

  STEPControl_Writer step_writer;
  ASSERT_EQ(step_writer.Transfer(shape, STEPControl_AsIs), IFSelect_RetDone);
  ASSERT_EQ(step_writer.Write(path_to_utf8(step).c_str()), IFSelect_RetDone);

  IGESControl_Writer iges_writer;
  ASSERT_TRUE(iges_writer.AddShape(shape));
  iges_writer.ComputeModel();
  ASSERT_TRUE(iges_writer.Write(path_to_utf8(iges).c_str()));

  expect_cad_mesh(ops->read_file(brep), "brep");
  expect_cad_mesh(ops->read_file(step), "step");
  expect_cad_mesh(ops->read_file(iges), "iges");
}

}  // namespace
}  // namespace tamias
