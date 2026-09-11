#pragma once

#include "engine/core/result.h"
#include "engine/render/material.h"
#include "engine/render/texture_asset.h"

#include <algorithm>
#include <cstdint>
#include <unordered_map>

namespace tamias {

// 文档级纹理库：稳定 id、内容指纹去重、replace 递增 generation。
// GPU 缓存不在这里；渲染线程按 (asset_id, generation) 上传。
class TextureLibrary {
 public:
  // 始终分配新 id（预设贴图、测试夹具）。相同像素也会留下第二份。
  TextureAsset& add(TextureAsset asset);
  // 保留 id（load / undo）。覆盖已有 id 时更新指纹索引。
  TextureAsset& insert(TextureAsset asset);
  // 按内容指纹复用；命中则补全空的 name / path / usage。
  TextureAsset& import(TextureAsset asset);
  // 原地换像素，保留 id。引用该 id 的材质全部看到新图。
  Result<void> replace(std::uint64_t id, TextureAsset incoming);
  bool remove(std::uint64_t id);
  // 打开渲染快照时整表替换，丢掉构造函数种下的默认 512² 纹理。
  void replace_all(std::unordered_map<std::uint64_t, TextureAsset> assets);
  void clear();

  [[nodiscard]] TextureAsset* find(std::uint64_t id);
  [[nodiscard]] const TextureAsset* find(std::uint64_t id) const;
  [[nodiscard]] const TextureAsset* find_by_hash(std::uint64_t hash) const;

  [[nodiscard]] std::uint32_t ref_count(
      std::uint64_t id, const std::unordered_map<std::uint64_t, Material>& materials) const;
  // 未被任何材质槽引用的贴图。不要在换材质后自动调用：撤销还需要旧像素。
  std::uint32_t remove_unused(const std::unordered_map<std::uint64_t, Material>& materials);

  [[nodiscard]] const std::unordered_map<std::uint64_t, TextureAsset>& assets() const {
    return assets_;
  }
  [[nodiscard]] std::uint64_t next_id() const { return next_id_; }
  void set_next_id(std::uint64_t id) { next_id_ = std::max<std::uint64_t>(1, id); }

 private:
  void ensure_hash(TextureAsset& asset) const;
  void unregister_hash(const TextureAsset& asset);

  std::unordered_map<std::uint64_t, TextureAsset> assets_;
  std::unordered_map<std::uint64_t, std::uint64_t> by_hash_;  // content_hash -> id
  std::uint64_t next_id_ = 1;
};

}  // namespace tamias
