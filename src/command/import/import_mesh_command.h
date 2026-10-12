#pragma once

#include "command/core/command.h"
#include "engine/document/document.h"
#include "engine/graphics/mesh.h"
#include "engine/io/imported_model.h"
#include "engine/render/resource/texture_asset.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>

namespace tamias {

// 把一份网格作为**导入网格**放进当前文档：贴图 → 材质 → 网格资产 → 场景节点，
// 整条是一次撤销记录（Ctrl+Z 一次全退，redo 按原 id 放回）。
//
// 两个刻意的边界：
//  - **命令层不碰 Qt**：贴图必须是已解码的 RGBA8（app 层用 QImage 解出来再传进来，
//    见 DocumentViewport::import_mesh_file）。走路径构造时只有几何，内嵌贴图不落地。
//  - 导入网格是**三角汤**：没有 Entity、不进特征树、不可参数化。它是参考几何 /
//    占位体，不是 BIM 构件（见 docs/PLAN-MESHY.md）。
class ImportMeshCommand final : public Command {
 public:
  // 三个贴图槽位对齐 TextureUsage（albedo / normal / orm）；rgba 为空 = 没这张图。
  struct Textures {
    TextureAsset albedo;
    TextureAsset normal;
    TextureAsset orm;
  };

  // 数据入口：几何 / 材质标量 + 已解码贴图。app 层与 Meshy 客户端走这条。
  ImportMeshCommand(Document& document, ImportedModel model, std::string name,
                    Mat4 transform = Mat4::identity(), Textures textures = {});
  // 路径入口：execute() 时才读盘，所以「文件读不动」能作为命令错误回给调用方，
  // 而不是在工厂里变成「未知命令」。只有几何（内嵌贴图需要 app 层解码）。
  ImportMeshCommand(Document& document, std::filesystem::path source, std::string name = {},
                    Mat4 transform = Mat4::identity());

  [[nodiscard]] Result<void> execute() override;
  void undo() override;
  void redo() override;

  // 落地结果（0 = 还没执行，或已撤销）。
  [[nodiscard]] std::uint64_t node_id() const { return node_.id; }
  [[nodiscard]] std::uint64_t mesh_id() const { return mesh_id_; }
  [[nodiscard]] std::uint64_t material_id() const { return material_.id; }

 private:
  struct Slot {
    TextureAsset asset;  // 带分配好的 id，redo 要原样放回
  };

  // 按内容指纹入库（同一张图不存两份）；undo 侧靠 ref_count 判断能不能删。
  void import_texture_slot(Slot& slot, TextureAsset incoming);
  void drop_texture_slot(const Slot& slot);
  [[nodiscard]] bool mesh_referenced_by_other_node() const;
  [[nodiscard]] Result<void> load_from_source();

  Document* document_ = nullptr;
  std::filesystem::path source_;  // 非空 = 从路径导入
  ImportedModel model_;
  std::string name_;
  Mat4 transform_ = Mat4::identity();
  Slot albedo_;
  Slot normal_;
  Slot orm_;
  Material material_{};
  SceneNode node_{};
  std::uint64_t mesh_id_ = 0;
  bool executed_ = false;
};

}  // namespace tamias
