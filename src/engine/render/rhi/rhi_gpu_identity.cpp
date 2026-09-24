#include "engine/render/rhi/rhi_gpu_identity.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace tamias {
namespace {

std::string lowercase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

// FNV-1a 混入：分隔符是必须的，不然 "ab"+"c" 和 "a"+"bc" 会撞。
void mix(std::uint64_t& hash, std::string_view value) {
  constexpr std::uint64_t kPrime = 1099511628211ull;
  for (char c : value) {
    hash ^= static_cast<std::uint8_t>(c);
    hash *= kPrime;
  }
  hash ^= 0x1Fu;
  hash *= kPrime;
}

}  // namespace

bool rhi_is_remote_session() {
#if defined(_WIN32)
  return GetSystemMetrics(SM_REMOTESESSION) != 0;
#else
  return false;  // Linux/macOS 要判 XDG_SESSION_TYPE / SSH_CONNECTION，桌面场景先不猜
#endif
}

std::uint32_t rhi_vendor_id_from_name(std::string_view name) {
  const std::string lower = lowercase(std::string(name));
  if (lower.find("nvidia") != std::string::npos) {
    return 0x10DE;
  }
  if (lower.find("intel") != std::string::npos) {
    return 0x8086;
  }
  if (lower.find("amd") != std::string::npos || lower.find("ati") != std::string::npos ||
      lower.find("radeon") != std::string::npos) {
    return 0x1002;
  }
  if (lower.find("apple") != std::string::npos) {
    return 0x106B;
  }
  if (lower.find("qualcomm") != std::string::npos) {
    return 0x5143;
  }
  if (lower.find("arm") != std::string::npos || lower.find("mali") != std::string::npos) {
    return 0x13B5;
  }
  return 0;
}

std::string rhi_gpu_fingerprint(const RhiGpuIdentity& identity) {
  std::uint64_t hash = 14695981039346656037ull;
  mix(hash, rhi_os_name(identity.os));
  mix(hash, identity.driver_name);
  mix(hash, identity.adapter_name);
  mix(hash, std::to_string(identity.vendor_id));
  mix(hash, std::to_string(identity.device_id));
  mix(hash, std::to_string(identity.api_version));
  mix(hash, identity.driver_version);
  mix(hash, identity.remote_session ? "remote" : "local");
  mix(hash, identity.software_renderer ? "sw" : "hw");
  char buffer[24];
  std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(hash));
  return buffer;
}

}  // namespace tamias
