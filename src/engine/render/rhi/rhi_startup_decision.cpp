#include "engine/render/rhi/rhi_startup_decision.h"

namespace tamias {
namespace {

GraphicsBackend other_backend(GraphicsBackend backend) {
  return backend == GraphicsBackend::Vulkan ? GraphicsBackend::OpenGL : GraphicsBackend::Vulkan;
}

}  // namespace

RhiStartupDecision decide_rhi_startup(const RhiStartupInput& input) {
  RhiStartupDecision decision;
  decision.policy_locked = input.policy.lock;
  decision.blocklist_override = input.policy.override_blocklist;

  if (input.cli_safe_mode) {
    // 安全模式：只试最保守的那条路，并且关掉校验层（校验层自己也要加载驱动代码）。
    decision.candidates = {GraphicsBackend::OpenGL};
    decision.safe_mode = true;
    decision.remember_result = false;
    decision.why = "safe mode requested (--safe-mode)";
    return decision;
  }
  if (input.previous_startup_incomplete) {
    // 上次启动死在启动期（很可能就是建设备 / 首帧把进程搞崩了）：这次别再用同一块驱动赌，
    // 直接退到最保守的后端，把"每次开机都崩"变成"至少还能开起来，把日志交出来"。
    decision.candidates = {GraphicsBackend::OpenGL};
    decision.safe_mode = true;
    decision.remember_result = false;
    decision.why = "previous startup did not finish; forcing safe mode (OpenGL)";
    return decision;
  }
  if (input.policy.force_backend.has_value()) {
    decision.candidates = {*input.policy.force_backend};
    decision.remember_result = false;  // 是策略定的，不是这台机器"自然"选中它
    decision.why = std::string("policy forces ") + to_string(*input.policy.force_backend);
    return decision;
  }
  if (input.cli_backend.has_value()) {
    decision.candidates = {*input.cli_backend};
    decision.remember_result = false;  // 调试用的指定，不该覆盖记忆
    decision.why = std::string("command line forces ") + to_string(*input.cli_backend);
    return decision;
  }

  // 用户偏好排第一（设置里选了 OpenGL 就该先用 OpenGL），上次可用排第二（它是实测证据），
  // 两者之外的那个后端兜底。同一个后端只留一次。
  const GraphicsBackend preferred = input.preferred_backend.value_or(GraphicsBackend::Vulkan);
  const auto push_unique = [&decision](GraphicsBackend backend) {
    for (const GraphicsBackend existing : decision.candidates) {
      if (existing == backend) {
        return;
      }
    }
    decision.candidates.push_back(backend);
  };
  push_unique(preferred);
  if (input.last_good_backend.has_value() && input.last_good_trusted) {
    push_unique(*input.last_good_backend);
  }
  push_unique(other_backend(preferred));
  if (input.last_good_backend.has_value() && input.last_good_trusted) {
    decision.why = std::string("preference ") + to_string(preferred) + ", last known good " +
                   to_string(*input.last_good_backend);
  } else if (input.last_good_backend.has_value()) {
    decision.why = std::string("preference ") + to_string(preferred) +
                   " (last session did not finish cleanly; not trusting the remembered backend)";
  } else {
    decision.why = std::string("preference ") + to_string(preferred);
  }
  return decision;
}

}  // namespace tamias
