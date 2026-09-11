#include "engine/document/document.h"
#include "engine/graphics/mesh.h"
#include "engine/math/math.h"
#include "engine/render/render_scene.h"
#include "engine/render/scene_debug_log.h"
#include "engine/render/scene_debug_player.h"

#include <gtest/gtest.h>

namespace tamias {
namespace {

RenderScene two_box_scene() {
  RenderScene scene;
  scene.source = "two-box";
  MeshCpu a = make_box_mesh(1.f, 1.f, 1.f);
  MeshCpu b = make_box_mesh(1.f, 1.f, 1.f);
  SceneDrawItem ia{};
  ia.node_id = 1;
  ia.mesh_asset_id = 10;
  ia.bounds = a.bounds;
  SceneDrawItem ib{};
  ib.node_id = 2;
  ib.mesh_asset_id = 20;
  ib.bounds = b.bounds;
  ib.transform = translate({4.f, 0.f, 0.f});
  scene.meshes.emplace(10, std::move(a));
  scene.meshes.emplace(20, std::move(b));
  scene.items.push_back(ia);
  scene.items.push_back(ib);
  return scene;
}

}  // namespace

TEST(SceneDebugPlayer, IsolateAndStepFilterItems) {
  SceneDebugPlayer player;
  player.set_scene(two_box_scene());
  EXPECT_EQ(player.filtered_items().size(), 2u);

  player.set_step_count(1);
  ASSERT_EQ(player.filtered_items().size(), 1u);
  EXPECT_EQ(player.filtered_items()[0].node_id, 1u);

  player.set_step_count(std::nullopt);
  player.set_isolate_node(2);
  ASSERT_EQ(player.filtered_items().size(), 1u);
  EXPECT_EQ(player.filtered_items()[0].node_id, 2u);

  FrameSubmission frame = player.make_frame({}, 64, 64, TurntableCamera{}, RenderMode::Shaded);
  ASSERT_EQ(frame.items.size(), 1u);
  EXPECT_EQ(frame.items[0].node_id, 2u);
  EXPECT_FALSE(frame.show_axes);
}

TEST(SceneDebugPlayer, HiddenRoundTripChangesDigest) {
  RenderScene scene = two_box_scene();
  const std::string before = render_scene_digest(scene);
  scene.hidden_node_ids = {2};
  EXPECT_NE(render_scene_digest(scene), before);

  auto bytes = serialize_render_scene(scene);
  ASSERT_TRUE(bytes) << bytes.error();
  auto loaded = deserialize_render_scene(*bytes);
  ASSERT_TRUE(loaded) << loaded.error();
  ASSERT_EQ(loaded->hidden_node_ids.size(), 1u);
  EXPECT_EQ(loaded->hidden_node_ids[0], 2u);
  EXPECT_EQ(render_scene_digest(*loaded), render_scene_digest(scene));

  SceneDebugPlayer player;
  player.set_scene(*loaded);
  EXPECT_EQ(player.hidden_node_ids().size(), 1u);
  player.set_apply_captured_hidden(false);
  EXPECT_TRUE(player.hidden_node_ids().empty());
}

TEST(SceneDebugPlayer, EmptyHiddenKeepsLegacyDigest) {
  RenderScene scene = two_box_scene();
  const std::string digest = render_scene_digest(scene);
  auto bytes = serialize_render_scene(scene);
  ASSERT_TRUE(bytes);
  auto loaded = deserialize_render_scene(*bytes);
  ASSERT_TRUE(loaded);
  EXPECT_TRUE(loaded->hidden_node_ids.empty());
  EXPECT_EQ(render_scene_digest(*loaded), digest);
}

TEST(SceneDebugLog, MarksHiddenAndRecordsDraws) {
  RenderScene scene = two_box_scene();
  scene.hidden_node_ids = {2};
  SceneDebugPlayer player;
  player.set_scene(scene);
  const SceneDebugLog log = capture_scene_debug_log(player, nullptr);
  ASSERT_EQ(log.items.size(), 2u);
  EXPECT_EQ(log.items[0].reason, SceneDebugSkipReason::Drawn);
  EXPECT_EQ(log.items[1].reason, SceneDebugSkipReason::Hidden);
  EXPECT_EQ(log.hidden, 1u);
  EXPECT_EQ(log.drawn_items, 1u);
  EXPECT_GE(log.draws.size(), 1u);
}

TEST(SceneDebugLog, IsolateAndStepReasons) {
  SceneDebugPlayer player;
  player.set_scene(two_box_scene());
  player.set_isolate_node(1);
  SceneDebugLog log = capture_scene_debug_log(player, nullptr);
  EXPECT_EQ(log.isolated, 1u);
  EXPECT_EQ(log.drawn_items, 1u);

  player.set_isolate_node(std::nullopt);
  player.set_step_count(1);
  log = capture_scene_debug_log(player, nullptr);
  EXPECT_EQ(log.stepped, 1u);
  EXPECT_EQ(log.drawn_items, 1u);
}

TEST(RenderScene, InspectMentionsHidden) {
  RenderScene scene = two_box_scene();
  scene.hidden_node_ids = {2};
  const std::string text = inspect_render_scene(scene);
  EXPECT_NE(text.find("hidden=1"), std::string::npos);
  EXPECT_NE(text.find("hidden_node_ids=2"), std::string::npos);
}

}  // namespace tamias
