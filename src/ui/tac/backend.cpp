#include "ui/tac/backend.h"

#include <map>
#include <string>

namespace tac {
namespace {

// 函数内静态：首次使用时才构造，避开跨翻译单元的静态初始化顺序问题。
std::map<std::string, BackendFactory, std::less<>>& registry() {
  static std::map<std::string, BackendFactory, std::less<>> table;
  return table;
}

}  // namespace

void register_backend(std::string_view name, BackendFactory factory) {
  if (name.empty() || factory == nullptr) {
    return;
  }
  registry()[std::string(name)] = factory;
}

std::unique_ptr<UiBackend> create_backend(std::string_view name) {
  const auto it = registry().find(name);
  if (it == registry().end()) {
    return nullptr;
  }
  return it->second();
}

std::vector<std::string> backend_names() {
  std::vector<std::string> names;
  names.reserve(registry().size());
  for (const auto& [name, factory] : registry()) {
    (void)factory;
    names.push_back(name);
  }
  return names;
}

std::unique_ptr<UiBackend> create_backend_or_fallback(std::string_view preferred,
                                                      std::string_view fallback,
                                                      std::string* used) {
  if (auto backend = create_backend(preferred)) {
    if (used != nullptr) {
      *used = std::string(preferred);
    }
    return backend;
  }
  if (auto backend = create_backend(fallback)) {
    if (used != nullptr) {
      *used = std::string(fallback);
    }
    return backend;
  }
  if (used != nullptr) {
    used->clear();
  }
  return nullptr;
}

}  // namespace tac
