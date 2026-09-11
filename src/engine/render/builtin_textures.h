#pragma once

#include "engine/render/texture_asset.h"

#include <string_view>
#include <vector>

namespace tamias {

inline constexpr std::string_view kBuiltinDefaultAlbedo = "default.albedo";
inline constexpr std::string_view kBuiltinConcreteAlbedo = "concrete.albedo";
inline constexpr std::string_view kBuiltinSteelAlbedo = "steel.albedo";
inline constexpr std::string_view kBuiltinWoodAlbedo = "wood.albedo";
inline constexpr std::string_view kBuiltinPlasterAlbedo = "plaster.albedo";
inline constexpr std::string_view kBuiltinDefaultNormal = "default.normal";
inline constexpr std::string_view kBuiltinConcreteNormal = "concrete.normal";
inline constexpr std::string_view kBuiltinWoodNormal = "wood.normal";
inline constexpr std::string_view kBuiltinSteelNormal = "steel.normal";
inline constexpr std::string_view kBuiltinPlasterNormal = "plaster.normal";
inline constexpr std::string_view kBuiltinGlassNormal = "glass.normal";

[[nodiscard]] const std::vector<std::string_view>& builtin_texture_keys();
[[nodiscard]] TextureAsset make_builtin_texture(std::string_view key);
[[nodiscard]] std::string_view builtin_key_for_hash(std::uint64_t content_hash);
[[nodiscard]] bool hydrate_builtin_texture(TextureAsset& texture);
void stamp_builtin_key(TextureAsset& texture);

inline bool omit_builtin_pixels(const TextureAsset& texture) {
  return !texture.builtin_key.empty() && texture.generation <= 1;
}

}  // namespace tamias
