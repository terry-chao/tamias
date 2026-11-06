#pragma once

#include "command/core/command.h"
#include "engine/document/document.h"
#include "engine/graphics/mesh.h"

#include <cstdint>

namespace tamias {

// 删除「没有实体的场景节点」——烘焙出来的网格、导入的哑网格都属于这一类。
//
// 普通构件走 DeleteEntityCommand；这些节点没有 Entity，以前在
// DocumentViewport::delete_selected() 里被 `entity(id) == nullptr` 直接跳过，
// 结果就是选中了也删不掉（烘焙出来的实体尤其明显）。
class DeleteSceneNodeCommand final : public Command {
 public:
  DeleteSceneNodeCommand(Document& document, std::uint64_t node_id);

  [[nodiscard]] Result<void> execute() override;
  void undo() override;
  void redo() override;

  [[nodiscard]] std::uint64_t node_id() const { return node_id_; }
  [[nodiscard]] std::uint64_t mesh_id() const { return node_.mesh_asset_id; }

 private:
  void remove_now();

  Document* document_ = nullptr;
  std::uint64_t node_id_ = 0;
  SceneNode node_{};   // 带原 id / 名字 / 变换，undo 原样放回
  MeshAsset mesh_{};   // 网格内容快照（可能被别的节点共用，放回前先查在不在）
  bool has_mesh_ = false;
  bool executed_ = false;
};

}  // namespace tamias
