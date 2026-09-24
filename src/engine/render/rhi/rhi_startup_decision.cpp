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
    decision.why = "safe mode requested (--safe-mode)";
    return decision;
  }
  if (input.policy.force_backend.has_value()) {
    decision.candidates = {*input.policy.force_backend};
    decision.why = std::string("policy forces ") + to_string(*input.policy.force_backend);
    return decision;
  }
  if (input.cli_backend.has_value()) {
    decision.candidates = {*input.cli_backend};
    decision.why = std::string("command line forces ") + to_string(*input.cli_backend);
    return decision;
  }
  if (input.last_good_backend.has_value() && input.last_good_trusted) {
    // 上次是这个后端跑通的：先试它，另一个留作兜底。
    decision.candidates = {*input.last_good_backend, other_backend(*input.last_good_backend)};
    decision.why = std::string("last known good: ") + to_string(*input.last_good_backend);
    return decision;
  }
  decision.candidates = {GraphicsBackend::Vulkan, GraphicsBackend::OpenGL};
  decision.why = input.last_good_backend.has_value()
                     ? "last session did not finish cleanly; probing from scratch"
                     : "default order: Vulkan then OpenGL";
  return decision;
}

}  // namespace tamias
