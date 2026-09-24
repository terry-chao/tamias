#pragma once

#include "engine/render/rhi/rhi_probe.h"

#include <cstddef>
#include <optional>
#include <string>

namespace tamias {

// 启动时那次探测的「现场记录」，留给诊断面板显示。
//
// 为什么缓存而不是面板里现探：volk 是单设备模型，进程里已经有渲染设备时再建一台
// Vulkan 设备会被守卫拒绝（rhi_probe 会如实报错）。启动那份报告才是可信的现场快照。
struct RhiDiagnosticsSnapshot {
  RhiProbeReport report;
  std::string startup_reason;  // decide_rhi_startup 给出的理由
  std::string preference;      // 用户偏好（设置里的选择）
  bool degraded = false;       // 实际用的 != 用户偏好
  bool safe_mode = false;
  bool policy_locked = false;
  bool has_policy = false;
  int blocklist_version = 0;
  std::size_t blocklist_entries = 0;
};

class RhiDiagnostics {
 public:
  static RhiDiagnostics& instance();

  void set_snapshot(RhiDiagnosticsSnapshot snapshot);
  [[nodiscard]] const std::optional<RhiDiagnosticsSnapshot>& snapshot() const {
    return snapshot_;
  }

 private:
  RhiDiagnostics() = default;
  std::optional<RhiDiagnosticsSnapshot> snapshot_;
};

// 诊断报告全文（面板显示 / 复制 / 「--diagnostics-report=file」都走它，保证三处一致）。
// 内容：后端与偏好、启动理由、安全模式、策略与块名单状态、探测报告、最近日志。
[[nodiscard]] std::string build_diagnostics_report();

}  // namespace tamias
