#pragma once

#include <expected>
#include <string>
#include <utility>

namespace tamias {

template <typename T>
using Result = std::expected<T, std::string>;

inline std::unexpected<std::string> Err(std::string message) {
  return std::unexpected<std::string>(std::move(message));
}

}  // namespace tamias
