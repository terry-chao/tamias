#pragma once

#include <cstdint>

namespace tamias {

inline constexpr int kHostApiVersion = 1;

// C ABI for C# / native plugins. Layout must match csharp/Tamias.Api/HostApi.cs.
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
                                   const char* tooltip) = nullptr;
};

}  // namespace tamias
