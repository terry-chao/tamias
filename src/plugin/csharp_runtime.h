#pragma once

#include "engine/core/result.h"
#include "plugin/host_api.h"
#include "plugin/host_pick_point.h"

#include <filesystem>
#include <string>
#include <vector>

namespace tamias {

class CsharpRuntime {
 public:
  CsharpRuntime() = default;
  ~CsharpRuntime();

  CsharpRuntime(const CsharpRuntime&) = delete;
  CsharpRuntime& operator=(const CsharpRuntime&) = delete;

  Result<void> start(const std::filesystem::path& managed_dir,
                   const std::filesystem::path& plugins_dir, HostApi* api);
  Result<void> invoke(const std::string& command_id);
  Result<void> complete_point_input(std::uint64_t request_id,
                                    const std::vector<HostPickPoint>& points, bool cancelled);
  [[nodiscard]] bool started() const { return invoke_ != nullptr; }

 private:
  using InitFn = int (*)(HostApi* api, const char* plugins_dir);
  using InvokeFn = int (*)(const char* command_id);
  using PointInputCompletedFn =
      int (*)(std::uint64_t request_id, const HostPickPoint* points, std::int32_t count,
              std::int32_t status);

  InitFn init_ = nullptr;
  InvokeFn invoke_ = nullptr;
  PointInputCompletedFn point_input_completed_ = nullptr;
  void* hostfxr_lib_ = nullptr;
  void* hostfxr_handle_ = nullptr;
};

}  // namespace tamias
