#include "rhi_selection/startup.h"

namespace tamias {

RhiStartupOutcome run_rhi_startup(const RhiStartupEnvironment& environment,
                                  const BackendDeviceFactory& create_device) {
  RhiStartupInput input{};
  input.cli_backend = environment.cli_backend;
  input.cli_safe_mode = environment.cli_safe_mode;
  input.policy = environment.policy;
  input.preferred_backend = environment.preferred_backend;
  input.last_good_backend = environment.last_good_backend;
  input.last_good_trusted = environment.last_good_trusted;
  input.previous_startup_incomplete = environment.previous_startup_incomplete;
  input.previous_startup_backend = environment.previous_startup_backend;

  RhiStartupOutcome outcome;
  outcome.decision = decide_rhi_startup(input);

  RhiProbeOptions options{};
  options.candidates = outcome.decision.candidates;
  options.enable_validation = environment.enable_validation;
  options.depth = environment.depth;
  options.blocklist = environment.blocklist;
  options.blocklist_override = outcome.decision.blocklist_override;
  outcome.report = probe_rhi(options, create_device);
  return outcome;
}

}  // namespace tamias
