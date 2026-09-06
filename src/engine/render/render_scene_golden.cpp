#include "engine/render/render_scene_golden.h"

#include "engine/core/fs_utf8.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <optional>
#include <sstream>

namespace tamias {
namespace {

constexpr std::string_view kSceneFile = "scene.trscn";
constexpr std::string_view kMetaFile = "scene.meta.json";
constexpr std::string_view kInspectFile = "scene.inspect.txt";

Result<void> write_text_file(const std::filesystem::path& path, std::string_view text) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return Err("Failed to write " + path_to_utf8(path));
  }
  out.write(text.data(), static_cast<std::streamsize>(text.size()));
  if (!out) {
    return Err("Failed to write " + path_to_utf8(path));
  }
  return {};
}

Result<std::string> read_text_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return Err("Failed to read " + path_to_utf8(path));
  }
  std::ostringstream body;
  body << in.rdbuf();
  if (!in && !in.eof()) {
    return Err("Failed to read " + path_to_utf8(path));
  }
  return body.str();
}

std::string json_escape(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    if (c == '\\' || c == '"') {
      out.push_back('\\');
    }
    out.push_back(c);
  }
  return out;
}

std::string meta_to_json(const RenderSceneGoldenMeta& meta) {
  std::ostringstream out;
  out << "{\n";
  out << "  \"name\": \"" << json_escape(meta.name) << "\",\n";
  out << "  \"digest\": \"" << json_escape(meta.digest) << "\",\n";
  out << "  \"items\": " << meta.items << ",\n";
  out << "  \"meshes\": " << meta.meshes << ",\n";
  out << "  \"textures\": " << meta.textures << ",\n";
  out << "  \"mode\": " << meta.mode << "\n";
  out << "}\n";
  return out.str();
}

std::optional<std::string> json_string_field(const std::string& body, std::string_view key) {
  const std::string pat = "\"" + std::string(key) + "\"";
  auto pos = body.find(pat);
  if (pos == std::string::npos) {
    return std::nullopt;
  }
  pos = body.find(':', pos + pat.size());
  if (pos == std::string::npos) {
    return std::nullopt;
  }
  ++pos;
  while (pos < body.size() && std::isspace(static_cast<unsigned char>(body[pos]))) {
    ++pos;
  }
  if (pos >= body.size() || body[pos] != '"') {
    return std::nullopt;
  }
  ++pos;
  std::string value;
  while (pos < body.size() && body[pos] != '"') {
    if (body[pos] == '\\' && pos + 1 < body.size()) {
      ++pos;
    }
    value.push_back(body[pos]);
    ++pos;
  }
  return value;
}

std::optional<std::uint64_t> json_u64_field(const std::string& body, std::string_view key) {
  const std::string pat = "\"" + std::string(key) + "\"";
  auto pos = body.find(pat);
  if (pos == std::string::npos) {
    return std::nullopt;
  }
  pos = body.find(':', pos + pat.size());
  if (pos == std::string::npos) {
    return std::nullopt;
  }
  ++pos;
  while (pos < body.size() && std::isspace(static_cast<unsigned char>(body[pos]))) {
    ++pos;
  }
  if (pos >= body.size() || !std::isdigit(static_cast<unsigned char>(body[pos]))) {
    return std::nullopt;
  }
  std::uint64_t v = 0;
  while (pos < body.size() && std::isdigit(static_cast<unsigned char>(body[pos]))) {
    v = v * 10u + static_cast<std::uint64_t>(body[pos] - '0');
    ++pos;
  }
  return v;
}

}  // namespace

std::filesystem::path render_scene_golden_root(const std::filesystem::path& source_dir) {
  return source_dir / "assets" / "samples" / "render";
}

bool is_render_scene_golden_slug(std::string_view slug) {
  if (slug.empty() || slug.size() > 64) {
    return false;
  }
  if (!std::isalpha(static_cast<unsigned char>(slug.front()))) {
    return false;
  }
  for (char c : slug) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (!std::isalnum(u) && c != '-' && c != '_') {
      return false;
    }
  }
  return true;
}

std::string suggest_render_scene_golden_slug(std::string_view name) {
  std::string slug;
  slug.reserve(name.size());
  for (char c : name) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (std::isalnum(u) || c == '-' || c == '_') {
      slug.push_back(c);
    }
  }
  if (slug.empty()) {
    return "scene";
  }
  if (!std::isalpha(static_cast<unsigned char>(slug.front()))) {
    slug.insert(slug.begin(), 's');
  }
  if (slug.size() > 64) {
    slug.resize(64);
  }
  return slug;
}

