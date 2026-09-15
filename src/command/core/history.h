#pragma once

#include "engine/core/result.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace tamias {

// Full-document binary snapshot stack (opaque blobs from serialize_document).
class DocumentHistory {
 public:
  explicit DocumentHistory(std::size_t max_depth = 64) : max_depth_(max_depth == 0 ? 1 : max_depth) {}

  [[nodiscard]] std::size_t max_depth() const { return max_depth_; }
  [[nodiscard]] bool can_undo() const { return index_ > 0; }
  [[nodiscard]] bool can_redo() const { return index_ + 1 < snapshots_.size(); }
  [[nodiscard]] std::size_t size() const { return snapshots_.size(); }
  [[nodiscard]] std::size_t index() const { return index_; }

  void clear() {
    snapshots_.clear();
    index_ = 0;
  }

  // Push a new snapshot after an edit. Drops any redo branch.
  void push_snapshot(std::vector<std::uint8_t> bytes) {
    if (index_ + 1 < snapshots_.size()) {
      snapshots_.resize(index_ + 1);
    }
    snapshots_.push_back(std::move(bytes));
    if (snapshots_.size() > max_depth_) {
      const std::size_t drop = snapshots_.size() - max_depth_;
      snapshots_.erase(snapshots_.begin(), snapshots_.begin() + static_cast<std::ptrdiff_t>(drop));
    }
    index_ = snapshots_.empty() ? 0 : snapshots_.size() - 1;
  }

  // Replace the current tip (e.g. seed baseline without growing undo).
  void reset_with(std::vector<std::uint8_t> bytes) {
    snapshots_.clear();
    snapshots_.push_back(std::move(bytes));
    index_ = 0;
  }

  Result<std::vector<std::uint8_t>> undo() {
    if (!can_undo()) {
      return Err("Nothing to undo");
    }
    --index_;
    return snapshots_[index_];
  }

  Result<std::vector<std::uint8_t>> redo() {
    if (!can_redo()) {
      return Err("Nothing to redo");
    }
    ++index_;
    return snapshots_[index_];
  }

  [[nodiscard]] const std::vector<std::uint8_t>* current() const {
    if (snapshots_.empty() || index_ >= snapshots_.size()) {
      return nullptr;
    }
    return &snapshots_[index_];
  }

 private:
  std::vector<std::vector<std::uint8_t>> snapshots_;
  std::size_t index_ = 0;
  std::size_t max_depth_ = 64;
};

}  // namespace tamias
