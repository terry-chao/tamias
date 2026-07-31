#pragma once

#include "modeling/shape_ops.h"

namespace tamias {

#if defined(TAMIAS_HAS_OCCT)

void register_occt_shape_ops();

[[nodiscard]] bool occt_supports_extension(const std::filesystem::path& path);

// Test helper: tessellate a 10x10x10 box via OCCT.
[[nodiscard]] Result<MeshCpu> tessellate_occt_box_for_tests();

#else

inline void register_occt_shape_ops() {}

inline bool occt_supports_extension(const std::filesystem::path&) { return false; }

#endif

}  // namespace tamias
