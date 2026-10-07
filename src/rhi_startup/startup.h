#pragma once

#include "rhi_startup/blocklist.h"
#include "rhi_startup/decision.h"
#include "rhi_startup/device.h"
#include "rhi_startup/probe.h"

#include <optional>

namespace tamias {

// 一次启动选择需要的全部输入（都是宿主读到的原始事实：CLI、策略、设置、上次结果）。
// 组件只负责判断，不碰文件、不碰设置系统——那些是宿主的活。
struct RhiStartupEnvironment {
  std::optional<GraphicsBackend> cli_backend;  // --gpu-backend=X
  bool cli_safe_mode = false;                  // --safe-mode
  RhiPolicy policy{};
  std::optional<GraphicsBackend> preferred_backend;  // 用户设置里选的
  std::optional<GraphicsBackend> last_good_backend;  // 上次跑通的后端
  bool last_good_trusted = true;   // 上次是干净退出
  bool previous_startup_incomplete = false;  // 上次死在启动期 → 强制安全模式
  const RhiBlocklist* blocklist = nullptr;   // 可空
  // 探测深度：正常启动 A 档（快），体检 / 自检走 B / C 档。
  RhiProbeDepth depth = RhiProbeDepth::Device;
  bool enable_validation = false;
};

struct RhiStartupOutcome {
  RhiStartupDecision decision;
  RhiProbeReport report;
  [[nodiscard]] std::optional<GraphicsBackend> chosen() const { return report.chosen; }
};

// 决策 + 探测一步到位：排候选顺序，按顺序试，第一个通过的即为本次后端。
// 块名单在这里逐后端生效（它按 GPU 身份匹配，而身份要建出设备才知道）。
// 结果是否该记成「上次可用」看 decision.remember_result —— 安全模式 / 策略强制 /
// 命令行指定的会话不该记，否则一次崩溃循环会把这台机器永久降级。
[[nodiscard]] RhiStartupOutcome run_rhi_startup(const RhiStartupEnvironment& environment,
                                                const BackendDeviceFactory& create_device);

}  // namespace tamias
