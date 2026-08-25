#include "command/command_system.h"
#include "engine/document/document.h"
#include "engine/io/mesh_io.h"
#include "engine/math/math.h"
#include "entity/wall_entity.h"
#include "host/command_arg_text.h"
#include "plugin/plugin_host.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

using namespace tamias;

namespace {

std::uint64_t add_wall_entity(Document& doc, Vec3 start, Vec3 end) {
  auto wall = std::make_unique<WallEntity>(start, end, 0.2, 3.0);
  MeshAsset mesh_asset{};
  mesh_asset.name = wall->name;
  mesh_asset.cpu = make_demo_cube();
  auto& stored_mesh = doc.add_mesh(std::move(mesh_asset));
  wall->mesh_asset_id = stored_mesh.id;

  SceneNode node{};
  node.name = wall->name;
  node.mesh_asset_id = wall->mesh_asset_id;
  node.local_transform = wall->local_transform;
  SceneNode& stored_node = doc.scene().add_node(std::move(node));
  wall->id = stored_node.id;

  const std::uint64_t id = wall->id;
  doc.insert_entity(std::move(wall));
  doc.recompute_scene();
  return id;
}

}  // namespace

TEST(CommandArgText, ParsesTypedAndInferredValues) {
  auto args = parse_command_arg_text("i:entity_id=7;d:radius=0.25;s:name=wall;v:origin=1,2,3");
  ASSERT_TRUE(args) << args.error();
  EXPECT_EQ(std::get<std::int64_t>((*args)["entity_id"]), 7);
  EXPECT_DOUBLE_EQ(std::get<double>((*args)["radius"]), 0.25);
  EXPECT_EQ(std::get<std::string>((*args)["name"]), "wall");
  const auto origin = std::get<Vec3>((*args)["origin"]);
  EXPECT_FLOAT_EQ(origin.x, 1.f);
  EXPECT_FLOAT_EQ(origin.y, 2.f);
  EXPECT_FLOAT_EQ(origin.z, 3.f);

  auto inferred = parse_command_arg_text("entity_id=4;radius=1.5");
  ASSERT_TRUE(inferred) << inferred.error();
  EXPECT_EQ(std::get<std::int64_t>((*inferred)["entity_id"]), 4);
  EXPECT_DOUBLE_EQ(std::get<double>((*inferred)["radius"]), 1.5);
}

TEST(PluginHost, HostApiSelectionAndDispatch) {
  CommandRegistry registry;
  register_commands(registry);
  CommandSystem system(registry);

  Document doc("plugin");
  const auto id = add_wall_entity(doc, {0.f, 0.f, 0.f}, {4.f, 0.f, 0.f});
  doc.select(id);
  ASSERT_EQ(doc.entities().size(), 1u);
  ASSERT_EQ(doc.selected_ids().size(), 1u);

  PluginHost host;
  int edits = 0;
  host.bind(&doc, &system, [&] { ++edits; });

  const HostApi& api = host.native_api();
  char name[64];
  ASSERT_GT(api.document_name(api.context, name, 64), 0);
  EXPECT_STREQ(name, "plugin");
  EXPECT_EQ(api.entity_count(api.context), 1);
  EXPECT_EQ(api.selection_count(api.context), 1);

  std::uint64_t selected = 0;
  ASSERT_EQ(api.selection_id_at(api.context, 0, &selected), 0);
  EXPECT_EQ(selected, id);

  char kind[32];
  ASSERT_GT(api.entity_kind(api.context, id, kind, 32), 0);
  EXPECT_STREQ(kind, "Wall");

  const std::string args = "i:entity_id=" + std::to_string(id);
  ASSERT_EQ(api.dispatch(api.context, "delete_entity", args.c_str()), 0);
  EXPECT_EQ(edits, 1);
  EXPECT_TRUE(doc.entities().empty());
}

TEST(PluginHost, LoadsManagedHelloCommands) {
  PluginHost host;
  auto loaded = host.load();
  if (!loaded) {
    GTEST_SKIP() << loaded.error();
  }
  ASSERT_FALSE(host.commands().empty()) << "Tamias.Hello should register Ribbon commands";
  bool list_selection = false;
  bool delete_selected = false;
  for (const auto& cmd : host.commands()) {
    list_selection = list_selection || cmd.id == "hello.list_selection";
    delete_selected = delete_selected || cmd.id == "hello.delete_selected";
  }
  EXPECT_TRUE(list_selection);
  EXPECT_TRUE(delete_selected);
}
