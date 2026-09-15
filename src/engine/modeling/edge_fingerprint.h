#pragma once

#include "engine/base/result.h"
#include "engine/math/math.h"
#include "engine/modeling/feature.h"
#include "engine/modeling/kernel/kernel.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace tamias {

// 边的几何指纹（拓扑命名「索引 + 指纹」的那一半，见 docs/FEATURE-TREE-EVALUATOR.md）。
//
// 圆角 / 倒角要记住「倒哪条边」。只记「第 N 条」的话，上游参数一改、BRep 重算，
// 边的遍历顺序就可能变，第 N 条已经不是原来那条 → 圆角悄悄倒到别的棱上。
// 所以创建特征时把那条边的几何长相一起记下来：位置、方向、长度、相邻面法线。
// 重算时先按索引找，再用指纹核对；对不上就全量找最像的那条，还找不到就报错。
//
// 这些量存在 Feature::params 里（跟特征树一起进 .tdoc），全部是求值器内部
// 坐标一律取 Tamias Y-up 局部空间（内核边界负责换算），位置/长度按包围盒归一化，
// 所以与模型尺寸、与后端都无关——换个内核求值，指纹仍然可比。
struct EdgeFingerprint {
  Vec3 mid{};           // 归一化中点（闭合边取采样质心）：相对包围盒，各分量 0..1
  Vec3 dir{};           // 单位方向；闭合边（圆）没有方向
  bool has_dir = false;
  double length = 0.0;  // 边长 / 包围盒对角线
  Vec3 normal1{};       // 相邻两个面的法线（朝外）
  Vec3 normal2{};
  bool has_normal1 = false;
  bool has_normal2 = false;
};

// 采集 shape_feature_id 这个特征输出体的第 edge_index 条边。
// shape_feature_id = 0 表示取整棵树的输出特征。
[[nodiscard]] Result<EdgeFingerprint> capture_edge_fingerprint(const FeatureModel& model,
                                                               ModelKernel& kernel,
                                                               std::uint64_t shape_feature_id,
                                                               int edge_index);

// 过渡重载：用进程默认内核（见 linked_kernels.h）。
[[nodiscard]] Result<EdgeFingerprint> capture_edge_fingerprint(const FeatureModel& model,
                                                               std::uint64_t shape_feature_id,
                                                               int edge_index);

// 解析「倒哪条边」：索引只是首选，指纹才是依据。
//   1. 没有指纹（旧文件 / 脚本只给 edge）→ 纯索引行为；
//   2. 索引那条边的指纹对得上 → 用它（没改上游时的快路）；
//   3. 对不上 → 全量找最像的一条；
//   4. 找不到、或两条一样像 → 报错，绝不静默倒到别的棱上。
[[nodiscard]] Result<EdgeId> resolve_edge(ModelKernel& kernel, const Body& body,
                                          const Feature& feature);

// 给一个 Fillet / Chamfer 特征（重新）采集指纹：取它 inputs[0] 形状上的第 edge 条边。
// 采集失败（内核不支持 / 索引越界）就清掉指纹，退回旧的纯索引行为。
void refresh_edge_fingerprint(FeatureModel& model, std::uint64_t feature_id);
void refresh_edge_fingerprint(FeatureModel& model, ModelKernel& kernel,
                              std::uint64_t feature_id);

void write_edge_fingerprint(std::unordered_map<std::string, double>& params,
                            const EdgeFingerprint& fp);
void erase_edge_fingerprint(std::unordered_map<std::string, double>& params);
[[nodiscard]] bool has_edge_fingerprint(const std::unordered_map<std::string, double>& params);

// 指纹占用的参数名：属性面板要把这些内部参数藏起来，不暴露给用户。
[[nodiscard]] bool is_edge_fingerprint_key(const std::string& key);

}  // namespace tamias
