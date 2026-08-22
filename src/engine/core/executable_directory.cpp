#include "executable_directory.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace tamias {

std::filesystem::path executable_directory() {
#ifdef __EMSCRIPTEN__
  return std::filesystem::path("/");
#elif defined(_WIN32)
  std::wstring buf(32768, L'\0');
  const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
  if (n == 0 || n >= buf.size()) {
    return std::filesystem::current_path();
  }
  buf.resize(n);
  return std::filesystem::path(buf).parent_path();
#else
  char buf[4096];
  const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (n <= 0) {
    return std::filesystem::current_path();
  }
  buf[n] = '\0';
  return std::filesystem::path(buf).parent_path();
#endif
}

}  // namespace tamias
