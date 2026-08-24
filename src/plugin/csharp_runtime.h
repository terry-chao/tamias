#pragma once

#include "engine/core/result.h"
#include "plugin/host_api.h"

#include <filesystem>
#include <string>

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
  [[nodiscard]] bool started() const { return invoke_ != nullptr; }

 private:
  using InitFn = int (*)(HostApi* api, const char* plugins_dir);
  using InvokeFn = int (*)(const char* command_id);

  InitFn init_ = nullptr;
  InvokeFn invoke_ = nullptr;
  void* hostfxr_lib_ = nullptr;
  void* hostfxr_handle_ = nullptr;
};

}  // namespace tamias
