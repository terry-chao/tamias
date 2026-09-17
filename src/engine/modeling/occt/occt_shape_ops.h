#pragma once

#include "engine/modeling/kernel/shape_ops.h"

namespace tamias {

void register_occt_shape_ops();

[[nodiscard]] bool occt_supports_extension(const std::filesystem::path& path);

}  // namespace tamias
