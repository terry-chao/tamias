#pragma once

#include "graphics/mesh.h"

#include <QImage>
#include <QString>

namespace tamias {

// Soft-shaded orthographic preview of a mesh for recent-file cards.
[[nodiscard]] QImage render_mesh_thumbnail(const MeshCpu& mesh, int width = 320, int height = 180);

// Writes PNG under the app thumbnail cache; returns absolute path or empty on failure.
[[nodiscard]] QString save_mesh_thumbnail(const QString& source_path, const QImage& image);

}  // namespace tamias
