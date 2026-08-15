#pragma once

#include <cstdint>
#include <utility>

namespace tamias {

struct NativeWindowHandle {
  void* hwnd = nullptr;      // HWND on Win32
  void* display = nullptr;   // Display* on X11
  std::uint64_t window = 0;  // Window on X11 / xid

  [[nodiscard]] bool valid() const noexcept {
#if defined(_WIN32)
    return hwnd != nullptr;
#else
    return display != nullptr && window != 0;
#endif
  }
};

}  // namespace tamias
