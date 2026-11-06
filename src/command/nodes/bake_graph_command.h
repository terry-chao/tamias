#pragma once

#include "command/core/command.h"
#include "engine/document/document.h"
#include "engine/graphics/mesh.h"

#include <cstdint>
#include <vector>

namespace tamias {

// 烘焙节点图：把「预览开着的节点 + output 节点」的求值结果固化成文档里的
// 普通网格节点（同 import_mesh 的哑几何路径：无 Entity、无特征树、不可参数化）。
// 整条烘焙是一条撤销记录；参数化烘焙（编译回 FeatureModel）是 P3 的事。
class BakeGraphCommand final : public Command {
 public:
  // node_ids 为空 = 烘全部（预览开着的 + output 节点）；否则只烘指定的。
  BakeGraphCommand(Document& document, std::vector<std::uint64_t> node_ids = {});

  [[nodiscard]] Result<void> execute() override;
  void undo() override;
  void redo() override;

  // 烘出来的场景节点 id（执行后有效，供选中等）。
  [[nodiscard]] const std::vector<std::uint64_t>& baked_node_ids() const {
    return baked_node_ids_;
  }

 private:
  struct Baked {
    SceneNode node;       // 带原 id，redo 原样放回
    std::uint64_t mesh_id = 0;
    MeshAsset asset;      // undo 删网格后 redo 放回用（保留原 id / 内容）
  };

  Document* document_ = nullptr;
  std::vector<std::uint64_t> node_ids_;  // 空 = 全部
  std::vector<Baked> baked_;
  std::vector<std::uint64_t> baked_node_ids_;
  bool executed_ = false;
};

}  // namespace tamias
