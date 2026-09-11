#include "engine/document/texture_library.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace tamias {

void TextureLibrary::ensure_hash(TextureAsset& asset) const {
  if (asset.content_hash == 0) {
    asset.content_hash = texture_content_hash(asset);
  }
}

void TextureLibrary::unregister_hash(const TextureAsset& asset) {
  if (asset.content_hash == 0) {
    return;
  }
  const auto it = by_hash_.find(asset.content_hash);
  if (it != by_hash_.end() && it->second == asset.id) {
    by_hash_.erase(it);
  }
}

TextureAsset& TextureLibrary::add(TextureAsset asset) {
  asset.id = next_id_++;
  ensure_hash(asset);
  if (asset.generation == 0) {
    asset.generation = 1;
  }
  auto& stored = assets_[asset.id];
  stored = std::move(asset);
  by_hash_[stored.content_hash] = stored.id;
  return stored;
}

TextureAsset& TextureLibrary::insert(TextureAsset asset) {
  if (asset.id == 0) {
    asset.id = next_id_++;
  } else {
    next_id_ = std::max(next_id_, asset.id + 1);
  }
  if (auto it = assets_.find(asset.id); it != assets_.end()) {
    unregister_hash(it->second);
  }
  ensure_hash(asset);
  if (asset.generation == 0) {
    asset.generation = 1;
  }
  auto& stored = assets_[asset.id];
  stored = std::move(asset);
  by_hash_[stored.content_hash] = stored.id;
  return stored;
}

TextureAsset& TextureLibrary::import(TextureAsset asset) {
  ensure_hash(asset);
  if (auto hit = by_hash_.find(asset.content_hash); hit != by_hash_.end()) {
    if (auto found = assets_.find(hit->second); found != assets_.end()) {
      TextureAsset& existing = found->second;
      if (existing.name.empty() && !asset.name.empty()) {
        existing.name = std::move(asset.name);
      }
      if (existing.source_path.empty() && !asset.source_path.empty()) {
        existing.source_path = std::move(asset.source_path);
      }
      if (existing.usage == TextureUsage::Unknown && asset.usage != TextureUsage::Unknown) {
        existing.usage = asset.usage;
      }
      return existing;
    }
  }
  return add(std::move(asset));
}

Result<void> TextureLibrary::replace(std::uint64_t id, TextureAsset incoming) {
  auto it = assets_.find(id);
  if (it == assets_.end()) {
    return Err("TextureLibrary: texture not found");
  }
  TextureAsset& stored = it->second;
  unregister_hash(stored);
  if (!incoming.name.empty()) {
    stored.name = std::move(incoming.name);
  }
  if (!incoming.source_path.empty()) {
    stored.source_path = std::move(incoming.source_path);
  }
  if (incoming.usage != TextureUsage::Unknown) {
    stored.usage = incoming.usage;
  }
  stored.width = incoming.width;
  stored.height = incoming.height;
  stored.srgb = incoming.srgb;
  stored.rgba = std::move(incoming.rgba);
  stored.builtin_key.clear();
  stored.content_hash = 0;
  ensure_hash(stored);
  stored.generation += 1;
  by_hash_[stored.content_hash] = stored.id;
  return {};
}

bool TextureLibrary::remove(std::uint64_t id) {
  auto it = assets_.find(id);
  if (it == assets_.end()) {
    return false;
  }
  unregister_hash(it->second);
  assets_.erase(it);
  return true;
}

void TextureLibrary::replace_all(std::unordered_map<std::uint64_t, TextureAsset> assets) {
  assets_.clear();
  by_hash_.clear();
  next_id_ = 1;
  for (auto& [id, asset] : assets) {
    asset.id = id;
    insert(std::move(asset));
  }
}

void TextureLibrary::clear() {
  assets_.clear();
  by_hash_.clear();
  next_id_ = 1;
}

TextureAsset* TextureLibrary::find(std::uint64_t id) {
  auto it = assets_.find(id);
  return it == assets_.end() ? nullptr : &it->second;
}

const TextureAsset* TextureLibrary::find(std::uint64_t id) const {
  auto it = assets_.find(id);
  return it == assets_.end() ? nullptr : &it->second;
}

const TextureAsset* TextureLibrary::find_by_hash(std::uint64_t hash) const {
  auto it = by_hash_.find(hash);
  if (it == by_hash_.end()) {
    return nullptr;
  }
  return find(it->second);
}

std::uint32_t TextureLibrary::ref_count(
    std::uint64_t id, const std::unordered_map<std::uint64_t, Material>& materials) const {
  if (id == 0) {
    return 0;
  }
  std::uint32_t n = 0;
  for (const auto& [unused, material] : materials) {
    (void)unused;
    if (material.albedo_texture_id == id || material.normal_texture_id == id ||
        material.orm_texture_id == id) {
      ++n;
    }
  }
  return n;
}

std::uint32_t TextureLibrary::remove_unused(
    const std::unordered_map<std::uint64_t, Material>& materials) {
  std::vector<std::uint64_t> drop;
  drop.reserve(assets_.size());
  for (const auto& [id, unused] : assets_) {
    (void)unused;
    if (ref_count(id, materials) == 0) {
      drop.push_back(id);
    }
  }
  for (const std::uint64_t id : drop) {
    remove(id);
  }
  return static_cast<std::uint32_t>(drop.size());
}

}  // namespace tamias
