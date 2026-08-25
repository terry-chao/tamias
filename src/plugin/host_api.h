#pragma once

#include <cstdint>

namespace tamias {

inline constexpr int kHostApiVersion = 4;

// C ABI for C# / native plugins. Layout must match plugin-sdk/csharp/Tamias.Api/HostApi.cs.
struct HostApi {
  std::int32_t abi_version = kHostApiVersion;
  void* context = nullptr;

  void (*log)(void* context, std::int32_t level, const char* utf8) = nullptr;
  std::int32_t (*document_name)(void* context, char* utf8, std::int32_t cap) = nullptr;
  std::int32_t (*entity_count)(void* context) = nullptr;
  std::int32_t (*entity_id_at)(void* context, std::int32_t index, std::uint64_t* out_id) = nullptr;
  std::int32_t (*entity_kind)(void* context, std::uint64_t id, char* utf8, std::int32_t cap) = nullptr;
  std::int32_t (*entity_name)(void* context, std::uint64_t id, char* utf8, std::int32_t cap) = nullptr;
  std::int32_t (*selection_count)(void* context) = nullptr;
  std::int32_t (*selection_id_at)(void* context, std::int32_t index, std::uint64_t* out_id) = nullptr;
  std::int32_t (*dispatch)(void* context, const char* command, const char* args_utf8) = nullptr;
  std::int32_t (*register_command)(void* context, const char* id, const char* title,
                                   const char* tooltip, const char* page_id,
                                   const char* group_id, const char* icon_path,
                                   std::int32_t order, std::int32_t flags) = nullptr;
  std::int32_t (*register_plugin)(void* context, const char* id, const char* title,
                                  const char* author, const char* version,
                                  const char* release_date, const char* description,
                                  const char* homepage_url, const char* icon_path,
                                  std::int32_t flags) = nullptr;
  std::int32_t (*begin_point_input)(void* context, std::uint64_t request_id,
                                    std::int32_t min_points, std::int32_t max_points,
                                    std::int32_t flags, float work_plane_y,
                                    std::int32_t preview_kind,
                                    const char* preview_curve_kind) = nullptr;
  std::int32_t (*cancel_point_input)(void* context, std::uint64_t request_id) = nullptr;
};

}  // namespace tamias
