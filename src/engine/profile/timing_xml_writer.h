#pragma once

#include "engine/core/result.h"

#include <filesystem>
#include <string>

namespace tamias {

class TimingSession;

class TimingXmlWriter {
 public:
  [[nodiscard]] static std::string to_string(const TimingSession& session);
  [[nodiscard]] static Result<void> write_file(const TimingSession& session,
                                               const std::filesystem::path& path);
};

}  // namespace tamias
