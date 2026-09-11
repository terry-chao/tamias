#include "engine/document/document.h"
#include "engine/document/document_io.h"
#include "engine/document/texture_library.h"
#include "engine/render/material.h"
#include "engine/render/texture_mips.h"

#include <gtest/gtest.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace tamias {
namespace {

TextureAsset make_solid(std::uint8_t r, std::uint8_t g, std::uint8_t b, bool srgb = true) {
  TextureAsset tex;
  tex.width = 2;
  tex.height = 2;
  tex.srgb = srgb;
  tex.rgba.assign(16, 255);
  for (std::size_t i = 0; i < 4; ++i) {
    tex.rgba[i * 4 + 0] = r;
    tex.rgba[i * 4 + 1] = g;
    tex.rgba[i * 4 + 2] = b;
  }
  return tex;
}

}  // namespace

TEST(TextureLibrary, ImportDedupsByContentHash) {
  TextureLibrary lib;
  TextureAsset a = make_solid(10, 20, 30);
  a.name = "brick";
  a.source_path = "brick.png";
  a.usage = TextureUsage::Albedo;
  const std::uint64_t first = lib.import(a).id;
  TextureAsset b = make_solid(10, 20, 30);
  b.name = "brick-copy";
  b.usage = TextureUsage::Albedo;
  const TextureAsset& again = lib.import(std::move(b));
  EXPECT_EQ(again.id, first);
  EXPECT_EQ(lib.assets().size(), 1u);
  EXPECT_EQ(again.name, "brick");
  EXPECT_EQ(again.source_path, "brick.png");
}

TEST(TextureLibrary, ImportKeepsSrgbDistinct) {
  TextureLibrary lib;
  TextureAsset albedo = make_solid(10, 20, 30, true);
  albedo.usage = TextureUsage::Albedo;
  TextureAsset linear = make_solid(10, 20, 30, false);
  linear.usage = TextureUsage::Normal;
  const std::uint64_t a = lib.import(std::move(albedo)).id;
  const std::uint64_t n = lib.import(std::move(linear)).id;
  EXPECT_NE(a, n);
  EXPECT_EQ(lib.assets().size(), 2u);
}

TEST(TextureLibrary, AddDoesNotDedup) {
  TextureLibrary lib;
  lib.add(make_solid(1, 2, 3));
  lib.add(make_solid(1, 2, 3));
  EXPECT_EQ(lib.assets().size(), 2u);
}

TEST(TextureLibrary, ReplaceBumpsGenerationAndHash) {
  TextureLibrary lib;
  TextureAsset first = make_solid(1, 1, 1);
  first.name = "wall";
  const TextureAsset& stored = lib.add(std::move(first));
  const std::uint64_t id = stored.id;
  const std::uint64_t gen0 = stored.generation;
  const std::uint64_t hash0 = stored.content_hash;
  ASSERT_NE(hash0, 0u);

  TextureAsset next = make_solid(9, 8, 7);
  auto replaced = lib.replace(id, std::move(next));
  ASSERT_TRUE(replaced) << replaced.error();
  const TextureAsset* updated = lib.find(id);
  ASSERT_NE(updated, nullptr);
  EXPECT_EQ(updated->name, "wall");
  EXPECT_EQ(updated->generation, gen0 + 1);
  EXPECT_NE(updated->content_hash, hash0);
  EXPECT_EQ(updated->rgba[0], 9);
  EXPECT_EQ(lib.find_by_hash(hash0), nullptr);
  ASSERT_NE(lib.find_by_hash(updated->content_hash), nullptr);
  EXPECT_EQ(lib.find_by_hash(updated->content_hash)->id, id);
}

