#pragma once

#include "engine/modeling/shape_ops.h"

namespace tamias {

void register_occt_shape_ops();

[[nodiscard]] bool occt_supports_extension(const std::filesystem::path& path);

// Test helper: tessellate a 10x10x10 box via OCCT.
[[nodiscard]] Result<MeshCpu> tessellate_occt_box_for_tests();

}  // namespace tamias
