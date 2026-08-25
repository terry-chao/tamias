#include "command/command_system.h"
#include "engine/document/document.h"
#include "engine/io/mesh_io.h"
#include "engine/math/math.h"
#include "entity/wall_entity.h"
#include "host/command_arg_text.h"
#include "plugin/plugin_host.h"
#include "plugin/plugin_manager.h"
#include "plugin/plugin_point_input_session.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
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

std::int32_t register_test_plugin(const HostApi& api, const char* id,
                                  const char* title) {
  return api.register_plugin(api.context, id, title, "Test Author", "1.2.3",
                             "2026-08-25", "Test plugin",
                             "https://example.com", "plugin.svg", 0);
}

}  // namespace

TEST(CommandArgText, ParsesTypedAndInferredValues) {
  auto args = parse_command_arg_text(
      "i:entity_id=7;d:radius=0.25;s:name=wall;v:origin=1,2,3;"
      "p:points=1,2,3|4,5,6;a:weights=1|2.5");
  ASSERT_TRUE(args) << args.error();
  EXPECT_EQ(std::get<std::int64_t>((*args)["entity_id"]), 7);
  EXPECT_DOUBLE_EQ(std::get<double>((*args)["radius"]), 0.25);
  EXPECT_EQ(std::get<std::string>((*args)["name"]), "wall");
  const auto origin = std::get<Vec3>((*args)["origin"]);
  EXPECT_FLOAT_EQ(origin.x, 1.f);
  EXPECT_FLOAT_EQ(origin.y, 2.f);
  EXPECT_FLOAT_EQ(origin.z, 3.f);
  const auto points = std::get<std::vector<Vec3>>((*args)["points"]);
  ASSERT_EQ(points.size(), 2u);
  EXPECT_FLOAT_EQ(points[1].x, 4.f);
  const auto weights = std::get<std::vector<double>>((*args)["weights"]);
  ASSERT_EQ(weights.size(), 2u);
  EXPECT_DOUBLE_EQ(weights[1], 2.5);

  auto inferred = parse_command_arg_text("entity_id=4;radius=1.5");
  ASSERT_TRUE(inferred) << inferred.error();
  EXPECT_EQ(std::get<std::int64_t>((*inferred)["entity_id"]), 4);
  EXPECT_DOUBLE_EQ(std::get<double>((*inferred)["radius"]), 1.5);
}

TEST(PluginHost, RegisterPluginAssociatesCommands) {
  PluginHost host;
  const HostApi& api = host.native_api();
  EXPECT_EQ(api.abi_version, 4);
  ASSERT_NE(api.register_plugin, nullptr);
  ASSERT_NE(api.begin_point_input, nullptr);
  ASSERT_NE(api.cancel_point_input, nullptr);
  ASSERT_EQ(register_test_plugin(api, "demo.plugin", "Demo"), 0);
  ASSERT_EQ(api.register_command(api.context, "demo.hello", "Hello", "tip",
                                 "home", "draw", "demo.svg", 42, 1),
            0);
  ASSERT_EQ(register_test_plugin(api, "demo.plugin", "Demo"), -1);

  ASSERT_EQ(host.plugins().size(), 1u);
  EXPECT_EQ(host.plugins()[0].id, "demo.plugin");
  EXPECT_EQ(host.plugins()[0].title, "Demo");
  EXPECT_EQ(host.plugins()[0].author, "Test Author");
  EXPECT_EQ(host.plugins()[0].version, "1.2.3");
  EXPECT_EQ(host.plugins()[0].release_date, "2026-08-25");
  EXPECT_EQ(host.plugins()[0].description, "Test plugin");
  EXPECT_EQ(host.plugins()[0].homepage_url, "https://example.com");
  EXPECT_EQ(host.plugins()[0].icon_path, "plugin.svg");
  EXPECT_FALSE(host.plugins()[0].built_in);
  ASSERT_EQ(host.commands().size(), 1u);
  EXPECT_EQ(host.commands()[0].id, "demo.hello");
  EXPECT_EQ(host.commands()[0].plugin_id, "demo.plugin");
  EXPECT_EQ(host.commands()[0].placement.page_id, "home");
  EXPECT_EQ(host.commands()[0].placement.group_id, "draw");
  EXPECT_EQ(host.commands()[0].placement.icon_path, "demo.svg");
  EXPECT_EQ(host.commands()[0].placement.order, 42);
  EXPECT_TRUE(host.commands()[0].placement.checkable);
}

