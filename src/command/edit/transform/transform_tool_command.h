#pragma once

#include "command/core/command.h"
#include "engine/document/document.h"
#include "engine/math/math.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace tamias {

// 交互式变换工具：视口里点两下（旋转三下）就完成一次移动 / 复制 / 旋转 / 镜像。
//
//   Move   ：点基点 → 点目标点（位移 = 两点之差）
//   Copy   ：同上，但生成副本
//   Rotate ：点基点 → 点参照方向 → 点目标方向（转角 = 两者夹角）
//   Mirror ：点镜像轴的两端
//
// 自己不做实际编辑：凑齐点以后构造对应的内核命令（TransformEntitiesCommand /
// CopyEntitiesCommand / MirrorEntitiesCommand）执行，undo/redo 直接转交给它——
// 所以「一条操作 = 一条撤销记录」这条规矩在交互路径上也成立。
//
// 交互点是水平面上的点（和画墙 / 放柱一样落在工作面上），所以拖出来的是水平位移；
// 竖直方向或精确数值走脚本接口（move_entities 的 delta 参数）。
class TransformToolCommand final : public Command {
 public:
  enum class Mode { Move, Copy, Rotate, Mirror };

  TransformToolCommand(Document& document, Mode mode, std::vector<std::uint64_t> entity_ids);

  [[nodiscard]] bool interactive() const override { return true; }
  [[nodiscard]] Result<bool> on_point(Vec3 point) override;
  [[nodiscard]] bool has_start() const override { return !points_.empty(); }
  [[nodiscard]] Vec3 start() const override { return points_.empty() ? Vec3{} : points_.front(); }
  // 交互点落在选中构件所在的水平面上（不会因为构件在楼上就点出一段莫名其妙的竖直位移）。
  [[nodiscard]] float work_plane_y() const override { return work_plane_y_; }
  [[nodiscard]] std::vector<Vec3> preview_polyline(Vec3 cursor) const override;
  [[nodiscard]] std::vector<Vec3> preview_points(Vec3 cursor) const override;
  [[nodiscard]] CommandArgs echo_args() const override;

  [[nodiscard]] Result<void> execute() override;
  void undo() override;
  void redo() override;

 private:
  [[nodiscard]] int required_points() const;
  // 当前光标位置对应的刚体摆放（镜像不是刚体，单独处理，这里返回 identity）。
  [[nodiscard]] Result<Mat4> placement_for(Vec3 cursor) const;
  [[nodiscard]] Result<std::unique_ptr<Command>> build() const;

  Document* document_ = nullptr;
  Mode mode_ = Mode::Move;
  std::vector<std::uint64_t> entity_ids_;
  std::vector<Vec3> points_;
  std::unique_ptr<Command> inner_;
  float work_plane_y_ = 0.f;
};

}  // namespace tamias
