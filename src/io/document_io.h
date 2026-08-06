#pragma once

#include "core/result.h"
#include "document/document.h"
#include "math/math.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace tamias {

// Persisted view mode; numeric values match RenderMode in render_runtime.h.
enum class ViewRenderMode : std::uint32_t {
  Wireframe = 0,
  Shaded = 1,
  Realistic = 2,
};

struct ViewportState {
  Vec3 target{};
  float distance = 5.f;
  float yaw = 0.785398163f;
  float pitch = 0.35f;
  float fovy = 0.8f;
  float znear = 0.05f;
  float zfar = 500.f;
  ViewRenderMode render_mode = ViewRenderMode::Shaded;
};

struct LoadedDocument {
  Document document;
  ViewportState viewport{};
  bool has_viewport = false;
};

// In-memory document body (no file wrapper / no viewport). Used by undo history.
Result<std::vector<std::uint8_t>> serialize_document(const Document& document);
Result<Document> deserialize_document(std::span<const std::uint8_t> bytes);

Result<void> save_document(const std::filesystem::path& path, const Document& document,
                           const ViewportState& viewport);
Result<LoadedDocument> load_document(const std::filesystem::path& path);

[[nodiscard]] bool is_tdoc_document_path(const std::filesystem::path& path);

}  // namespace tamias
