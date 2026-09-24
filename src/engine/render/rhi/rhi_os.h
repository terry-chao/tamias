#pragma once

#include <cstdint>
#include <string_view>

namespace tamias {

// 平台。块名单要按平台区分：同一条驱动问题通常只在某一个 OS 上出现，
// 而且 Windows 和 Linux 的驱动版本号完全不是一回事。
enum class RhiOs : std::uint8_t {
  Unknown = 0,
  Windows = 1,
  Linux = 2,
  Mac = 3,
};

[[nodiscard]] inline const char* rhi_os_name(RhiOs os) {
  switch (os) {
    case RhiOs::Windows:
      return "windows";
    case RhiOs::Linux:
      return "linux";
    case RhiOs::Mac:
      return "mac";
    case RhiOs::Unknown:
      break;
  }
  return "unknown";
}

[[nodiscard]] inline RhiOs rhi_os_from_name(std::string_view name) {
  if (name == "windows" || name == "Windows" || name == "win") {
    return RhiOs::Windows;
  }
  if (name == "linux" || name == "Linux") {
    return RhiOs::Linux;
  }
  if (name == "mac" || name == "macos" || name == "darwin") {
    return RhiOs::Mac;
  }
  return RhiOs::Unknown;
}

[[nodiscard]] inline RhiOs rhi_current_os() {
#if defined(_WIN32)
  return RhiOs::Windows;
#elif defined(__APPLE__)
  return RhiOs::Mac;
#elif defined(__linux__)
  return RhiOs::Linux;
#else
  return RhiOs::Unknown;
#endif
}

}  // namespace tamias
