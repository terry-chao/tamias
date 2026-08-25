#include "command/command_system.h"
#include "engine/document/document.h"
#include "engine/io/mesh_io.h"
#include "engine/math/math.h"
#include "entity/wall_entity.h"
#include "host/command_arg_text.h"
#include "plugin/plugin_host.h"
#include "plugin/plugin_manager.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>
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

TEST(PluginHost, RegisterPluginAssociatesCommands) {
  PluginHost host;
  const HostApi& api = host.native_api();
  ASSERT_NE(api.register_plugin, nullptr);
  ASSERT_EQ(api.register_plugin(api.context, "demo.plugin", "Demo"), 0);
  ASSERT_EQ(api.register_command(api.context, "demo.hello", "Hello", "tip"), 0);
  ASSERT_EQ(api.register_plugin(api.context, "demo.plugin", "Demo"), -1);

  ASSERT_EQ(host.plugins().size(), 1u);
  EXPECT_EQ(host.plugins()[0].id, "demo.plugin");
  EXPECT_EQ(host.plugins()[0].title, "Demo");
  ASSERT_EQ(host.commands().size(), 1u);
  EXPECT_EQ(host.commands()[0].id, "demo.hello");
  EXPECT_EQ(host.commands()[0].plugin_id, "demo.plugin");
}

TEST(PluginManager, HidesLoadedPluginUntilApplied) {
  PluginHost host;
  const HostApi& api = host.native_api();
  ASSERT_EQ(api.register_plugin(api.context, "a", "A"), 0);
  ASSERT_EQ(api.register_plugin(api.context, "b", "B"), 0);

  PluginManager manager(host);
  EXPECT_TRUE(manager.is_visible("a"));
  EXPECT_TRUE(manager.is_visible("b"));

  manager.commit_hidden_among_loaded({"a"});
  EXPECT_FALSE(manager.is_visible("a"));
  EXPECT_TRUE(manager.is_visible("b"));

  manager.set_hidden_ids({"gone"});
  manager.commit_hidden_among_loaded({"b"});
  EXPECT_TRUE(manager.is_visible("a"));
  EXPECT_FALSE(manager.is_visible("b"));
  EXPECT_EQ(manager.hidden_ids().count("gone"), 1u);
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
  ASSERT_FALSE(host.plugins().empty()) << "Tamias.Hello should register as a loaded plugin";
  bool list_selection = false;
  bool delete_selected = false;
  bool hello_plugin = false;
  for (const auto& plugin : host.plugins()) {
    hello_plugin = hello_plugin || plugin.id.find("HelloPlugin") != std::string::npos;
  }
  for (const auto& cmd : host.commands()) {
    list_selection = list_selection || cmd.id == "hello.list_selection";
    delete_selected = delete_selected || cmd.id == "hello.delete_selected";
    EXPECT_FALSE(cmd.plugin_id.empty());
  }
  EXPECT_TRUE(hello_plugin);
  EXPECT_TRUE(list_selection);
  EXPECT_TRUE(delete_selected);
}
