#include "engine/document/document.h"
#include "engine/core/fs_utf8.h"
#include "engine/io/mesh_io.h"
#include "engine/render/render_scene.h"
#include "engine/render/render_scene_golden.h"
#include "engine/render/scene_graph.h"
#include "engine/render/gpu_instance.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_map>

namespace tamias {
namespace {

class MockBuffer : public Buffer {
 public:
  const BufferDesc& desc() const override { return desc_; }
  Result<void> write(std::uint64_t, std::span<const std::byte>) override { return {}; }
  BufferDesc desc_{};
};

class MockTexture : public Texture {
 public:
  const TextureDesc& desc() const override { return desc_; }
  Result<void> write(std::uint64_t, std::span<const std::byte>) override { return {}; }
  Result<void> write_subresource(std::uint32_t, std::uint32_t,
                                 std::span<const std::byte>) override {
    return {};
  }
  TextureDesc desc_{};
};

class MockPipeline : public PipelineState {};

class MockCommandList : public CommandList {
 public:
  void begin() override {}
  void end() override {}
  void begin_render_pass(SwapChain&, const float[4], float) override {}
  void end_render_pass() override {}
  void set_pipeline(PipelineState& pipeline) override { pipelines.push_back(&pipeline); }
  void set_vertex_buffer(Buffer&, std::uint64_t) override { ++vertex_binds; }
  void set_instance_buffer(Buffer&, std::uint64_t) override { ++instance_binds; }
  void set_index_buffer(Buffer&, std::uint64_t) override { ++index_binds; }
  void set_push_constants(std::span<const std::byte> data) override {
    PushConstants pc{};
    const std::size_t n = std::min(data.size(), sizeof(pc));
    std::memcpy(&pc, data.data(), n);
    push_constants.push_back(pc);
  }
  void set_texture(Texture&, std::uint32_t) override { ++texture_binds; }
  void draw_indexed(const DrawIndexedDesc& desc) override { draws.push_back(desc); }
  void set_viewport(float, float, float, float, float, float) override {}
  void set_scissor(std::int32_t, std::int32_t, std::uint32_t, std::uint32_t) override {}

  std::vector<PipelineState*> pipelines;
  std::vector<PushConstants> push_constants;
  std::vector<DrawIndexedDesc> draws;
  int texture_binds = 0;
  int vertex_binds = 0;
  int instance_binds = 0;
  int index_binds = 0;
};

struct Fixture {
  MockCommandList cmds;
  MockPipeline shaded;
  MockPipeline wire;
  MockPipeline entity_line;
  MockTexture default_tex;
  MockTexture default_normal_tex;
  std::unordered_map<std::uint64_t, GpuMesh> meshes;
  std::unordered_map<std::uint64_t, std::uint64_t> asset_to_gpu;
  std::unordered_map<std::uint64_t, GpuTexture> textures;
  std::unordered_map<std::uint64_t, std::uint64_t> texture_asset_to_gpu;
  bool texture_diag_logged = false;
  Mat4 view_proj = Mat4::identity();
  SceneGraphDrawContext ctx;
  MockBuffer instance_buf;
  std::vector<GpuInstance> recorded;

  Fixture() {
    ctx.command_list = &cmds;
    ctx.view_proj = &view_proj;
    ctx.eye_position = {0.f, 0.f, 5.f};
    ctx.mode_value = 1.f;
    ctx.shaded_pipeline = &shaded;
    ctx.wire_pipeline = &wire;
    ctx.entity_line_pipeline = &entity_line;
    ctx.default_texture = &default_tex;
    ctx.default_normal = &default_normal_tex;
    ctx.meshes = &meshes;
    ctx.asset_to_gpu = &asset_to_gpu;
    ctx.textures = &textures;
    ctx.texture_asset_to_gpu = &texture_asset_to_gpu;
    ctx.texture_diag_logged = &texture_diag_logged;
    ctx.instance_buffer = &instance_buf;
    ctx.recorded_instances = &recorded;
  }

