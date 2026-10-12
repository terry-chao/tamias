#include "command/core/command_system.h"
#include "command/import/import_mesh_command.h"
#include "engine/base/fs_utf8.h"
#include "engine/document/document.h"
#include "engine/io/imported_model.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace tamias {
namespace {

std::filesystem::path temp_dir() {
  const auto dir = std::filesystem::temp_directory_path() / "tamias_import_mesh_tests";
  std::filesystem::create_directories(dir);
  return dir;
}

void write_text(const std::filesystem::path& path, const std::string& text) {
  std::ofstream out(path, std::ios::binary);
  ASSERT_TRUE(out);
  out << text;
}

// 最小合法 OBJ：一个三角形。走的是真正的 loader，不是手工造的 MeshCpu。
std::filesystem::path write_triangle_obj(const std::string& file_name) {
  const auto path = temp_dir() / file_name;
  write_text(path, "v 0 0 0\nv 1 0 0\nv 0 1 0\nvn 0 0 1\nf 1//1 2//1 3//1\n");
  return path;
}

MeshCpu triangle_mesh() {
  MeshCpu mesh;
  mesh.vertices.resize(3);
  mesh.vertices[0].position = {0.f, 0.f, 0.f};
  mesh.vertices[1].position = {1.f, 0.f, 0.f};
  mesh.vertices[2].position = {0.f, 1.f, 0.f};
  mesh.vertices[0].normal = mesh.vertices[1].normal = mesh.vertices[2].normal = {0.f, 0.f, 1.f};
  mesh.indices = {0, 1, 2};
  recompute_bounds(mesh);
  return mesh;
}

TextureAsset solid_texture(const std::string& name, TextureUsage usage, bool srgb,
                           std::uint8_t value) {
  TextureAsset tex;
  tex.name = name;
  tex.usage = usage;
  tex.srgb = srgb;
  tex.width = 2;
  tex.height = 2;
  tex.rgba.assign(2u * 2u * 4u, value);
  tex.content_hash = texture_content_hash(tex);
  return tex;
}

ImportedModel model_with_textures() {
  ImportedModel model;
  model.mesh = triangle_mesh();
  model.base_color = {0.2f, 0.4f, 0.6f};
  model.roughness = 0.25f;
  model.metallic = 0.75f;
  return model;
}

struct Cmd {
  CommandRegistry registry;
  CommandSystem system;
  Document doc;

  explicit Cmd(const char* name) : system(registry), doc(name) { register_commands(registry); }
};

// 路径入口：读盘、建网格 + 材质 + 节点，一条撤销。
TEST(ImportMesh, PathImportAddsNodeMeshAndMaterial) {
  Cmd cmd("import-mesh");
  const std::size_t materials_before = cmd.doc.materials().size();
  const std::size_t nodes_before = cmd.doc.scene().nodes().size();

  ImportMeshCommand command(cmd.doc, write_triangle_obj("plain.obj"), "plain");
  ASSERT_TRUE(command.execute());

  EXPECT_EQ(cmd.doc.scene().nodes().size(), nodes_before + 1);
  EXPECT_EQ(cmd.doc.materials().size(), materials_before + 1);
  ASSERT_NE(cmd.doc.mesh(command.mesh_id()), nullptr);
  ASSERT_NE(cmd.doc.material(command.material_id()), nullptr);
  const SceneNode* node = cmd.doc.scene().find(command.node_id());
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->mesh_asset_id, command.mesh_id());
  EXPECT_EQ(node->material_id, command.material_id());
  EXPECT_TRUE(cmd.doc.dirty());
}

// 撤销要连带收掉网格 / 材质 / 节点，重做按原 id 放回。
TEST(ImportMesh, UndoRedoRestoresSameIds) {
  Cmd cmd("import-mesh-undo");
  const std::size_t materials_before = cmd.doc.materials().size();
  const std::size_t nodes_before = cmd.doc.scene().nodes().size();

  auto command = std::make_unique<ImportMeshCommand>(cmd.doc, write_triangle_obj("undo.obj"),
                                                     "undo");
  ImportMeshCommand* raw = command.get();
  ASSERT_TRUE(raw->execute());
  const std::uint64_t node_id = raw->node_id();
  const std::uint64_t mesh_id = raw->mesh_id();
  const std::uint64_t material_id = raw->material_id();
  cmd.system.push_executed(std::move(command));

  cmd.system.undo();
  EXPECT_EQ(cmd.doc.scene().nodes().size(), nodes_before);
  EXPECT_EQ(cmd.doc.materials().size(), materials_before);
  EXPECT_EQ(cmd.doc.mesh(mesh_id), nullptr);
  EXPECT_EQ(cmd.doc.material(material_id), nullptr);

  cmd.system.redo();
  EXPECT_EQ(cmd.doc.scene().nodes().size(), nodes_before + 1);
  ASSERT_NE(cmd.doc.mesh(mesh_id), nullptr);
  ASSERT_NE(cmd.doc.material(material_id), nullptr);
  const SceneNode* node = cmd.doc.scene().find(node_id);
  ASSERT_NE(node, nullptr) << "重做要保留场景节点 id，否则选中集与引用会断线";
  EXPECT_EQ(node->mesh_asset_id, mesh_id);
  EXPECT_EQ(node->material_id, material_id);
}

