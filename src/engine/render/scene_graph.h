#pragma once

#include "engine/render/render_types.h"
#include "engine/render/rhi/device.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace tamias {

class RenderVisitor;
class StateCommand;

// ---------------------------------------------------------------------------
// 渲染场景图骨架（VSG 式：节点 + 访问者 + 命令图状态）
//
// 定位（见 docs/SCENE-GRAPH.md）：语义树（Scene）是层级唯一真相源；这里是
// 面向绘制的投影，draw-oriented（drawable + transform + material + 可见性）。
// 当前阶段：由 Document::render_items() 的展平结果每帧全量重建，之后再做
// 脏标记增量同步。节点只存数据，不碰 GPU 资源；录制发生在 visitor 里。
// ---------------------------------------------------------------------------

class RenderNode {
 public:
  virtual ~RenderNode() = default;
  virtual void accept(RenderVisitor& visitor) = 0;

  std::string name;    // 调试 / 将来拾取用
  bool visible = true; // 节点级可见性开关；visitor 遍历时检查
};

class GroupNode final : public RenderNode {
 public:
  void accept(RenderVisitor& visitor) override;

  std::vector<std::unique_ptr<RenderNode>> children;
  void add_child(std::unique_ptr<RenderNode> child);
  // 按指针移除直接子节点（增量同步删除子树用）。
  void remove_child(const RenderNode* child);
};

// VSG 式 Transform：对整棵子树乘一个局部矩阵。访问者维护世界矩阵栈，
// Drawable 录制时世界矩阵 = 父世界 × 本节点矩阵（自顶向下累积）。
class TransformNode final : public RenderNode {
 public:
  void accept(RenderVisitor& visitor) override;

  Mat4 matrix = Mat4::identity();
  std::vector<std::unique_ptr<RenderNode>> children;
  void add_child(std::unique_ptr<RenderNode> child);
};

// 命令图状态节点：子树绘制前，把 commands 逐个“录制”进上下文状态。
// 语义是 VSG StateCommands 式的显式命令图——状态沿遍历线性累积，子树要
// 覆盖什么就在自己的 StateGroup 里再下命令，不做 OSG StateSet 式隐式继承。
class StateGroupNode final : public RenderNode {
 public:
  void accept(RenderVisitor& visitor) override;

  std::vector<std::unique_ptr<StateCommand>> commands;
  std::vector<std::unique_ptr<RenderNode>> children;
  void add_child(std::unique_ptr<RenderNode> child);
};

// 可绘制叶子：引用网格资产 + 语义节点 id（将来拾取 / 增量同步定位用）。
class DrawableNode final : public RenderNode {
 public:
  void accept(RenderVisitor& visitor) override;

  std::uint64_t node_id = 0;
  std::uint64_t mesh_asset_id = 0;
  Aabb bounds{}; // 局部包围盒（将来视锥剔除用）
};

// ---------------------------------------------------------------------------
// StateCommands：数据 + 录制（record 到上下文，由渲染线程每帧填充）
// ---------------------------------------------------------------------------

// 录制目标上下文。渲染线程（draw_channel）构造并填充资源指针；
// 命令录制时写状态字段，Drawable 录制时消费状态字段并下发 draw。
struct SceneGraphDrawContext {
  CommandList* command_list = nullptr;
  const Mat4* view_proj = nullptr;
  const Frustum* frustum = nullptr;                  // 录制时视锥剔除（nullptr = 不剔除）
  const std::unordered_set<std::uint64_t>* hidden_nodes = nullptr;  // 按语义节点 id 隐藏
  Vec3 eye_position{};
  float mode_value = 1.f; // 0=wire / 1=shaded / 2=realistic（同 RenderMode 映射）

  PipelineState* shaded_pipeline = nullptr;
  PipelineState* wire_pipeline = nullptr;
  PipelineState* entity_line_pipeline = nullptr;
  Texture* default_texture = nullptr; // 1x1 白纹理，无贴图物体兜底

  std::unordered_map<std::uint64_t, GpuMesh>* meshes = nullptr;
  std::unordered_map<std::uint64_t, std::uint64_t>* asset_to_gpu = nullptr;
  std::unordered_map<std::uint64_t, GpuTexture>* textures = nullptr;
  std::unordered_map<std::uint64_t, std::uint64_t>* texture_asset_to_gpu = nullptr;
  bool* texture_diag_logged = nullptr; // 只打一次的贴图诊断日志

