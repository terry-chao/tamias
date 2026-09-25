#pragma once

#include "engine/base/result.h"
#include "engine/math/math.h"

#include <cstdint>
#include <vector>

namespace tamias {

class Document;
class Entity;

// 一个实体的一次刚体变换（世界空间）：from → to。
// 移动 / 旋转共用这一份「变换列表」表示；复制用它算目标摆放。
struct EntityTransform {
  std::uint64_t id = 0;
  Mat4 from = Mat4::identity();
  Mat4 to = Mat4::identity();
};

// ===== 变换构造（纯函数，可单测）=====

// 位移。
[[nodiscard]] Mat4 translation_transform(Vec3 delta);
// 绕「过 base 的竖直轴（Y）」旋转 angle_rad；俯视逆时针为正，和 rotate_y 同向。
[[nodiscard]] Mat4 yaw_rotation_about(Vec3 base, double angle_rad);

// 竖直镜像平面：过 plane_point、沿 plane_direction（取 XZ 分量）。方向退化成 0 时
// 以 +X 为平面方向。
[[nodiscard]] Vec3 mirror_point(Vec3 point, Vec3 plane_point, Vec3 plane_direction);
[[nodiscard]] Vec3 mirror_direction(Vec3 direction, Vec3 plane_direction);

// ===== 阵列（返回**副本**的摆放，不含原件；count 含原件所以副本数是 count-1）=====

// 线性阵列：沿 direction 每 spacing 米一份。
[[nodiscard]] Result<std::vector<Mat4>> linear_array_placements(Vec3 direction, double spacing,
                                                                int count);
// 环形阵列：绕 center 的竖直轴，每份转 step_angle_deg 度。
[[nodiscard]] Result<std::vector<Mat4>> polar_array_placements(Vec3 center, double step_angle_deg,
                                                               int count);

// ===== 摆放读写 =====

// 实体当前的世界摆放。楼层分组节点不带变换（见 docs/BIM.md），所以实体自己的
// local_transform 就是世界变换。
[[nodiscard]] Mat4 entity_world_transform(const Entity& entity);

// 把实体的世界摆放写成 transform。必须是刚体（平移 + 绕 Y 旋转）：
//   - 有 Location 的族实体（墙 / 梁 / 板 / 柱 / 基础）反写 Location，造型与朝向跟着走；
//   - 其余实体（草图 / 基础体 / 门窗）直接写 local_transform。
// apply_entity_placement 只改实体自己（新建实体还没进场景时用），
// set_entity_world_transform 另把它同步到场景节点。
void apply_entity_placement(Document& document, Entity& entity, const Mat4& transform);
void set_entity_world_transform(Document& document, Entity& entity, const Mat4& transform);

// 把实体镜像到上面那个竖直平面。**镜像烘焙进几何，不写反射矩阵**：拾取与空间索引
// 都假设场景变换是刚体（见 docs/SPATIAL-INDEX.md §7、picking.cpp），写了反射矩阵
// 会同时坏掉射线、法线和三角绕序。
//
// 覆盖：族实体反射 Location 锚点；草图实体把控制点反射后写回特征树。宿主开口
// （门 / 窗）的摆放由宿主墙决定，调用方要走 relation，不该调这里。
Result<void> mirror_entity(Document& document, Entity& entity, Vec3 plane_point,
                           Vec3 plane_direction);

}  // namespace tamias
