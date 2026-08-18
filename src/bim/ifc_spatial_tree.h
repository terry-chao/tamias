#pragma once

#include "engine/core/result.h"

#include <filesystem>
#include <string>

namespace tamias {

// Parse an IFC-SPF file with IfcOpenShell IfcParse and format the spatial
// decomposition tree (IfcProject → site/building/storey → contained products).
// Geometry (IfcGeom) is not used.
[[nodiscard]] Result<std::string> format_ifc_spatial_tree(const std::filesystem::path& path);

}  // namespace tamias