  // ---- 以下状态由 StateCommands 录制累积、Drawable 录制消费 ----
  Vec3 material_color{0.75f, 0.78f, 0.82f};
  float material_roughness = 0.6f;
  float material_metallic = 0.f;
  std::uint64_t material_albedo_texture_id = 0;
  std::uint64_t material_normal_texture_id = 0;
  bool selected = false;
  bool lines = false;
};

class StateCommand {
 public:
  virtual ~StateCommand() = default;
  virtual void record(SceneGraphDrawContext& ctx) = 0;
  virtual std::unique_ptr<StateCommand> clone() const = 0;
};

// 材质命令：base_color / roughness / metallic / 贴图引用。
// 命名避开命令层的 tamias::SetMaterialCommand（src/command），这里是渲染侧
// 的状态绑定命令，语义对应 VSG 的 Bind* 系列。
class BindMaterialCommand final : public StateCommand {
 public:
  Vec3 color{0.75f, 0.78f, 0.82f};
  float roughness = 0.6f;
  float metallic = 0.f;
  std::uint64_t albedo_texture_id = 0;
  std::uint64_t normal_texture_id = 0;

  void record(SceneGraphDrawContext& ctx) override;
  std::unique_ptr<StateCommand> clone() const override;
};

// 选中命令：驱动高亮（push constants 里 light_dir_selected.w）。
class SetSelectedCommand final : public StateCommand {
 public:
  bool selected = false;

  void record(SceneGraphDrawContext& ctx) override;
  std::unique_ptr<StateCommand> clone() const override;
};

// 线条命令：LineList pipeline + mode 3（无光照线条）。
class SetLinesCommand final : public StateCommand {
 public:
  bool lines = false;

  void record(SceneGraphDrawContext& ctx) override;
  std::unique_ptr<StateCommand> clone() const override;
};

// ---------------------------------------------------------------------------
// 访问者
// ---------------------------------------------------------------------------

class RenderVisitor {
 public:
  virtual ~RenderVisitor() = default;

  virtual void apply(GroupNode& node);
  virtual void apply(TransformNode& node);
  virtual void apply(StateGroupNode& node);
  virtual void apply(DrawableNode& node);

 protected:
  static void traverse(GroupNode& node, RenderVisitor& visitor);
  static void traverse(TransformNode& node, RenderVisitor& visitor);
  static void traverse(StateGroupNode& node, RenderVisitor& visitor);
  static void traverse(DrawableNode& node, RenderVisitor& visitor);
};

// RecordCommands：沿树遍历，维护世界矩阵栈；命中 StateGroup 先录制命令，
// 命中 Drawable 解析 GPU 资源并下发 draw。等价于把 draw_channel 里原来的
// “for items 循环”拆成“树 + 显式状态命令”的录制。
class RecordCommands final : public RenderVisitor {
 public:
  explicit RecordCommands(SceneGraphDrawContext& ctx) : ctx_(ctx) {}

  void apply(GroupNode& node) override;
  void apply(TransformNode& node) override;
  void apply(StateGroupNode& node) override;
  void apply(DrawableNode& node) override;

 private:
  SceneGraphDrawContext& ctx_;
  std::vector<Mat4> matrix_stack_{Mat4::identity()};
};

// 由展平结果（SceneDrawItem 列表）每帧全量构建场景图。
// 当前阶段每个 item 包成 Transform(world) → StateGroup(材质/选中/线条) → Drawable；
// item.transform 目前直接是烘好的 world matrix，父链（局部矩阵）等增量同步时引入。
// 传入 node_index（可选）时同时填充 node_id → 该 item 的 Transform 包装节点，
// 供增量同步按 id 定位/删除。
std::unique_ptr<RenderNode> build_scene_graph(
    const std::vector<SceneDrawItem>& items,
    std::unordered_map<std::uint64_t, TransformNode*>* node_index = nullptr);

// 增量同步：只更新/插入/删除 `dirty_ids` 对应的子树，其余节点保持原样。
// - id 在 items 中存在且已在树里 → 就地更新 world matrix / StateGroup / Drawable；
// - id 在 items 中存在但不在树里 → 追加新子树；
// - id 不在 items 里（已删除）→ 移除对应子树。
// 调用方保证 generation 已变化且 dirty_ids 非空（空列表时走全量重建兜底）。
void update_scene_graph(GroupNode& root,
                        std::unordered_map<std::uint64_t, TransformNode*>& node_index,
                        const std::vector<SceneDrawItem>& items,
                        const std::vector<std::uint64_t>& dirty_ids);

}  // namespace tamias