// 已解码的贴图要落进材质的三个槽位，撤销时随材质一起收掉。
TEST(ImportMesh, DecodedTexturesLandInMaterialSlots) {
  Cmd cmd("import-mesh-textures");
  const std::size_t textures_before = cmd.doc.textures().size();

  ImportMeshCommand::Textures textures;
  textures.albedo = solid_texture("albedo", TextureUsage::Albedo, true, 200);
  textures.normal = solid_texture("normal", TextureUsage::Normal, false, 128);
  textures.orm = solid_texture("orm", TextureUsage::Orm, false, 64);

  ImportMeshCommand command(cmd.doc, model_with_textures(), "textured", Mat4::identity(),
                            std::move(textures));
  ASSERT_TRUE(command.execute());
  EXPECT_EQ(cmd.doc.textures().size(), textures_before + 3);

  const Material* material = cmd.doc.material(command.material_id());
  ASSERT_NE(material, nullptr);
  EXPECT_NE(material->albedo_texture_id, 0u);
  EXPECT_NE(material->normal_texture_id, 0u);
  EXPECT_NE(material->orm_texture_id, 0u);
  EXPECT_NE(cmd.doc.texture(material->albedo_texture_id), nullptr);
  EXPECT_EQ(cmd.doc.texture(material->albedo_texture_id)->usage, TextureUsage::Albedo);
  // 材质标量跟着 model 走。
  EXPECT_FLOAT_EQ(material->roughness, 0.25f);
  EXPECT_FLOAT_EQ(material->metallic, 0.75f);

  command.undo();
  EXPECT_EQ(cmd.doc.textures().size(), textures_before);
}

// 同一份几何导入两次共用一份网格资产；撤销第二次不能把第一次在用的网格收走。
TEST(ImportMesh, DuplicateGeometrySharesOneAsset) {
  Cmd cmd("import-mesh-dedupe");
  const std::size_t meshes_before = cmd.doc.meshes().size();

  auto first = std::make_unique<ImportMeshCommand>(cmd.doc, model_with_textures(), "a");
  ImportMeshCommand* raw_first = first.get();
  ASSERT_TRUE(raw_first->execute());
  cmd.system.push_executed(std::move(first));

  auto second = std::make_unique<ImportMeshCommand>(cmd.doc, model_with_textures(), "b");
  ImportMeshCommand* raw_second = second.get();
  ASSERT_TRUE(raw_second->execute());
  EXPECT_EQ(raw_second->mesh_id(), raw_first->mesh_id()) << "相同三角网应当 intern 成一份资产";
  cmd.system.push_executed(std::move(second));
  EXPECT_EQ(cmd.doc.meshes().size(), meshes_before + 1);

  cmd.system.undo();
  ASSERT_NE(cmd.doc.mesh(raw_first->mesh_id()), nullptr)
      << "第一个节点还在引用，网格不能被收走";
  EXPECT_EQ(cmd.doc.scene().nodes().size(), 1u);
}

// 空数据 / 读不到文件都要给得出原因的失败，而不是静默成功。
TEST(ImportMesh, EmptyAndMissingInputsFailWithReason) {
  Cmd cmd("import-mesh-errors");

  ImportMeshCommand empty(cmd.doc, ImportedModel{}, "empty");
  const auto empty_result = empty.execute();
  ASSERT_FALSE(empty_result);
  EXPECT_NE(empty_result.error().find("no source path"), std::string::npos);

  ImportMeshCommand missing(cmd.doc, temp_dir() / "does-not-exist.obj", "missing");
  const auto missing_result = missing.execute();
  ASSERT_FALSE(missing_result);
  EXPECT_FALSE(missing_result.error().empty());
  EXPECT_TRUE(cmd.doc.scene().nodes().empty());
}

// 命令注册表入口：AI / 脚本走的就是这条（路径 + 放置）。
TEST(ImportMesh, DispatchThroughRegistryAppliesPlacement) {
  Cmd cmd("import-mesh-dispatch");
  const std::string path = path_to_utf8(write_triangle_obj("dispatch.obj"));

  ASSERT_TRUE(cmd.system.dispatch(cmd.doc, "import_mesh",
                                  {{"path", path},
                                   {"name", "meshy-part"},
                                   {"origin", Vec3{1.f, 2.f, 3.f}},
                                   {"scale", 2.0}}));
  ASSERT_EQ(cmd.doc.scene().nodes().size(), 1u);
  const SceneNode& node = cmd.doc.scene().nodes().front();
  EXPECT_EQ(node.name, "meshy-part");
  EXPECT_FLOAT_EQ(node.local_transform(0, 3), 1.f);
  EXPECT_FLOAT_EQ(node.local_transform(1, 3), 2.f);
  EXPECT_FLOAT_EQ(node.local_transform(2, 3), 3.f);
  EXPECT_FLOAT_EQ(node.local_transform(0, 0), 2.f) << "scale 要作用在几何上（Meshy 单位换算）";

  cmd.system.undo();
  EXPECT_TRUE(cmd.doc.scene().nodes().empty());
}

}  // namespace
}  // namespace tamias
