#pragma once

#include "rhi_selection/backend.h"

#include <filesystem>
#include <optional>

namespace tamias {

// 启动标记：开窗口**之前**写，界面起来后清掉。
// 下次启动时标记还在，说明上次死在启动期（很可能是驱动把进程搞崩了）→ 安全模式。
// 这是挡「建得出设备但一提交就崩」这类问题的正解，比另起一个探测进程便宜得多。
//
// 标记里**记着当时用的是哪个后端**：安全模式要避开的就是那个（见 decide_rhi_startup）。
// 没有它，安全模式只能猜，而猜错就是「每次开机都崩」的自锁（docs/RHI-TRIAGE.md §7.1）。
class StartupGuard {
 public:
  StartupGuard();

  [[nodiscard]] bool previous_startup_incomplete() const { return previous_incomplete_; }
  // 上次死在启动期时用的后端；旧版标记没这一行 / 不认识的值 → 空。
  [[nodiscard]] std::optional<GraphicsBackend> previous_startup_backend() const {
    return previous_backend_;
  }
  // 写标记（在建设备之前调用）。
  void begin(GraphicsBackend backend);
  // 清标记（界面首帧之后调用）。
  void mark_started_ok();

 private:
  std::filesystem::path marker_path_;
  bool previous_incomplete_ = false;
  std::optional<GraphicsBackend> previous_backend_;
};

}  // namespace tamias
