#include "plugin/plugin_point_input_session.h"

#include <utility>

namespace tamias {

Result<void> PluginPointInputSession::begin(
    PluginPointInputRequest request, PluginHost::PointInputCompletion completion) {
  if (!completion) {
    return Err("point input needs a completion callback");
  }
  if (request.request_id == 0 || request.min_points < 0 ||
      (request.max_points > 0 && request.max_points < request.min_points)) {
    return Err("invalid point input request");
  }
  cancel();
  request_ = std::move(request);
  completion_ = std::move(completion);
  points_.clear();
  return {};
}

void PluginPointInputSession::add_point(PluginPickPoint point) {
  if (!active()) {
    return;
  }
  points_.push_back(point);
  if (request_.max_points > 0 &&
      static_cast<int>(points_.size()) >= request_.max_points) {
    finish(false);
  }
}

void PluginPointInputSession::confirm() {
  if (accepts_confirm() &&
      static_cast<int>(points_.size()) >= request_.min_points) {
    finish(false);
  }
}

void PluginPointInputSession::cancel(std::uint64_t request_id) {
  if (!active() || (request_id != 0 && request_id != request_.request_id)) {
    return;
  }
  finish(true);
}

bool PluginPointInputSession::accepts_confirm() const {
  return active() && (request_.flags & PluginPointInputRequest::kAllowConfirm) != 0;
}

bool PluginPointInputSession::grid_snap() const {
  return active() && (request_.flags & PluginPointInputRequest::kGridSnap) != 0;
}

bool PluginPointInputSession::pick_entities() const {
  return active() && (request_.flags & PluginPointInputRequest::kPickEntities) != 0;
}

void PluginPointInputSession::finish(bool cancelled) {
  auto completion = std::move(completion_);
  auto points = std::move(points_);
  request_ = {};
  points_.clear();
  completion_ = {};
  completion(std::move(points), cancelled);
}

}  // namespace tamias
