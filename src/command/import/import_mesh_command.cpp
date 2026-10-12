#include "command/import/import_mesh_command.h"

#include "engine/base/fs_utf8.h"
#include "engine/io/mesh_io.h"

#include <utility>

namespace tamias {

ImportMeshCommand::ImportMeshCommand(Document& document, ImportedModel model, std::string name,
                                     Mat4 transform, Textures textures)
    : document_(&document),
      model_(std::move(model)),
      name_(std::move(name)),
      transform_(transform) {
  albedo_.asset = std::move(textures.albedo);
  normal_.asset = std::move(textures.normal);
  orm_.asset = std::move(textures.orm);
}

ImportMeshCommand::ImportMeshCommand(Document& document, std::filesystem::path source,
                                     std::string name, Mat4 transform)
    : document_(&document),
      source_(std::move(source)),
      name_(std::move(name)),
      transform_(transform) {}

Result<void> ImportMeshCommand::load_from_source() {
  auto loaded = load_mesh_model(source_);
  if (!loaded) {
    return Err(loaded.error());
  }
  model_ = std::move(*loaded);
  if (name_.empty()) {
    name_ = path_to_utf8(source_.filename());
  }
  return {};
}

void ImportMeshCommand::import_texture_slot(Slot& slot, TextureAsset incoming) {
  if (incoming.rgba.empty()) {
    return;
  }
  if (incoming.content_hash == 0) {
    incoming.content_hash = texture_content_hash(incoming);
  }
  // 同一张图（只按像素指纹）不存第二份——别人已经有一份就复用，undo 时也不该删。
  TextureAsset& stored = document_->import_texture(std::move(incoming));
  slot.asset = stored;
}

void ImportMeshCommand::drop_texture_slot(const Slot& slot) {
  if (slot.asset.id == 0 || document_->texture(slot.asset.id) == nullptr) {
    return;
  }
  if (document_->texture_ref_count(slot.asset.id) != 0) {
    return;  // 还有材质（包括别人的）在用这张图
  }
  document_->remove_texture(slot.asset.id);
}

bool ImportMeshCommand::mesh_referenced_by_other_node() const {
  for (const SceneNode& node : document_->scene().nodes()) {
    if (node.id != node_.id && node.mesh_asset_id == mesh_id_) {
      return true;
    }
  }
  return false;
}

Result<void> ImportMeshCommand::execute() {
  if (document_ == nullptr) {
    return Err("ImportMeshCommand: no document");
  }
  if (!source_.empty()) {
    if (auto r = load_from_source(); !r) {
      return r;
    }
  }
  if (model_.mesh.vertices.empty() || model_.mesh.indices.empty()) {
    return Err(source_.empty() ? "ImportMeshCommand: no source path and no mesh data"
                               : "ImportMeshCommand: file contained no triangles");
  }
  if (name_.empty()) {
    name_ = "Imported mesh";
  }

  import_texture_slot(albedo_, albedo_.asset);
  import_texture_slot(normal_, normal_.asset);
  import_texture_slot(orm_, orm_.asset);

  Material material{};
  material.name = name_;
  material.base_color = model_.base_color;
  material.roughness = model_.roughness;
  material.metallic = model_.metallic;
  material.opacity = model_.opacity;
  material.albedo_texture_id = albedo_.asset.id;
  material.normal_texture_id = normal_.asset.id;
  material.orm_texture_id = orm_.asset.id;
  material_ = document_->add_material(std::move(material));

  // 有顶点色时用顶点色，否则用材质的 base_color —— 与「打开网格文件」同一套判断。
  const bool has_colors = mesh_has_vertex_colors(model_.mesh);
  const Vec3 color = has_colors ? Vec3{1.f, 1.f, 1.f} : model_.base_color;

  // intern 按内容去重：相同三角网（例如同一个 Meshy 结果导入两次）复用同一份资产。
  MeshAsset& stored = document_->intern_mesh(name_, model_.mesh);
  mesh_id_ = stored.id;

  SceneNode node{};
  node.name = name_;
  node.mesh_asset_id = mesh_id_;
  node.material_id = material_.id;
  node.local_transform = transform_;
  node.color = color;
  node_ = document_->scene().add_node(std::move(node));
  document_->bind_work_lod(mesh_id_);
  document_->recompute_scene();
  document_->mark_dirty();
  executed_ = true;
  return {};
}

void ImportMeshCommand::undo() {
  if (!executed_) {
    return;
  }
  document_->scene().remove_node(node_.id);
  if (document_->mesh(mesh_id_) != nullptr && !mesh_referenced_by_other_node()) {
    document_->invalidate_geometry_lods(mesh_id_);
    document_->remove_mesh(mesh_id_);
  }
  if (document_->material(material_.id) != nullptr &&
      document_->material_user_count(material_.id) == 0) {
    document_->remove_material(material_.id);
  }
  drop_texture_slot(albedo_);
  drop_texture_slot(normal_);
  drop_texture_slot(orm_);
  document_->recompute_scene();
  document_->mark_dirty();
}

void ImportMeshCommand::redo() {
  if (!executed_) {
    return;
  }
  // 贴图 / 材质 / 网格都按原 id 放回，场景节点也保留原 id：撤销期间谁引用了它们都不断线。
  const Slot* slots[3] = {&albedo_, &normal_, &orm_};
  for (const Slot* slot : slots) {
    if (slot->asset.id != 0 && document_->texture(slot->asset.id) == nullptr) {
      document_->insert_texture(slot->asset);
    }
  }
  if (material_.id != 0 && document_->material(material_.id) == nullptr) {
    document_->insert_material(material_);
  }
  if (mesh_id_ != 0 && document_->mesh(mesh_id_) == nullptr) {
    MeshAsset asset{};
    asset.id = mesh_id_;
    asset.name = name_;
    asset.cpu = model_.mesh;
    document_->insert_mesh(std::move(asset));
    document_->bind_work_lod(mesh_id_);
  }
  document_->scene().insert_node(node_);
  document_->recompute_scene();
  document_->mark_dirty();
}

}  // namespace tamias
