#pragma once

#include "bim/relation.h"
#include "command/core/command.h"
#include "engine/document/document.h"
#include "engine/math/math.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace tamias {

// 整层复制：把源楼层上的构件**整体**复制到一个或多个目标楼层，一条撤销。
//
// 语义（和 CopyEntitiesCommand 同一套路，只是摆放不是「平移到位」而是「换层」）：
//   - 复制出来的都是**新实体**（新 id），源件一动不动；
//   - 门窗跟着宿主墙走：宿主也在这层，副本门窗就挂在**新的**墙上，新墙上照样开洞，
//     不会出现「副本门还挂在 1 楼原墙上」这种半截状态；
//   - 高度：按构件**相对源楼层的标高偏移**原样搬到目标层（世界标高 = 目标层标高 +
//     原偏移），所以层高不同的两层之间复制，构件在本层内的上下关系不乱；
//   - 几何相同的副本走 document 的网格 intern，不额外占显存（见 docs/INSTANCING.md）。
//
// 复制范围 = 楼层归属落在源层上的构件（读入口 entity_storey_id）：族实体认自己记的
// 楼层，没有族语义的退回 Location。没有归属的（草图 / 未归属构件 / 导入网格）不在
// 任何一层名下，自然不参与整层复制。
class CopyStoreyCommand final : public Command {
 public:
  CopyStoreyCommand(Document& document, std::uint64_t source_storey_id,
                     std::uint64_t target_storey_id);
  CopyStoreyCommand(Document& document, std::uint64_t source_storey_id,
                    std::vector<std::uint64_t> target_storey_ids);

  [[nodiscard]] Result<void> execute() override;
  void undo() override;
  void redo() override;

  // 复制出来的新实体 id（视口用它把选择移到副本上）。
  [[nodiscard]] const std::vector<std::uint64_t>& created_ids() const { return created_ids_; }

 private:
  struct Source {
    std::unique_ptr<Entity> entity;  // 源件快照：redo 从它重新克隆，不依赖文档当前状态
    MeshAsset mesh;
    std::uint64_t host_id = 0;        // 源件是门窗时的宿主
    bool is_opening = false;
    double elevation_offset = 0.0;    // 相对源楼层的标高偏移
  };
  struct Made {
    std::unique_ptr<Entity> entity;
    MeshAsset mesh;
  };

  void capture_sources();
  Result<void> create();
  Result<void> create_to_target(std::uint64_t target_storey_id);
  void destroy();
  void record_made(const Entity& entity);

  Document* document_ = nullptr;
  std::uint64_t source_storey_id_ = 0;
  std::vector<std::uint64_t> target_storey_ids_;
  std::vector<Source> sources_;
  std::vector<Made> made_;
  std::vector<Relation> made_relations_;
  std::vector<std::uint64_t> created_ids_;
  std::vector<std::uint64_t> affected_hosts_;  // 收到新开口的副本墙
  bool captured_ = false;
};

}  // namespace tamias
