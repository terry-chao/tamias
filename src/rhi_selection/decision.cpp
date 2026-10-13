#include "rhi_selection/decision.h"

#include <algorithm>
#include <string>
#include <utility>

namespace tamias {
namespace {

GraphicsBackend other_backend(GraphicsBackend backend) {
  return backend == GraphicsBackend::Vulkan ? GraphicsBackend::OpenGL : GraphicsBackend::Vulkan;
}

void push_unique(std::vector<GraphicsBackend>& order, GraphicsBackend backend) {
  if (std::find(order.begin(), order.end(), backend) == order.end()) {
    order.push_back(backend);
  }
}

// 自然顺序：用户在设置里选的 → 上次实测可用（干净退出才作数）→ 另一个后端。
std::vector<GraphicsBackend> natural_order(const RhiStartupInput& input) {
  std::vector<GraphicsBackend> order;
  const GraphicsBackend preferred = input.preferred_backend.value_or(GraphicsBackend::Vulkan);
  push_unique(order, preferred);
  if (input.last_good_backend.has_value() && input.last_good_trusted) {
    push_unique(order, *input.last_good_backend);
  }
  push_unique(order, other_backend(preferred));
  return order;
}

// 把某个后端挪到末尾：安全模式里「上次开着它死在启动期」的那个不再排前面。
void demote(std::vector<GraphicsBackend>& order, GraphicsBackend backend) {
  const auto it = std::find(order.begin(), order.end(), backend);
  if (it == order.end()) {
    return;
  }
  order.erase(it);
  order.push_back(backend);
}

std::string natural_why(const RhiStartupInput& input) {
  const GraphicsBackend preferred = input.preferred_backend.value_or(GraphicsBackend::Vulkan);
  if (input.last_good_backend.has_value() && input.last_good_trusted) {
    return std::string("preference ") + to_string(preferred) + ", last known good " +
           to_string(*input.last_good_backend);
  }
  if (input.last_good_backend.has_value()) {
    return std::string("preference ") + to_string(preferred) +
           " (last session did not finish cleanly; not trusting the remembered backend)";
  }
  return std::string("preference ") + to_string(preferred);
}

}  // namespace

RhiStartupDecision decide_rhi_startup(const RhiStartupInput& input) {
  RhiStartupDecision decision;
  decision.policy_locked = input.policy.lock;
  decision.blocklist_override = input.policy.override_blocklist;

  // 安全模式：候选顺序照自然顺序，但把「上次死在启动期的那个后端」排到最后，并且不写记忆。
  // 以前这里硬编码 {OpenGL}——在坏驱动上正好是最危险的那条路（见 docs/RHI-TRIAGE.md §7）。
  const auto safe_mode = [&input, &decision](std::string why) {
    decision.candidates = natural_order(input);
    decision.safe_mode = true;
    decision.remember_result = false;
    if (input.previous_startup_backend.has_value()) {
      demote(decision.candidates, *input.previous_startup_backend);
      why += std::string("; trying ") + to_string(*input.previous_startup_backend) +
             " last (it was in use when the last startup died)";
    } else {
      why += "; natural order, result not remembered";
    }
    decision.why = std::move(why);
    return decision;
  };

  if (input.cli_safe_mode) {
    // 最高优先：人明确要求「别再赌」。关校验层（校验层自己也要加载驱动代码）。
    return safe_mode("safe mode requested (--safe-mode)");
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
  if (input.previous_startup_incomplete) {
    // 上次启动死在启动期（很可能就是建设备 / 首帧把进程搞崩了）：这次别再用同一个后端赌。
    // 命令行 / 策略在上面已经拦掉了：显式指定是人在现场给的证据，自动判断不该压过它。
    return safe_mode("previous startup did not finish; safe mode");
  }

  // 用户偏好排第一（设置里选了 OpenGL 就该先用 OpenGL），上次可用排第二（它是实测证据），
  // 两者之外的那个后端兜底。同一个后端只留一次。
  decision.candidates = natural_order(input);
  decision.why = natural_why(input);
  return decision;
}

}  // namespace tamias
