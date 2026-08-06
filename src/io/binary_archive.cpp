#include "binary_archive.h"

// Header-only style helpers live in the header; this TU ensures the library
// has a compile unit for the archive API and keeps linker happy on MSVC.
namespace tamias {
namespace {
[[maybe_unused]] constexpr std::uint32_t kArchiveMarker = 0x53414D54u;  // 'TMAS' LE
}
}  // namespace tamias