  void add_mesh(std::uint64_t asset_id, std::uint32_t index_count) {
    GpuMesh mesh;
    mesh.vertex_buffer = std::make_unique<MockBuffer>();
    mesh.index_buffer = std::make_unique<MockBuffer>();
    mesh.index_count = index_count;
    const std::uint64_t gpu_id = meshes.size() + 1;
    meshes.emplace(gpu_id, std::move(mesh));
    asset_to_gpu[asset_id] = gpu_id;
  }

  void visit(RenderNode& root) {
    recorded.clear();
    RecordCommands visitor(ctx);
    root.accept(visitor);
  }
};

void expect_mat4_eq(const Mat4& a, const Mat4& b) {
  for (int i = 0; i < 16; ++i) {
    EXPECT_FLOAT_EQ(a.m[i], b.m[i]) << "matrix element m[" << i << "]";
  }
}

void expect_vec3_eq(const Vec3& a, const Vec3& b) {
  EXPECT_FLOAT_EQ(a.x, b.x);
  EXPECT_FLOAT_EQ(a.y, b.y);
  EXPECT_FLOAT_EQ(a.z, b.z);
}

std::uint32_t read_u32le(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
  return static_cast<std::uint32_t>(bytes[offset]) |
         (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
         (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
         (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

TextureAsset make_tiny_texture(std::uint64_t id, bool srgb = true) {
  TextureAsset tex;
  tex.id = id;
  tex.width = 2;
  tex.height = 2;
  tex.srgb = srgb;
  tex.rgba = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 0, 255};
  return tex;
}

void expect_texture_eq(const TextureAsset& a, const TextureAsset& b) {
  EXPECT_EQ(a.id, b.id);
  EXPECT_EQ(a.width, b.width);
  EXPECT_EQ(a.height, b.height);
  EXPECT_EQ(a.srgb, b.srgb);
  EXPECT_EQ(a.rgba, b.rgba);
}

void expect_item_eq(const SceneDrawItem& a, const SceneDrawItem& b) {
  EXPECT_EQ(a.node_id, b.node_id);
  EXPECT_EQ(a.mesh_asset_id, b.mesh_asset_id);
  expect_mat4_eq(a.transform, b.transform);
  expect_vec3_eq(a.color, b.color);
  expect_vec3_eq(a.category_color, b.category_color);
  EXPECT_FLOAT_EQ(a.roughness, b.roughness);
  EXPECT_FLOAT_EQ(a.metallic, b.metallic);
  EXPECT_FLOAT_EQ(a.opacity, b.opacity);
  EXPECT_EQ(a.albedo_texture_id, b.albedo_texture_id);
  EXPECT_EQ(a.normal_texture_id, b.normal_texture_id);
  EXPECT_EQ(a.orm_texture_id, b.orm_texture_id);
  EXPECT_FLOAT_EQ(a.tex.scale.x, b.tex.scale.x);
  EXPECT_FLOAT_EQ(a.tex.scale.y, b.tex.scale.y);
  EXPECT_FLOAT_EQ(a.tex.offset.x, b.tex.offset.x);
  EXPECT_FLOAT_EQ(a.tex.offset.y, b.tex.offset.y);
  EXPECT_FLOAT_EQ(a.tex.rotation, b.tex.rotation);
  EXPECT_FLOAT_EQ(a.tex.world_scale, b.tex.world_scale);
  EXPECT_EQ(a.selected, b.selected);
  EXPECT_EQ(a.lines, b.lines);
}

RenderScene handmade_scene() {
  RenderScene scene;
  scene.source = "handmade";
  scene.view.mode = RenderMode::Realistic;
  scene.view.width = 800;
  scene.view.height = 600;
  scene.view.view_distance = 12.f;
  scene.view.target = {1.f, 2.f, 3.f};
  MeshCpu cube = make_box_mesh(1.f, 1.f, 1.f);
  cube.has_texcoord = true;
  SceneDrawItem item{};
  item.node_id = 3;
  item.mesh_asset_id = 7;
  item.transform = translate({1.f, 2.f, 3.f});
  item.bounds = cube.bounds;
  item.color = {0.1f, 0.2f, 0.3f};
  item.category_color = {0.6f, 0.7f, 0.8f};
  item.roughness = 0.15f;
  item.metallic = 0.4f;
  item.opacity = 0.9f;
  item.albedo_texture_id = 11;
  item.selected = true;
  item.tex.scale = {2.f, 3.f};
  item.tex.offset = {0.1f, 0.2f};
  item.tex.rotation = 0.5f;
  item.tex.world_scale = 4.f;
  scene.meshes.emplace(7, std::move(cube));
  scene.textures.emplace(11, make_tiny_texture(11));
  scene.items.push_back(item);
  return scene;
}

}  // namespace

TEST(RenderSceneIo, HandmadeRoundTrip) {
  const RenderScene original = handmade_scene();
  auto bytes = serialize_render_scene(original);
  ASSERT_TRUE(bytes) << bytes.error();
  ASSERT_GE(bytes->size(), 4u);
  EXPECT_EQ((*bytes)[0], static_cast<std::uint8_t>('T'));
  EXPECT_EQ((*bytes)[1], static_cast<std::uint8_t>('R'));
  EXPECT_EQ((*bytes)[2], static_cast<std::uint8_t>('S'));
  EXPECT_EQ((*bytes)[3], static_cast<std::uint8_t>('C'));

  auto loaded = deserialize_render_scene(*bytes);
  ASSERT_TRUE(loaded) << loaded.error();
  EXPECT_EQ(loaded->source, "handmade");
  EXPECT_EQ(loaded->view.mode, RenderMode::Realistic);
  EXPECT_EQ(loaded->view.width, 800u);
  EXPECT_EQ(loaded->view.height, 600u);
  EXPECT_FLOAT_EQ(loaded->view.view_distance, 12.f);
  EXPECT_EQ(loaded->meshes.size(), 1u);
  ASSERT_EQ(loaded->textures.size(), 1u);
  expect_texture_eq(loaded->textures.at(11), original.textures.at(11));
  ASSERT_EQ(loaded->items.size(), 1u);
  expect_item_eq(loaded->items[0], original.items[0]);
  const MeshCpu& mesh = loaded->meshes.at(7);
  EXPECT_TRUE(mesh.has_texcoord);
  EXPECT_FALSE(mesh.line_list);
  EXPECT_EQ(mesh.indices.size(), original.meshes.at(7).indices.size());
  EXPECT_EQ(render_scene_digest(*loaded), render_scene_digest(original));
}

TEST(RenderSceneIo, FileRoundTrip) {
  const auto path = std::filesystem::temp_directory_path() / "tamias_render_scene.trscn";
  const RenderScene original = handmade_scene();
  ASSERT_TRUE(save_render_scene(path, original)) << "save failed";
  EXPECT_TRUE(is_render_scene_path(path));
  auto loaded = load_render_scene(path);
  ASSERT_TRUE(loaded) << loaded.error();
  EXPECT_EQ(loaded->items.size(), 1u);
  EXPECT_EQ(render_scene_digest(*loaded), render_scene_digest(original));
  std::error_code ec;
  std::filesystem::remove(path, ec);
}

TEST(RenderSceneIo, DigestStableAcrossRewrite) {
  const RenderScene original = handmade_scene();
  auto a = serialize_render_scene(original);
  auto b = serialize_render_scene(original);
  ASSERT_TRUE(a);
  ASSERT_TRUE(b);
  auto la = deserialize_render_scene(*a);
  auto lb = deserialize_render_scene(*b);
  ASSERT_TRUE(la);
  ASSERT_TRUE(lb);
  EXPECT_EQ(render_scene_digest(*la), render_scene_digest(*lb));
}

TEST(RenderSceneIo, OmitsTextChunkWhenNoTextures) {
  RenderScene scene;
  scene.source = "no-tex";
  MeshCpu cube = make_box_mesh(1.f, 1.f, 1.f);
  SceneDrawItem item{};
  item.node_id = 1;
  item.mesh_asset_id = 1;
  item.bounds = cube.bounds;
  scene.meshes.emplace(1, std::move(cube));
  scene.items.push_back(item);

  auto bytes = serialize_render_scene(scene);
  ASSERT_TRUE(bytes) << bytes.error();
  ASSERT_GE(bytes->size(), 12u);
  EXPECT_EQ(read_u32le(*bytes, 8), 4u);

  auto loaded = deserialize_render_scene(*bytes);
  ASSERT_TRUE(loaded) << loaded.error();
  EXPECT_TRUE(loaded->textures.empty());
}

TEST(RenderSceneIo, IncludesTextChunkWhenTexturesPresent) {
  auto bytes = serialize_render_scene(handmade_scene());
  ASSERT_TRUE(bytes) << bytes.error();
  ASSERT_GE(bytes->size(), 12u);
  EXPECT_EQ(read_u32le(*bytes, 8), 5u);
}

TEST(RenderScene, CapturesOnlyReferencedMeshes) {
  Document doc("cap");
  MeshAsset unused{};
  unused.name = "unused";
  unused.cpu = make_box_mesh(1.f, 1.f, 1.f);
  doc.add_mesh(std::move(unused));
  const std::uint64_t used =
      doc.add_import_mesh("used", make_box_mesh(2.f, 2.f, 2.f), Mat4::identity(),
                          {0.5f, 0.5f, 0.5f});

  const RenderScene snap = doc.capture_render_scene({});
  EXPECT_EQ(snap.meshes.size(), 1u);
  EXPECT_TRUE(snap.meshes.contains(used));
  ASSERT_EQ(snap.items.size(), 1u);
  EXPECT_EQ(snap.items[0].mesh_asset_id, used);
}

TEST(RenderScene, CapturesOnlyReferencedTextures) {
  Document doc("tex");
  TextureAsset extra = make_tiny_texture(0);
  doc.add_texture(std::move(extra));
  doc.add_import_mesh("used", make_box_mesh(1.f, 1.f, 1.f), Mat4::identity(),
                      {0.5f, 0.5f, 0.5f});
  EXPECT_GE(doc.textures().size(), 12u);

  const RenderScene snap = doc.capture_render_scene({});
  EXPECT_TRUE(snap.textures.empty());
}

TEST(RenderScene, BakeDropsUnreferencedTextures) {
  MeshCpu cube = make_box_mesh(1.f, 1.f, 1.f);
  std::unordered_map<std::uint64_t, MeshCpu> meshes;
  meshes.emplace(7, cube);
  std::unordered_map<std::uint64_t, TextureAsset> textures;
  textures.emplace(11, make_tiny_texture(11));
  TextureAsset unused = make_tiny_texture(99);
  unused.rgba[0] = 7;
  textures.emplace(99, std::move(unused));

  SceneDrawItem item{};
  item.node_id = 1;
  item.mesh_asset_id = 7;
  item.albedo_texture_id = 11;
  item.bounds = cube.bounds;

  const RenderScene baked = bake_render_scene({item}, meshes, {}, "bake", textures);
  EXPECT_EQ(baked.textures.size(), 1u);
  EXPECT_TRUE(baked.textures.contains(11));
  EXPECT_FALSE(baked.textures.contains(99));
}

TEST(RenderScene, CaptureMatchesRenderItems) {
  Document doc("match");
  doc.add_import_mesh("a", make_demo_cube(), translate({1.f, 0.f, 0.f}), {0.2f, 0.4f, 0.6f});
  doc.add_import_mesh("b", make_box_mesh(0.5f, 0.5f, 0.5f), translate({0.f, 2.f, 0.f}),
                      {0.9f, 0.1f, 0.1f});
  const auto live = doc.render_items();
  const RenderScene snap = doc.capture_render_scene({});
  ASSERT_EQ(snap.items.size(), live.size());
  for (std::size_t i = 0; i < live.size(); ++i) {
    const SceneDrawItem* found = nullptr;
    for (const auto& item : snap.items) {
      if (item.node_id == live[i].node_id) {
        found = &item;
        break;
      }
    }
    ASSERT_NE(found, nullptr);
    expect_item_eq(*found, live[i]);
  }
}

TEST(RenderScene, DigestChangesWhenMeshChanges) {
  Document a("a");
  Document b("b");
  a.add_import_mesh("box", make_box_mesh(1.f, 1.f, 1.f), Mat4::identity(), {1.f, 1.f, 1.f});
  b.add_import_mesh("box", make_box_mesh(2.f, 1.f, 1.f), Mat4::identity(), {1.f, 1.f, 1.f});
  const std::string da = render_scene_digest(a.capture_render_scene({}));
  const std::string db = render_scene_digest(b.capture_render_scene({}));
  EXPECT_NE(da, db);
  EXPECT_EQ(da, render_scene_digest(a.capture_render_scene({})));
}

TEST(RenderScene, DigestChangesWhenTextureChanges) {
  RenderScene a = handmade_scene();
  RenderScene b = handmade_scene();
  EXPECT_EQ(render_scene_digest(a), render_scene_digest(b));
  b.textures.at(11).rgba[0] = 1;
  EXPECT_NE(render_scene_digest(a), render_scene_digest(b));
}

TEST(RenderScene, FrustumCaptureDropsOffscreen) {
  Document doc("frustum");
  const std::uint64_t front =
      doc.add_import_mesh("front", make_demo_cube(), Mat4::identity(), {1.f, 1.f, 1.f});
  doc.add_import_mesh("behind", make_demo_cube(), translate({0.f, 0.f, 40.f}), {1.f, 1.f, 1.f});

  const Mat4 view = look_at({0.f, 0.f, 8.f}, {0.f, 0.f, 0.f}, {0.f, 1.f, 0.f});
  const Mat4 proj = perspective(0.8f, 1.f, 0.05f, 500.f);
  const Frustum frustum = Frustum::from_view_proj(proj * view);

  const RenderScene snap = doc.capture_render_scene({}, &frustum);
  ASSERT_EQ(snap.items.size(), 1u);
  EXPECT_EQ(snap.items[0].mesh_asset_id, front);
}

TEST(RenderScene, HydrateKeepsBakedMaterial) {
  RenderScene original = handmade_scene();
  Document doc = document_from_render_scene(original);
  ASSERT_NE(doc.render_snapshot(), nullptr);
  const auto items = doc.render_items();
  ASSERT_EQ(items.size(), 1u);
  EXPECT_FLOAT_EQ(items[0].roughness, 0.15f);
  EXPECT_FLOAT_EQ(items[0].metallic, 0.4f);
  EXPECT_FLOAT_EQ(items[0].opacity, 0.9f);
  EXPECT_EQ(items[0].albedo_texture_id, 11u);
  EXPECT_FLOAT_EQ(items[0].tex.world_scale, 4.f);
  EXPECT_FLOAT_EQ(items[0].tex.scale.x, 2.f);
  expect_vec3_eq(items[0].color, {0.1f, 0.2f, 0.3f});
  EXPECT_EQ(doc.textures().size(), 1u);
  ASSERT_NE(doc.texture(11), nullptr);
  expect_texture_eq(*doc.texture(11), original.textures.at(11));

  const RenderScene recapture = doc.capture_render_scene({});
  ASSERT_EQ(recapture.items.size(), 1u);
  EXPECT_FLOAT_EQ(recapture.items[0].roughness, 0.15f);
  EXPECT_EQ(recapture.textures.size(), 1u);
}

TEST(RenderScene, ReplayBuildsSameDrawCount) {
  const RenderScene scene = handmade_scene();
  Fixture f;
  const MeshCpu& cpu = scene.meshes.at(7);
  f.add_mesh(7, static_cast<std::uint32_t>(cpu.indices.size()));
  GpuTexture gpu_albedo;
  gpu_albedo.texture = std::make_unique<MockTexture>();
  f.textures.emplace(1, std::move(gpu_albedo));
  f.texture_asset_to_gpu[11] = 1;

  auto root = build_scene_graph(scene.items);
  f.visit(*root);

  ASSERT_EQ(f.cmds.draws.size(), scene.items.size());
  ASSERT_EQ(f.cmds.draws[0].instance_count, 1u);
  ASSERT_EQ(f.recorded.size(), 1u);
  EXPECT_FLOAT_EQ(f.recorded[0].color[0], 0.6f);  // shaded uses category
  expect_mat4_eq(gpu_instance_world(f.recorded[0]), scene.items[0].transform);
}

TEST(RenderScene, InspectMentionsDigestAndItems) {
  const std::string text = inspect_render_scene(handmade_scene());
  EXPECT_NE(text.find("handmade"), std::string::npos);
  EXPECT_NE(text.find("digest="), std::string::npos);
  EXPECT_NE(text.find("item node=3"), std::string::npos);
  EXPECT_NE(text.find("selected"), std::string::npos);
  EXPECT_NE(text.find("textures=1"), std::string::npos);
  EXPECT_NE(text.find("albedo=11"), std::string::npos);
  EXPECT_NE(text.find("==== VIEW ===="), std::string::npos);
  EXPECT_NE(text.find("==== MESHES ===="), std::string::npos);
  EXPECT_NE(text.find("==== TEXTURES ===="), std::string::npos);
  EXPECT_NE(text.find("v[0]"), std::string::npos);
  EXPECT_NE(text.find("transform:"), std::string::npos);
  EXPECT_NE(text.find("roughness="), std::string::npos);
  EXPECT_NE(text.find("world_scale="), std::string::npos);
}

TEST(RenderScene, DebugDumpWritesUvAndWorldDraw) {
  const auto root = std::filesystem::temp_directory_path() / "tamias_debug_dump";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  ASSERT_FALSE(ec) << ec.message();
  const RenderScene original = handmade_scene();
  const auto trscn = root / "scene.trscn";
  ASSERT_TRUE(write_render_scene_debug_sidecars(trscn, original)) << "debug dump failed";

  const auto mesh_obj = root / "scene.debug" / "mesh_7.obj";
  const auto draw_obj = root / "scene.debug" / "draw_0_node_3.obj";
  EXPECT_TRUE(std::filesystem::is_regular_file(mesh_obj, ec));
  EXPECT_TRUE(std::filesystem::is_regular_file(draw_obj, ec));
  EXPECT_TRUE(std::filesystem::is_regular_file(root / "scene.debug" / "tex_11.ppm", ec));

  std::ifstream mesh_in(mesh_obj);
  ASSERT_TRUE(mesh_in);
  std::string mesh_text((std::istreambuf_iterator<char>(mesh_in)), std::istreambuf_iterator<char>());
  EXPECT_NE(mesh_text.find("vt "), std::string::npos);
  EXPECT_NE(mesh_text.find("f 1/1/1"), std::string::npos);

  std::ifstream draw_in(draw_obj);
  ASSERT_TRUE(draw_in);
  std::string draw_text((std::istreambuf_iterator<char>(draw_in)), std::istreambuf_iterator<char>());
  EXPECT_NE(draw_text.find("v 0.500000000 1.500000000 2.500000000"), std::string::npos);

  const auto one = root / "one_draw.obj";
  ASSERT_TRUE(write_render_scene_debug_draw(one, original, 0));
  EXPECT_TRUE(std::filesystem::is_regular_file(one, ec));
  EXPECT_FALSE(write_render_scene_debug_draw(root / "bad.obj", original, 9));

  std::filesystem::remove_all(root, ec);
}

TEST(RenderSceneIo, UnicodePathRoundTrip) {
  const auto dir = std::filesystem::temp_directory_path() / std::filesystem::path(u8"tamias_场景");
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  ASSERT_FALSE(ec) << ec.message();
  const auto path = dir / std::filesystem::path(u8"快照.trscn");
  const RenderScene original = handmade_scene();
  ASSERT_TRUE(save_render_scene(path, original)) << "save failed";
  EXPECT_TRUE(is_render_scene_path(path));
  EXPECT_NO_THROW(path_to_utf8(path));
  auto loaded = load_render_scene(path);
  ASSERT_TRUE(loaded) << loaded.error();
  EXPECT_EQ(render_scene_digest(*loaded), render_scene_digest(original));
  std::filesystem::remove(path, ec);
  std::filesystem::remove(dir, ec);
}

TEST(RenderSceneIo, RejectsBadMagic) {
  const std::uint8_t junk[] = {'T', 'M', 'A', 'S', 1, 0, 0, 0};
  auto loaded = deserialize_render_scene(junk);
  EXPECT_FALSE(loaded);
}

TEST(RenderSceneGolden, PinWritesSidecarAndRoundTrips) {
  const auto root = std::filesystem::temp_directory_path() / "tamias_golden_pin";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  const RenderScene original = handmade_scene();
  auto meta = save_render_scene_golden(root, "box", original, false);
  ASSERT_TRUE(meta) << meta.error();
  EXPECT_EQ(meta->name, "box");
  EXPECT_EQ(meta->digest, render_scene_digest(original));
  EXPECT_EQ(meta->items, 1u);
  EXPECT_EQ(meta->textures, 1u);

  EXPECT_TRUE(std::filesystem::is_regular_file(root / "box" / "debug" / "mesh_7.obj", ec));
  EXPECT_TRUE(std::filesystem::is_regular_file(root / "box" / "debug" / "tex_11.ppm", ec));
  EXPECT_TRUE(std::filesystem::is_regular_file(root / "box" / "debug" / "draw_0_node_3.obj", ec));
  const auto inspect_size = std::filesystem::file_size(root / "box" / "scene.inspect.txt", ec);
  EXPECT_GT(inspect_size, 400u);

  auto loaded_meta = load_render_scene_golden_meta(root / "box");
  ASSERT_TRUE(loaded_meta) << loaded_meta.error();
  EXPECT_EQ(loaded_meta->digest, meta->digest);

  auto exists = save_render_scene_golden(root, "box", original, false);
  EXPECT_FALSE(exists);

  auto again = save_render_scene_golden(root, "box", original, true);
  ASSERT_TRUE(again) << again.error();
  EXPECT_EQ(again->digest, meta->digest);

  std::filesystem::remove_all(root, ec);
}

TEST(RenderSceneGolden, RefreshSidecarFromExistingTrscn) {
  const auto root = std::filesystem::temp_directory_path() / "tamias_golden_refresh";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  const RenderScene original = handmade_scene();
  auto meta = save_render_scene_golden(root, "box", original, false);
  ASSERT_TRUE(meta) << meta.error();

  const auto dir = root / "box";
  std::filesystem::remove(dir / "scene.meta.json", ec);
  std::filesystem::remove(dir / "scene.inspect.txt", ec);
  EXPECT_TRUE(list_render_scene_goldens(root).empty());

  auto refreshed = refresh_render_scene_golden_sidecar(dir);
  ASSERT_TRUE(refreshed) << refreshed.error();
  EXPECT_EQ(refreshed->digest, meta->digest);
  EXPECT_EQ(refreshed->items, meta->items);

  const auto listed = list_render_scene_goldens(root);
  ASSERT_EQ(listed.size(), 1u);
  auto loaded_meta = load_render_scene_golden_meta(listed[0]);
  ASSERT_TRUE(loaded_meta) << loaded_meta.error();
  EXPECT_EQ(loaded_meta->digest, meta->digest);

  std::filesystem::remove_all(root, ec);
}

TEST(RenderSceneGolden, IncompleteRepoFixturesAreRejected) {
  const auto root = render_scene_golden_root(TAMIAS_SOURCE_DIR);
  std::error_code ec;
  if (!std::filesystem::is_directory(root, ec)) {
    return;
  }
  for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
    if (ec || !entry.is_directory()) {
      continue;
    }
    const auto trscn = entry.path() / "scene.trscn";
    const auto meta = entry.path() / "scene.meta.json";
    if (std::filesystem::is_regular_file(trscn, ec) && !ec &&
        !std::filesystem::is_regular_file(meta, ec)) {
      ADD_FAILURE() << "incomplete golden (missing scene.meta.json): "
                    << path_to_utf8(entry.path())
                    << " — Pin again so sidecar is written beside scene.trscn";
    }
  }
}

TEST(RenderSceneGolden, ScansRepositoryFixtures) {
  const auto root = render_scene_golden_root(TAMIAS_SOURCE_DIR);
  const auto dirs = list_render_scene_goldens(root);
  if (dirs.empty()) {
    GTEST_SKIP() << "no goldens in " << path_to_utf8(root);
  }
  for (const auto& dir : dirs) {
    SCOPED_TRACE(path_to_utf8(dir));
    auto meta = load_render_scene_golden_meta(dir);
    ASSERT_TRUE(meta) << meta.error();
    auto scene = load_render_scene(dir / "scene.trscn");
    ASSERT_TRUE(scene) << scene.error();
    EXPECT_EQ(render_scene_digest(*scene), meta->digest);
    EXPECT_EQ(scene->items.size(), meta->items);
    EXPECT_EQ(scene->meshes.size(), meta->meshes);
    EXPECT_EQ(scene->textures.size(), meta->textures);
    EXPECT_EQ(static_cast<int>(scene->view.mode), meta->mode);

    Document doc = document_from_render_scene(*scene);
    EXPECT_EQ(doc.render_items().size(), scene->items.size());

    Fixture f;
    for (const auto& [id, mesh] : scene->meshes) {
      f.add_mesh(id, static_cast<std::uint32_t>(mesh.indices.size()));
    }
    std::uint64_t gpu_tex = 1;
    for (const auto& item : scene->items) {
      for (std::uint64_t tex_id :
           {item.albedo_texture_id, item.normal_texture_id, item.orm_texture_id}) {
        if (tex_id == 0 || f.texture_asset_to_gpu.contains(tex_id)) {
          continue;
        }
        GpuTexture gpu;
        gpu.texture = std::make_unique<MockTexture>();
        f.textures.emplace(gpu_tex, std::move(gpu));
        f.texture_asset_to_gpu[tex_id] = gpu_tex;
        ++gpu_tex;
      }
    }
    auto graph = build_scene_graph(scene->items);
    f.visit(*graph);
    std::uint32_t instances = 0;
    for (const auto& d : f.cmds.draws) {
      instances += d.instance_count;
    }
    EXPECT_EQ(instances, scene->items.size());
    EXPECT_LE(f.cmds.draws.size(), scene->items.size());
  }
}

TEST(RenderSceneGolden, RejectsBadSlug) {
  EXPECT_FALSE(is_render_scene_golden_slug(""));
  EXPECT_FALSE(is_render_scene_golden_slug("1box"));
  EXPECT_FALSE(is_render_scene_golden_slug("box.trscn"));
  EXPECT_TRUE(is_render_scene_golden_slug("box"));
  EXPECT_TRUE(is_render_scene_golden_slug("wall-corner"));
  EXPECT_EQ(suggest_render_scene_golden_slug(""), "scene");
  EXPECT_EQ(suggest_render_scene_golden_slug("12box"), "s12box");
}

}  // namespace tamias