TEST(TextureLibrary, RemoveUnusedLeavesReferenced) {
  TextureLibrary lib;
  const std::uint64_t used = lib.add(make_solid(1, 0, 0)).id;
  const std::uint64_t orphan = lib.add(make_solid(0, 1, 0)).id;
  Material mat{};
  mat.id = 1;
  mat.albedo_texture_id = used;
  std::unordered_map<std::uint64_t, Material> materials;
  materials.emplace(1, mat);
  EXPECT_EQ(lib.ref_count(used, materials), 1u);
  EXPECT_EQ(lib.ref_count(orphan, materials), 0u);
  EXPECT_EQ(lib.remove_unused(materials), 1u);
  EXPECT_NE(lib.find(used), nullptr);
  EXPECT_EQ(lib.find(orphan), nullptr);
}

TEST(DocumentTextures, ImportDedupAndRoundTrip) {
  Document doc("tex-lib");
  const std::size_t seeded = doc.textures().size();
  TextureAsset brick = make_solid(40, 50, 60);
  brick.name = "brick";
  brick.source_path = "C:/maps/brick.png";
  brick.usage = TextureUsage::Albedo;
  const std::uint64_t id = doc.import_texture(brick).id;
  EXPECT_EQ(doc.import_texture(std::move(brick)).id, id);
  EXPECT_EQ(doc.textures().size(), seeded + 1);

  auto bytes = serialize_document(doc);
  ASSERT_TRUE(bytes) << bytes.error();
  auto restored = deserialize_document(*bytes);
  ASSERT_TRUE(restored) << restored.error();
  const TextureAsset* loaded = restored->texture(id);
  ASSERT_NE(loaded, nullptr);
  EXPECT_EQ(loaded->name, "brick");
  EXPECT_EQ(loaded->source_path, "C:/maps/brick.png");
  EXPECT_EQ(loaded->usage, TextureUsage::Albedo);
  EXPECT_EQ(loaded->srgb, true);
  EXPECT_NE(loaded->content_hash, 0u);
  EXPECT_EQ(loaded->rgba[0], 40);
}

TEST(DocumentTextures, DefaultAssetsHaveNamesAndUsage) {
  Document doc("seeded");
  bool saw_albedo = false;
  bool saw_normal = false;
  for (const auto& [id, tex] : doc.textures()) {
    (void)id;
    EXPECT_FALSE(tex.name.empty());
    EXPECT_NE(tex.content_hash, 0u);
    if (tex.usage == TextureUsage::Albedo) {
      saw_albedo = true;
      EXPECT_TRUE(tex.srgb);
    }
    if (tex.usage == TextureUsage::Normal) {
      saw_normal = true;
      EXPECT_FALSE(tex.srgb);
    }
  }
  EXPECT_TRUE(saw_albedo);
  EXPECT_TRUE(saw_normal);
  for (const auto& [id, material] : doc.materials()) {
    (void)id;
    if (material.albedo_texture_id != 0) {
      EXPECT_GE(doc.texture_ref_count(material.albedo_texture_id), 1u);
    }
    EXPECT_GE(doc.texture_ref_count(material.normal_texture_id), 1u);
  }
  EXPECT_EQ(doc.remove_unused_textures(), 0u);
}

TEST(DocumentTextures, MaterialTransformRoundTrip) {
  Document doc("tex-xform");
  ASSERT_FALSE(doc.materials().empty());
  Material* material = doc.material(doc.materials().begin()->first);
  ASSERT_NE(material, nullptr);
  const std::uint64_t id = material->id;
  material->tex.scale = {3.f, 4.f};
  material->tex.offset = {0.25f, 0.5f};
  material->tex.rotation = 0.75f;
  material->tex.world_scale = 8.f;

  auto bytes = serialize_document(doc);
  ASSERT_TRUE(bytes) << bytes.error();
  auto restored = deserialize_document(*bytes);
  ASSERT_TRUE(restored) << restored.error();
  const Material* loaded = restored->material(id);
  ASSERT_NE(loaded, nullptr);
  EXPECT_FLOAT_EQ(loaded->tex.scale.x, 3.f);
  EXPECT_FLOAT_EQ(loaded->tex.scale.y, 4.f);
  EXPECT_FLOAT_EQ(loaded->tex.offset.x, 0.25f);
  EXPECT_FLOAT_EQ(loaded->tex.offset.y, 0.5f);
  EXPECT_FLOAT_EQ(loaded->tex.rotation, 0.75f);
  EXPECT_FLOAT_EQ(loaded->tex.world_scale, 8.f);
}

