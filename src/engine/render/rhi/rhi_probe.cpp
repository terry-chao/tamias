#include "engine/render/rhi/rhi_probe.h"

#include <algorithm>
#include <sstream>

namespace tamias {
namespace {

std::string json_escape(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (char c : text) {
    if (c == '\\' || c == '"') {
      out.push_back('\\');
    }
    out.push_back(c);
  }
  return out;
}

// 探测用的身份补全：后端只管「这块 GPU 是谁」，平台环境由这里统一补。
void complete_identity(RhiGpuIdentity& identity) {
  if (identity.os == RhiOs::Unknown) {
    identity.os = rhi_current_os();
  }
  identity.remote_session = rhi_is_remote_session();
  if (identity.vendor_id == 0 && !identity.driver_name.empty()) {
    identity.vendor_id = rhi_vendor_id_from_name(identity.driver_name);
  }
}

}  // namespace

// C 档：离屏画 1×1、读回、核对颜色。返回空字符串 = 通过，否则是失败原因。
std::string pixel_check(RHIDevice& device) {
  auto target = device.create_offscreen_swap_chain(1, 1);
  if (!target.has_value()) {
    return "offscreen target: " + target.error();
  }
  auto commands = device.create_command_list();
  if (!commands.has_value()) {
    return "offscreen command list: " + commands.error();
  }
  const float reference[4] = {0.f, 1.f, 0.f, 1.f};  // 纯绿：读回来最好判
  if (auto began = device.begin_frame(**target); !began) {
    return "offscreen begin_frame: " + began.error();
  }
  (*commands)->begin();
  (*commands)->begin_render_pass(**target, reference, 1.f);
  (*commands)->end_render_pass();
  (*commands)->end();
  if (auto executed = device.execute(**commands); !executed) {
    return "offscreen execute: " + executed.error();
  }
  if (auto ended = device.end_frame(**target); !ended) {
    return "offscreen end_frame: " + ended.error();
  }
  std::vector<std::uint8_t> pixels;
  if (auto read = (*target)->read_back_rgba(pixels); !read) {
    return "offscreen read back: " + read.error();
  }
  if (pixels.size() != 4) {
    return "offscreen read back returned " + std::to_string(pixels.size()) + " bytes";
  }
  if (pixels[0] > 40 || pixels[1] < 200 || pixels[2] > 40) {
    return "offscreen pixel check got unexpected color";
  }
  return {};
}

std::string RhiProbeReport::to_text() const {
  std::ostringstream out;
  out << "RHI probe\n";
  out << "  chosen  : " << (chosen.has_value() ? to_string(*chosen) : "(none)") << "\n";
  out << "  summary : " << summary << "\n";
  for (const RhiProbeResult& attempt : attempts) {
    out << "  - " << to_string(attempt.backend) << ": " << (attempt.ok ? "ok" : "failed") << "\n";
    out << "      reason : " << (attempt.reason.empty() ? "-" : attempt.reason) << "\n";
    if (!attempt.matched_entry.empty()) {
      out << "      blocklist: " << attempt.matched_entry << "\n";
    }
    if (!attempt.identity.valid()) {
      continue;
    }
    out << "      adapter: " << attempt.identity.adapter_name << "\n";
    if (!attempt.identity.driver_name.empty() || !attempt.identity.driver_version.empty()) {
      out << "      driver : " << attempt.identity.driver_name;
      if (!attempt.identity.driver_version.empty()) {
        out << " " << attempt.identity.driver_version;
      }
      out << "\n";
    }
    out << "      ids    : vendor=0x" << std::hex << attempt.identity.vendor_id << std::dec
        << " device=0x" << std::hex << attempt.identity.device_id << std::dec
        << " api=" << attempt.identity.api_version << "\n";
    out << "      flags  : os=" << rhi_os_name(attempt.identity.os)
        << " remote=" << (attempt.identity.remote_session ? "yes" : "no")
        << " software=" << (attempt.identity.software_renderer ? "yes" : "no") << "\n";
  }
  return out.str();
}

std::string RhiProbeReport::to_json() const {
  std::ostringstream out;
  out << "{\n";
  out << "  \"chosen\": ";
  if (chosen.has_value()) {
    out << "\"" << to_string(*chosen) << "\",\n";
  } else {
    out << "null,\n";
  }
  out << "  \"summary\": \"" << json_escape(summary) << "\",\n";
  out << "  \"attempts\": [\n";
  for (std::size_t i = 0; i < attempts.size(); ++i) {
    const RhiProbeResult& attempt = attempts[i];
    out << "    { \"backend\": \"" << to_string(attempt.backend) << "\", \"ok\": "
        << (attempt.ok ? "true" : "false") << ", \"reason\": \"" << json_escape(attempt.reason)
        << "\", \"matched_entry\": \"" << json_escape(attempt.matched_entry)
        << "\", \"adapter\": \"" << json_escape(attempt.identity.adapter_name)
        << "\", \"driver_version\": \"" << json_escape(attempt.identity.driver_version)
        << "\", \"vendor_id\": " << attempt.identity.vendor_id
        << ", \"device_id\": " << attempt.identity.device_id
        << ", \"api_version\": " << attempt.identity.api_version
        << ", \"remote\": " << (attempt.identity.remote_session ? "true" : "false")
        << ", \"software\": " << (attempt.identity.software_renderer ? "true" : "false") << " }";
    out << (i + 1 < attempts.size() ? ",\n" : "\n");
  }
  out << "  ]\n}\n";
  return out.str();
}

RhiProbeReport probe_rhi(const RhiProbeOptions& options, const RhiDeviceFactory& create_device) {
  RhiProbeReport report;
  std::vector<GraphicsBackend> queue = options.candidates;
  std::vector<GraphicsBackend> tried;
  const auto already_tried = [&tried](GraphicsBackend backend) {
    return std::find(tried.begin(), tried.end(), backend) != tried.end();
  };

  for (std::size_t i = 0; i < queue.size(); ++i) {
    const GraphicsBackend backend = queue[i];
    if (already_tried(backend)) {
      continue;
    }
    tried.push_back(backend);

    RhiProbeResult attempt;
    attempt.backend = backend;

    DeviceCreateInfo info{};
    info.backend = backend;
    info.enable_validation = options.enable_validation;
    info.app_name = "tamias-rhi-probe";
    // 设备活不过这一轮：下一轮之前必须销毁（volk 的 device 表是进程全局的，不能并存）。
    Result<std::unique_ptr<RHIDevice>> device = create_device(info);
    if (!device.has_value()) {
      attempt.reason = device.error();
      report.attempts.push_back(std::move(attempt));
      continue;
    }

    attempt.identity = (*device)->gpu_identity();
    complete_identity(attempt.identity);

    if (options.blocklist != nullptr && !options.blocklist_override) {
      if (const RhiBlockEntry* entry = options.blocklist->match(attempt.identity)) {
        if (entry->action == RhiBlockActionKind::SkipBackend && entry->backend == backend) {
          attempt.matched_entry = entry->id;  // 只在这个条目**真的改了我们的选择**时标出来
          attempt.reason = "blocked by blocklist entry \"" + entry->id + "\": " + entry->why;
          (*device)->wait_idle();
          report.attempts.push_back(std::move(attempt));
          continue;
        }
        if (entry->action == RhiBlockActionKind::ForceBackend && entry->backend != backend) {
          attempt.matched_entry = entry->id;
          attempt.reason = "redirected to " + std::string(to_string(entry->backend)) +
                           " by blocklist entry \"" + entry->id + "\": " + entry->why;
          (*device)->wait_idle();
          report.attempts.push_back(std::move(attempt));
          queue.push_back(entry->backend);  // 接着试它；已在 tried 里就跳过
          continue;
        }
      }
    }

    if (options.depth == RhiProbeDepth::Submit) {
      if (auto submitted = (*device)->submit_noop(); !submitted) {
        attempt.reason = submitted.error();
        (*device)->wait_idle();
        report.attempts.push_back(std::move(attempt));
        continue;
      }
    }
    if (options.depth == RhiProbeDepth::Pixel) {
      if (std::string failure = pixel_check(**device); !failure.empty()) {
        attempt.reason = std::move(failure);
        (*device)->wait_idle();
        report.attempts.push_back(std::move(attempt));
        continue;
      }
    }
    (*device)->wait_idle();
    attempt.ok = true;
    attempt.reason = "ready";
    report.attempts.push_back(std::move(attempt));
    report.chosen = backend;
    break;
  }

  std::ostringstream summary;
  if (report.chosen.has_value()) {
    summary << "RHI: using " << to_string(*report.chosen);
  } else {
    summary << "RHI: no usable graphics backend";
  }
  for (const RhiProbeResult& attempt : report.attempts) {
    if (attempt.reason.empty() || attempt.reason == "ready") {
      continue;
    }
    summary << "; " << to_string(attempt.backend) << " -> " << attempt.reason;
  }
  report.summary = summary.str();
  return report;
}

}  // namespace tamias
