#pragma once

#include "engine/graphics/graphics_backend.h"
#include "engine/render/rhi/rhi_blocklist.h"

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
};

struct RhiStartupDecision {
  std::vector<GraphicsBackend> candidates;  // 交给探测，按顺序试
  bool safe_mode = false;
  bool policy_locked = false;
  bool blocklist_override = false;
  std::string why;  // 一句话解释为什么这么排（进日志）
};

// 顺序：安全模式 > 策略 > 命令行 > 上次可用 > 默认（Vulkan → OpenGL）。
// 块名单**不在这里**过滤——它按 GPU 身份匹配，而身份要建出设备才知道（见 probe_rhi）。
[[nodiscard]] RhiStartupDecision decide_rhi_startup(const RhiStartupInput& input);

}  // namespace tamias
