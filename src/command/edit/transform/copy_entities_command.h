#pragma once

#include "bim/relation.h"
#include "command/core/command.h"
#include "engine/document/document.h"
#include "engine/math/math.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace tamias {

// 复制选中的实体：每个 placement 生成一份完整副本，一条撤销。
// 阵列就是「先算出一串 placement，再走同一条复制路径」（见 register_commands.cpp）。
//
// 复制的语义：
//   - 门窗（宿主开口）连同宿主一起复制时，副本挂在**新的**墙上，新墙上照样开洞；
//     只复制门窗时，副本留在原来那面墙的对应位置；
//   - 草图与基础体平移摆放，族实体反写 Location（楼层归属不变，跟着源件走）；
//   - 几何相同的副本走 document 的网格 intern，不额外占显存（见 docs/INSTANCING.md）。
class CopyEntitiesCommand final : public Command {
 public:
  CopyEntitiesCommand(Document& document, std::vector<std::uint64_t> source_ids,
                      std::vector<Mat4> placements);

  [[nodiscard]] Result<void> execute() override;
  void undo() override;
  void redo() override;

  // 复制出来的新实体 id（视口用它把选择移到副本上）。
  [[nodiscard]] const std::vector<std::uint64_t>& created_ids() const { return created_ids_; }

 private:
  struct Source {
    std::unique_ptr<Entity> entity;  // 源件快照：redo 从它重新克隆，不依赖文档当前状态
    MeshAsset mesh;
    std::uint64_t host_id = 0;  // 源件是门窗时的宿主
    bool is_opening = false;
  };
  struct Made {
    std::unique_ptr<Entity> entity;
    MeshAsset mesh;
  };

  void capture_sources();
  Result<void> create();
  void destroy();

  Document* document_ = nullptr;
  std::vector<std::uint64_t> source_ids_;
  std::vector<Mat4> placements_;
  std::vector<Source> sources_;
  std::vector<Made> made_;
  std::vector<Relation> made_relations_;
  std::vector<std::uint64_t> created_ids_;
  std::vector<std::uint64_t> affected_hosts_;  // 收到新开口的墙（含没被复制的墙）
  bool captured_ = false;
};

}  // namespace tamias
