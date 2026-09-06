#pragma once

#include "engine/core/result.h"
#include "engine/graphics/mesh.h"
#include "engine/io/binary_archive.h"

namespace tamias {

Result<void> write_mesh_cpu(BinaryWriter& w, const MeshCpu& mesh);
Result<void> read_mesh_cpu(BinaryReader& r, MeshCpu& mesh);

}  // namespace tamias
