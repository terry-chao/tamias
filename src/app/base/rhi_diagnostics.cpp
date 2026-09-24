#include "app/base/rhi_diagnostics.h"

#include "app/base/log_buffer.h"
#include "tamias_version.h"

#include <sstream>
#include <utility>
#include <vector>

namespace tamias {

RhiDiagnostics& RhiDiagnostics::instance() {
  static RhiDiagnostics diagnostics;
  return diagnostics;
}

void RhiDiagnostics::set_snapshot(RhiDiagnosticsSnapshot snapshot) {
  snapshot_ = std::move(snapshot);
}

std::string build_diagnostics_report() {
  std::ostringstream out;
  out << "Tamias " << TAMIAS_VERSION_FULL << "\n";
  out << "OS: " << rhi_os_name(rhi_current_os()) << "\n\n";

  const std::optional<RhiDiagnosticsSnapshot>& snapshot = RhiDiagnostics::instance().snapshot();
  if (!snapshot.has_value()) {
    out << "No startup probe recorded (this build did not run the RHI probe).\n";
  } else {
    out << "backend   : "
        << (snapshot->report.chosen.has_value() ? to_string(*snapshot->report.chosen) : "(none)")
        << "\n";
    out << "preference: " << snapshot->preference
        << (snapshot->degraded ? "  (fell back this session)" : "") << "\n";
    out << "startup   : " << snapshot->startup_reason << "\n";
    out << "safe mode : " << (snapshot->safe_mode ? "yes" : "no") << "\n";
    out << "policy    : " << (snapshot->has_policy ? "present" : "none")
        << (snapshot->policy_locked ? "  (backend locked)" : "") << "\n";
    out << "blocklist : v" << snapshot->blocklist_version << ", " << snapshot->blocklist_entries
        << " entries\n\n";
    out << snapshot->report.to_text();
  }

  const std::vector<std::string> lines = recent_log_lines();
  out << "\nRecent log (" << lines.size() << " lines)\n";
  for (const std::string& line : lines) {
    out << line << "\n";
  }
  return out.str();
}

}  // namespace tamias
