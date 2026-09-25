#pragma once

#include "engine/base/result.h"
#include "plugin/host_api.h"
#include "plugin/host_pick_point.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace tamias {

class CsharpRuntime {
 public:
  CsharpRuntime() = default;
  ~CsharpRuntime();

  CsharpRuntime(const CsharpRuntime&) = delete;
  CsharpRuntime& operator=(const CsharpRuntime&) = delete;

  Result<void> start(const std::filesystem::path& managed_dir,
                     std::string_view extension_roots, HostApi* api);
  Result<void> invoke(const std::string& command_id);
  // 重扫扩展目录、重载内容真的变了的那些；返回给人看的摘要（没变化就是空串）。
  [[nodiscard]] Result<std::string> reload();
  // 控制台：把一段 C# 片段交给脚本引擎求值。
  // 返回 Ok(结果文本) 或 Err(错误文本)——两种情况都有文本要显示。
  [[nodiscard]] Result<std::string> evaluate(std::string_view code);
  // 文件监视要盯的根（约定根 + loader 用 LoadExtension 登记进来的），换行分隔。
  // 老版本 Tamias.Host 没这个导出，那时返回空串——额外根没人盯，其余照常。
  [[nodiscard]] Result<std::string> extension_roots();
  Result<void> complete_point_input(std::uint64_t request_id,
                                    const std::vector<HostPickPoint>& points, bool cancelled);
  void shutdown();
  [[nodiscard]] bool started() const { return invoke_ != nullptr; }

 private:
  using InitFn = int (*)(HostApi* api, const char* plugins_dir);
  using InvokeFn = int (*)(const char* command_id);
  using ReloadFn = int (*)(char* out_utf8, std::int32_t cap);
  using EvaluateFn = int (*)(const char* code_utf8, char* out_utf8, std::int32_t cap);
  using ExtensionRootsFn = int (*)(char* out_utf8, std::int32_t cap);
  using ShutdownFn = int (*)();
  using PointInputCompletedFn =
      int (*)(std::uint64_t request_id, const HostPickPoint* points, std::int32_t count,
              std::int32_t status);

  InitFn init_ = nullptr;
  InvokeFn invoke_ = nullptr;
  ReloadFn reload_ = nullptr;
  EvaluateFn evaluate_ = nullptr;
  ExtensionRootsFn extension_roots_fn_ = nullptr;
  ShutdownFn shutdown_fn_ = nullptr;
  PointInputCompletedFn point_input_completed_ = nullptr;
  void* hostfxr_lib_ = nullptr;
  void* hostfxr_handle_ = nullptr;
};

}  // namespace tamias
