#pragma once

#include "rhi_selection/backend.h"
#include "rhi_selection/blocklist.h"

#include <optional>
#include <string>
#include <vector>

namespace tamias {

// 启动决策的输入：CLI 与设置由壳读取，engine 只做判断（可单测）。
struct RhiStartupInput {
  std::optional<GraphicsBackend> cli_backend;        // --gpu-backend=X
  bool cli_safe_mode = false;                        // --safe-mode
  RhiPolicy policy{};                                // IT 下发的策略
  std::optional<GraphicsBackend> last_good_backend;  // 上次启动成功用的那个
  bool last_good_trusted = true;  // 上次是干净退出（不是被安全模式标记打断）
  // 上次启动没走完（启动标记还在）：按设计强制安全模式——别拿同一块驱动再赌一次。
  bool previous_startup_incomplete = false;
  // 启动标记里记着的那个后端：上次死在启动期时正在用的就是它。安全模式把它排到最后。
  // 旧版标记没这一行 → 空（这时只能按偏好/记忆排，见 decide_rhi_startup）。
  std::optional<GraphicsBackend> previous_startup_backend;
  std::optional<GraphicsBackend> preferred_backend;  // 用户在设置里选的
};

struct RhiStartupDecision {
  std::vector<GraphicsBackend> candidates;  // 交给探测，按顺序试
  bool safe_mode = false;
  bool policy_locked = false;
  bool blocklist_override = false;
  // 这次结果要不要记成「上次可用」。安全模式 / 策略强制 / 命令行指定的会话不该记：
  // 否则一次崩溃循环（被迫走 OpenGL）会把这台机器永久降级到 OpenGL。
  bool remember_result = true;
  std::string why;  // 一句话解释为什么这么排（进日志）
};

// 顺序：--safe-mode > 策略 > 命令行 > 启动标记（自动安全模式）> 用户偏好（+ 上次可用兜底）
//       > 默认（Vulkan → OpenGL）。
// 命令行排在启动标记**之前**是刻意的：标记是自动判断，命令行是人在现场给的证据。
// 反过来的话，标记一留下连 --gpu-backend=vulkan 都救不回来（见 docs/RHI-TRIAGE.md §7.1）。
// 安全模式不再是「硬编码 OpenGL」：候选顺序按自然顺序来，只把标记里那个后端排到最后
// （在坏驱动上硬走 OpenGL 恰恰是最危险的一条路，见 docs/RHI-TRIAGE.md §7）。
// 块名单**不在这里**过滤——它按 GPU 身份匹配，而身份要建出设备才知道（见 probe_rhi）。
[[nodiscard]] RhiStartupDecision decide_rhi_startup(const RhiStartupInput& input);

}  // namespace tamias