RenderSceneGoldenMeta make_render_scene_golden_meta(std::string name, const RenderScene& scene) {
  RenderSceneGoldenMeta meta;
  meta.name = std::move(name);
  meta.digest = render_scene_digest(scene);
  meta.items = static_cast<std::uint64_t>(scene.items.size());
  meta.meshes = static_cast<std::uint64_t>(scene.meshes.size());
  meta.textures = static_cast<std::uint64_t>(scene.textures.size());
  meta.mode = static_cast<int>(scene.view.mode);
  return meta;
}

std::vector<std::filesystem::path> list_render_scene_goldens(
    const std::filesystem::path& golden_root) {
  std::vector<std::filesystem::path> dirs;
  std::error_code ec;
  if (!std::filesystem::is_directory(golden_root, ec)) {
    return dirs;
  }
  for (const auto& entry : std::filesystem::directory_iterator(golden_root, ec)) {
    if (ec || !entry.is_directory()) {
      continue;
    }
    if (!is_render_scene_golden_slug(path_to_utf8(entry.path().filename()))) {
      continue;
    }
    const auto scene = entry.path() / kSceneFile;
    const auto meta = entry.path() / kMetaFile;
    if (std::filesystem::is_regular_file(scene, ec) && !ec &&
        std::filesystem::is_regular_file(meta, ec) && !ec) {
      dirs.push_back(entry.path());
    }
  }
  std::sort(dirs.begin(), dirs.end());
  return dirs;
}

Result<RenderSceneGoldenMeta> load_render_scene_golden_meta(const std::filesystem::path& dir) {
  auto body = read_text_file(dir / kMetaFile);
  if (!body) {
    return Err(body.error());
  }
  RenderSceneGoldenMeta meta;
  auto name = json_string_field(*body, "name");
  auto digest = json_string_field(*body, "digest");
  auto items = json_u64_field(*body, "items");
  auto meshes = json_u64_field(*body, "meshes");
  auto textures = json_u64_field(*body, "textures");
  auto mode = json_u64_field(*body, "mode");
  if (!name || !digest || !items || !meshes || !textures || !mode) {
    return Err("render golden meta missing fields: " + path_to_utf8(dir / kMetaFile));
  }
  meta.name = std::move(*name);
  meta.digest = std::move(*digest);
  meta.items = *items;
  meta.meshes = *meshes;
  meta.textures = *textures;
  meta.mode = static_cast<int>(*mode);
  return meta;
}

Result<RenderSceneGoldenMeta> refresh_render_scene_golden_sidecar(const std::filesystem::path& dir) {
  auto loaded = load_render_scene(dir / kSceneFile);
  if (!loaded) {
    return Err(loaded.error());
  }
  const std::string slug = path_to_utf8(dir.filename());
  auto meta = make_render_scene_golden_meta(slug, *loaded);
  if (auto r = write_render_scene_debug_files(dir / kInspectFile, dir / "debug", *loaded); !r) {
    return Err(r.error());
  }
  if (auto r = write_text_file(dir / kMetaFile, meta_to_json(meta)); !r) {
    return Err(r.error());
  }
  return meta;
}

Result<RenderSceneGoldenMeta> save_render_scene_golden(const std::filesystem::path& golden_root,
                                                       std::string_view slug,
                                                       const RenderScene& scene, bool overwrite) {
  if (!is_render_scene_golden_slug(slug)) {
    return Err("golden slug must start with a letter and use only A-Z, a-z, 0-9, '-' or '_'");
  }
  const auto dir = golden_root / std::string(slug);
  std::error_code ec;
  if (std::filesystem::exists(dir / kSceneFile, ec) && !overwrite) {
    return Err("golden already exists: " + path_to_utf8(dir));
  }
  std::filesystem::create_directories(dir, ec);
  if (ec) {
    return Err("failed to create " + path_to_utf8(dir) + ": " + ec.message());
  }
  const auto trscn = dir / kSceneFile;
  if (auto r = save_render_scene(trscn, scene); !r) {
    return Err(r.error());
  }
  auto meta = make_render_scene_golden_meta(std::string(slug), scene);
  if (auto r = write_render_scene_debug_files(dir / kInspectFile, dir / "debug", scene); !r) {
    return Err(r.error());
  }
  if (auto r = write_text_file(dir / kMetaFile, meta_to_json(meta)); !r) {
    return Err(r.error());
  }
  auto loaded = load_render_scene(trscn);
  if (!loaded) {
    return Err(loaded.error());
  }
  const std::string roundtrip = render_scene_digest(*loaded);
  if (roundtrip != meta.digest) {
    return Err("golden roundtrip digest mismatch: wrote " + meta.digest + " loaded " + roundtrip);
  }
  return meta;
}

}  // namespace tamias
