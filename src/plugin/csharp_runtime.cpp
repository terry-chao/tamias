#include "plugin/csharp_runtime.h"

#include "engine/core/log.h"

#include <filesystem>
#include <string>

#ifdef TAMIAS_HAS_NETHOST
#include <coreclr_delegates.h>
#include <hostfxr.h>
#include <nethost.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#endif

namespace tamias {
namespace {

#ifdef TAMIAS_HAS_NETHOST

#ifdef _WIN32
using LibraryHandle = HMODULE;

LibraryHandle load_library(const char_t* path) { return LoadLibraryW(path); }

void* get_export(LibraryHandle lib, const char* name) {
  return reinterpret_cast<void*>(GetProcAddress(lib, name));
}

std::wstring to_native(const std::filesystem::path& path) { return path.wstring(); }
#else
using LibraryHandle = void*;

LibraryHandle load_library(const char_t* path) { return dlopen(path, RTLD_LAZY | RTLD_LOCAL); }

void* get_export(LibraryHandle lib, const char* name) { return dlsym(lib, name); }

std::string to_native(const std::filesystem::path& path) { return path.string(); }
#endif

#endif

}  // namespace

CsharpRuntime::~CsharpRuntime() {
#ifdef TAMIAS_HAS_NETHOST
  if (hostfxr_handle_ != nullptr && hostfxr_lib_ != nullptr) {
    auto close = reinterpret_cast<hostfxr_close_fn>(
        get_export(static_cast<LibraryHandle>(hostfxr_lib_), "hostfxr_close"));
    if (close != nullptr) {
      close(static_cast<hostfxr_handle>(hostfxr_handle_));
    }
    hostfxr_handle_ = nullptr;
  }
#endif
}

Result<void> CsharpRuntime::start(const std::filesystem::path& managed_dir,
                                 const std::filesystem::path& plugins_dir, HostApi* api) {
  if (invoke_ != nullptr) {
    return {};
  }
#ifndef TAMIAS_HAS_NETHOST
  (void)managed_dir;
  (void)plugins_dir;
  (void)api;
  return Err("C# plugin host was not built (nethost not found)");
#else
  const auto config = managed_dir / "Tamias.Host.runtimeconfig.json";
  const auto assembly = managed_dir / "Tamias.Host.dll";
  if (!std::filesystem::exists(config) || !std::filesystem::exists(assembly)) {
    return Err("C# host not found next to the executable (expected " + assembly.string() + ")");
  }

  char_t hostfxr_path[1024];
  size_t hostfxr_path_size = sizeof(hostfxr_path) / sizeof(char_t);
  if (get_hostfxr_path(hostfxr_path, &hostfxr_path_size, nullptr) != 0) {
    return Err("get_hostfxr_path failed; install the .NET runtime");
  }

  LibraryHandle lib = load_library(hostfxr_path);
  if (lib == nullptr) {
    return Err("failed to load hostfxr");
  }
  hostfxr_lib_ = lib;

  auto init = reinterpret_cast<hostfxr_initialize_for_runtime_config_fn>(
      get_export(lib, "hostfxr_initialize_for_runtime_config"));
  auto get_delegate =
      reinterpret_cast<hostfxr_get_runtime_delegate_fn>(get_export(lib, "hostfxr_get_runtime_delegate"));
  auto close = reinterpret_cast<hostfxr_close_fn>(get_export(lib, "hostfxr_close"));
  if (init == nullptr || get_delegate == nullptr || close == nullptr) {
    return Err("hostfxr is missing required exports");
  }

  const auto config_native = to_native(config);
  hostfxr_handle handle = nullptr;
  int rc = init(config_native.c_str(), nullptr, &handle);
  if (rc != 0 || handle == nullptr) {
    return Err("hostfxr_initialize_for_runtime_config failed (" + std::to_string(rc) + ")");
  }
  hostfxr_handle_ = handle;

  void* load_fn = nullptr;
  rc = get_delegate(handle, hdt_load_assembly_and_get_function_pointer, &load_fn);
  if (rc != 0 || load_fn == nullptr) {
    return Err("hostfxr_get_runtime_delegate failed (" + std::to_string(rc) + ")");
  }
  auto load_assembly = reinterpret_cast<load_assembly_and_get_function_pointer_fn>(load_fn);

  const auto assembly_native = to_native(assembly);
  const char_t* type_name =
#ifdef _WIN32
      L"Tamias.Host.Bootstrap, Tamias.Host";
#else
      "Tamias.Host.Bootstrap, Tamias.Host";
#endif
  const char_t* init_name =
#ifdef _WIN32
      L"Initialize";
#else
      "Initialize";
#endif
  const char_t* invoke_name =
#ifdef _WIN32
      L"Invoke";
#else
      "Invoke";
#endif

  rc = load_assembly(assembly_native.c_str(), type_name, init_name, UNMANAGEDCALLERSONLY_METHOD, nullptr,
                     reinterpret_cast<void**>(&init_));
  if (rc != 0 || init_ == nullptr) {
    return Err("failed to bind Tamias.Host.Bootstrap.Initialize (" + std::to_string(rc) + ")");
  }
  rc = load_assembly(assembly_native.c_str(), type_name, invoke_name, UNMANAGEDCALLERSONLY_METHOD, nullptr,
                     reinterpret_cast<void**>(&invoke_));
  if (rc != 0 || invoke_ == nullptr) {
    return Err("failed to bind Tamias.Host.Bootstrap.Invoke (" + std::to_string(rc) + ")");
  }

  const auto plugins_utf8 = plugins_dir.string();
  const int init_rc = init_(api, plugins_utf8.c_str());
  if (init_rc != 0) {
    invoke_ = nullptr;
    init_ = nullptr;
    return Err("Tamias.Host.Initialize failed (" + std::to_string(init_rc) + ")");
  }
  return {};
#endif
}

Result<void> CsharpRuntime::invoke(const std::string& command_id) {
  if (invoke_ == nullptr) {
    return Err("C# plugin host is not loaded");
  }
  const int rc = invoke_(command_id.c_str());
  if (rc != 0) {
    return Err("plugin command '" + command_id + "' failed (" + std::to_string(rc) + ")");
  }
  return {};
}

}  // namespace tamias
