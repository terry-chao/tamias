#pragma once

#include <filesystem>

namespace tamias {

// 启动标记：开窗口**之前**写，界面起来后清掉。
// 下次启动时标记还在，说明上次死在启动期（很可能是驱动把进程搞崩了）→ 安全模式。
// 这是挡「建得出设备但一提交就崩」这类问题的正解，比另起一个探测进程便宜得多。
class StartupGuard {
 public:
  StartupGuard();

  [[nodiscard]] bool previous_startup_incomplete() const { return previous_incomplete_; }
  // 写标记（在建设备之前调用）。
  void begin();
  // 清标记（界面首帧之后调用）。
  void mark_started_ok();

 private:
  std::filesystem::path marker_path_;
  bool previous_incomplete_ = false;
};

}  // namespace tamias