TEST(PluginManager, HidesLoadedPluginUntilApplied) {
  PluginHost host;
  const HostApi& api = host.native_api();
  ASSERT_EQ(register_test_plugin(api, "a", "A"), 0);
  ASSERT_EQ(register_test_plugin(api, "b", "B"), 0);

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

TEST(PluginManager, OrdersCommandsWithinRibbonLocationAndKeepsUnknownIds) {
  PluginHost host;
  const HostApi& api = host.native_api();
  ASSERT_EQ(register_test_plugin(api, "a", "A"), 0);
  ASSERT_EQ(api.register_command(api.context, "a.first", "First", "",
                                 "home", "draw", "", 10, 0),
            0);
  ASSERT_EQ(register_test_plugin(api, "b", "B"), 0);
  ASSERT_EQ(api.register_command(api.context, "b.second", "Second", "",
                                 "home", "draw", "", 20, 0),
            0);
  ASSERT_EQ(api.register_command(api.context, "b.other", "Other", "",
                                 "view", "display", "", 0, 0),
            0);

  PluginManager manager(host);
  manager.set_command_order({"missing.command", "b.second", "a.first"});
  auto draw = manager.ordered_commands("home", "draw");
  ASSERT_EQ(draw.size(), 2u);
  EXPECT_EQ(draw[0]->id, "b.second");
  EXPECT_EQ(draw[1]->id, "a.first");

  manager.commit_command_order_among_loaded({"a.first", "b.second",
                                              "b.other"});
  ASSERT_EQ(manager.command_order().size(), 4u);
  EXPECT_EQ(manager.command_order()[0], "a.first");
  EXPECT_EQ(manager.command_order()[1], "b.second");
  EXPECT_EQ(manager.command_order()[2], "b.other");
  EXPECT_EQ(manager.command_order()[3], "missing.command");
}

TEST(PluginPointInputSession, ConfirmsAndCancelsWithoutQt) {
  PluginPointInputSession input;
  PluginPointInputRequest request;
  request.request_id = 7;
  request.min_points = 2;
  request.max_points = 0;
  request.flags = PluginPointInputRequest::kAllowConfirm |
                  PluginPointInputRequest::kGridSnap;

  bool called = false;
  bool cancelled = false;
  std::vector<PluginPickPoint> completed;
  ASSERT_TRUE(input.begin(
      request, [&](std::vector<PluginPickPoint> points, bool was_cancelled) {
        called = true;
        cancelled = was_cancelled;
        completed = std::move(points);
      }));
  EXPECT_TRUE(input.grid_snap());
  input.add_point({{1.f, 0.f, 2.f}, 3});
  input.confirm();
  EXPECT_FALSE(called);
  input.add_point({{4.f, 0.f, 5.f}, 6});
  input.confirm();
  EXPECT_TRUE(called);
  EXPECT_FALSE(cancelled);
  ASSERT_EQ(completed.size(), 2u);
  EXPECT_EQ(completed[1].entity_id, 6u);

  called = false;
  ASSERT_TRUE(input.begin(
      request, [&](std::vector<PluginPickPoint>, bool was_cancelled) {
        called = true;
        cancelled = was_cancelled;
      }));
  input.cancel(8);
  EXPECT_FALSE(called);
  input.cancel(7);
  EXPECT_TRUE(called);
  EXPECT_TRUE(cancelled);
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
  bool create_nurbs = false;
  bool hello_plugin = false;
  for (const auto& plugin : host.plugins()) {
    if (plugin.id == "tamias.hello") {
      hello_plugin = true;
      EXPECT_EQ(plugin.author, "Tamias");
      EXPECT_EQ(plugin.version, "1.0.0");
      EXPECT_EQ(plugin.release_date, "2026-08-25");
      EXPECT_TRUE(plugin.built_in);
      EXPECT_FALSE(plugin.homepage_url.empty());
    }
  }
  for (const auto& cmd : host.commands()) {
    list_selection = list_selection || cmd.id == "hello.list_selection";
    delete_selected = delete_selected || cmd.id == "hello.delete_selected";
    if (cmd.id == "tamias.nurbs.create") {
      create_nurbs = true;
      EXPECT_EQ(cmd.placement.page_id, "home");
      EXPECT_EQ(cmd.placement.group_id, "draw");
      EXPECT_TRUE(cmd.placement.checkable);
    }
    EXPECT_FALSE(cmd.plugin_id.empty());
  }
  EXPECT_TRUE(hello_plugin);
  EXPECT_TRUE(list_selection);
  EXPECT_TRUE(delete_selected);
  EXPECT_TRUE(create_nurbs);
}
