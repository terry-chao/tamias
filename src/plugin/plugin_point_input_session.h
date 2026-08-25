#pragma once

#include "engine/core/result.h"
#include "plugin/plugin_host.h"
#include "plugin/plugin_pick_point.h"
#include "plugin/plugin_point_input_request.h"

#include <cstdint>
#include <vector>

namespace tamias {

class PluginPointInputSession {
 public:
  Result<void> begin(PluginPointInputRequest request,
                     PluginHost::PointInputCompletion completion);
  void add_point(PluginPickPoint point);
  void confirm();
  void cancel(std::uint64_t request_id = 0);

  [[nodiscard]] bool active() const { return completion_ != nullptr; }
  [[nodiscard]] bool accepts_confirm() const;
  [[nodiscard]] bool grid_snap() const;
  [[nodiscard]] bool pick_entities() const;
  [[nodiscard]] float work_plane_y() const { return request_.work_plane_y; }
  [[nodiscard]] int preview_kind() const { return request_.preview_kind; }
  [[nodiscard]] const std::string& preview_curve_kind() const {
    return request_.preview_curve_kind;
  }
  [[nodiscard]] const std::vector<PluginPickPoint>& points() const { return points_; }

 private:
  void finish(bool cancelled);

  PluginPointInputRequest request_{};
  std::vector<PluginPickPoint> points_;
  PluginHost::PointInputCompletion completion_;
};

}  // namespace tamias