TEST(DocumentTextures, BuiltinPixelsOmittedFromArchive) {
  Document doc("builtin-omit");
  bool any_key = false;
  for (const auto& [id, tex] : doc.textures()) {
    (void)id;
    EXPECT_FALSE(tex.builtin_key.empty());
    any_key = true;
  }
  EXPECT_TRUE(any_key);

  auto bytes = serialize_document(doc);
  ASSERT_TRUE(bytes) << bytes.error();
  EXPECT_LT(bytes->size(), 512 * 1024u);

  auto restored = deserialize_document(*bytes);
  ASSERT_TRUE(restored) << restored.error();
  EXPECT_EQ(restored->textures().size(), doc.textures().size());
  for (const auto& [id, tex] : restored->textures()) {
    const TextureAsset* original = doc.texture(id);
    ASSERT_NE(original, nullptr);
    EXPECT_EQ(tex.builtin_key, original->builtin_key);
    EXPECT_EQ(tex.width, original->width);
    EXPECT_EQ(tex.height, original->height);
    EXPECT_EQ(tex.rgba.size(), original->rgba.size());
    EXPECT_EQ(tex.content_hash, original->content_hash);
  }
}

TEST(DocumentTextures, OrmRoundTripAndRefCount) {
  Document doc("orm");
  TextureAsset orm = make_solid(255, 128, 0, false);
  orm.name = "packed-orm";
  orm.usage = TextureUsage::Orm;
  const std::uint64_t tid = doc.import_texture(std::move(orm)).id;
  ASSERT_FALSE(doc.materials().empty());
  Material* material = doc.material(doc.materials().begin()->first);
  ASSERT_NE(material, nullptr);
  material->orm_texture_id = tid;
  EXPECT_GE(doc.texture_ref_count(tid), 1u);

  auto bytes = serialize_document(doc);
  ASSERT_TRUE(bytes) << bytes.error();
  auto restored = deserialize_document(*bytes);
  ASSERT_TRUE(restored) << restored.error();
  const Material* loaded = restored->material(material->id);
  ASSERT_NE(loaded, nullptr);
  EXPECT_EQ(loaded->orm_texture_id, tid);
  const TextureAsset* loaded_tex = restored->texture(tid);
  ASSERT_NE(loaded_tex, nullptr);
  EXPECT_EQ(loaded_tex->usage, TextureUsage::Orm);
}

TEST(TextureLibrary, ReplaceClearsBuiltinKey) {
  TextureLibrary lib;
  TextureAsset builtin = make_solid(1, 2, 3);
  builtin.builtin_key = "default.albedo";
  builtin.name = "Default albedo";
  const std::uint64_t id = lib.add(std::move(builtin)).id;
  TextureAsset next = make_solid(9, 8, 7);
  ASSERT_TRUE(lib.replace(id, std::move(next)));
  const TextureAsset* updated = lib.find(id);
  ASSERT_NE(updated, nullptr);
  EXPECT_TRUE(updated->builtin_key.empty());
}

TEST(TextureMips, CountsAndBoxFilter) {
  EXPECT_EQ(texture_mip_levels(0, 0), 1u);
  EXPECT_EQ(texture_mip_levels(1, 1), 1u);
  EXPECT_EQ(texture_mip_levels(4, 4), 3u);
  EXPECT_EQ(texture_mip_levels(512, 512), 10u);

  TextureAsset tex;
  tex.width = 4;
  tex.height = 4;
  tex.srgb = false;
  tex.rgba.assign(64, 255);
  const auto mips = build_texture_mips(tex);
  ASSERT_EQ(mips.size(), 3u);
  EXPECT_EQ(mips[0].size(), 64u);
  EXPECT_EQ(mips[1].size(), 16u);
  EXPECT_EQ(mips[2].size(), 4u);
  EXPECT_EQ(mips[1][0], 255);
  EXPECT_EQ(mips[2][0], 255);
}

}  // namespace tamias
