#pragma once

#include <cstdint>
#include <list>
#include <optional>
#include <unordered_map>
#include <vector>

namespace tamias {

// GPU mesh LRU with a byte budget. Keys are MeshAsset ids.
class ResidentCache {
 public:
  explicit ResidentCache(std::uint64_t budget_bytes = 2ull * 1024ull * 1024ull * 1024ull)
      : budget_bytes_(budget_bytes) {}

  void set_budget(std::uint64_t bytes) { budget_bytes_ = bytes; }
  [[nodiscard]] std::uint64_t budget() const { return budget_bytes_; }
  [[nodiscard]] std::uint64_t bytes() const { return used_bytes_; }
  [[nodiscard]] std::uint32_t mesh_count() const {
    return static_cast<std::uint32_t>(entries_.size());
  }

  void insert(std::uint64_t asset_id, std::uint64_t gpu_mesh_id, std::uint64_t bytes) {
    if (asset_id == 0 || gpu_mesh_id == 0) {
      return;
    }
    erase(asset_id);
    lru_.push_front(asset_id);
    entries_[asset_id] = Entry{gpu_mesh_id, bytes, lru_.begin()};
    used_bytes_ += bytes;
  }

  [[nodiscard]] std::optional<std::uint64_t> lookup(std::uint64_t asset_id) {
    const auto it = entries_.find(asset_id);
    if (it == entries_.end()) {
      return std::nullopt;
    }
    lru_.splice(lru_.begin(), lru_, it->second.lru_it);
    it->second.lru_it = lru_.begin();
    return it->second.gpu_mesh_id;
  }

  [[nodiscard]] bool contains(std::uint64_t asset_id) const {
    return entries_.count(asset_id) != 0;
  }

  void erase(std::uint64_t asset_id) {
    const auto it = entries_.find(asset_id);
    if (it == entries_.end()) {
      return;
    }
    used_bytes_ -= it->second.bytes;
    lru_.erase(it->second.lru_it);
    entries_.erase(it);
  }

  // Oldest assets whose eviction brings used_bytes_ under budget. Does not erase;
  // caller destroys GPU resources then erase().
  [[nodiscard]] std::vector<std::uint64_t> over_budget_assets() const {
    std::vector<std::uint64_t> out;
    if (used_bytes_ <= budget_bytes_) {
      return out;
    }
    std::uint64_t used = used_bytes_;
    for (auto it = lru_.rbegin(); it != lru_.rend() && used > budget_bytes_; ++it) {
      const auto e = entries_.find(*it);
      if (e == entries_.end()) {
        continue;
      }
      out.push_back(*it);
      used -= e->second.bytes;
    }
    return out;
  }

  void clear() {
    entries_.clear();
    lru_.clear();
    used_bytes_ = 0;
  }

 private:
  struct Entry {
    std::uint64_t gpu_mesh_id = 0;
    std::uint64_t bytes = 0;
    std::list<std::uint64_t>::iterator lru_it{};
  };

  std::uint64_t budget_bytes_ = 0;
  std::uint64_t used_bytes_ = 0;
  std::list<std::uint64_t> lru_;
  std::unordered_map<std::uint64_t, Entry> entries_;
};

}  // namespace tamias
