#include "command/core/command_system.h"
#include "engine/base/executable_directory.h"
#include "engine/document/document.h"
#include "engine/io/mesh_io.h"
#include "engine/math/math.h"
#include "engine/modeling/feature/feature.h"
#include "entity/family/host/architectural/wall_entity.h"
#include "host/command_arg_text.h"
#include "plugin/plugin_host.h"
#include "plugin/plugin_manager.h"
#include "plugin/plugin_point_input_session.h"
#include "plugin/plugin_prompt_spec.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
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

TEST(PluginHost, DefaultsRibbonPlacementToHomePlugins) {
  PluginHost host;
  const HostApi& api = host.native_api();
  ASSERT_EQ(register_test_plugin(api, "demo.plugin", "Demo"), 0);
  ASSERT_EQ(api.register_command(api.context, "demo.hello", "Hello", "", nullptr,
                                 nullptr, "", 0, 0),
            0);
  ASSERT_EQ(api.register_command(api.context, "demo.legacy", "Legacy", "",
                                 "plugins", "commands", "", 0, 0),
            0);
  ASSERT_EQ(host.commands().size(), 2u);
  EXPECT_EQ(host.commands()[0].placement.page_id, "home");
  EXPECT_EQ(host.commands()[0].placement.group_id, "plugins");
  EXPECT_EQ(host.commands()[1].placement.page_id, "home");
  EXPECT_EQ(host.commands()[1].placement.group_id, "plugins");
}

TEST(PluginHost, RegisterPluginAssociatesCommands) {
  PluginHost host;
  const HostApi& api = host.native_api();
  EXPECT_EQ(api.abi_version, 8);
  ASSERT_NE(api.register_plugin, nullptr);
  ASSERT_NE(api.begin_point_input, nullptr);
  ASSERT_NE(api.cancel_point_input, nullptr);
  ASSERT_NE(api.set_selection, nullptr);
  ASSERT_NE(api.show_dialog, nullptr);
  ASSERT_NE(api.entity_feature_count, nullptr);
  ASSERT_NE(api.entity_feature_at, nullptr);
  ASSERT_NE(api.feature_input_at, nullptr);
  ASSERT_NE(api.feature_param_count, nullptr);
  ASSERT_NE(api.feature_param_at, nullptr);
  ASSERT_NE(api.begin_transaction, nullptr);
  ASSERT_NE(api.commit_transaction, nullptr);
  ASSERT_NE(api.abort_transaction, nullptr);
  ASSERT_NE(api.unregister_plugin, nullptr);
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

TEST(PluginPointInputSession, EntitiesOnlySkipsMissAndDuplicates) {
  PluginPointInputSession input;
  PluginPointInputRequest request;
  request.request_id = 9;
  request.min_points = 1;
  request.max_points = 2;
  request.flags = PluginPointInputRequest::kEntitiesOnly;

  bool called = false;
  std::vector<PluginPickPoint> completed;
  ASSERT_TRUE(input.begin(
      request, [&](std::vector<PluginPickPoint> points, bool cancelled) {
        called = true;
        EXPECT_FALSE(cancelled);
        completed = std::move(points);
      }));
  EXPECT_TRUE(input.entities_only());
  EXPECT_TRUE(input.pick_entities());
  input.add_point({{1.f, 0.f, 2.f}, 0});
  EXPECT_FALSE(called);
  input.add_point({{1.f, 0.f, 2.f}, 4});
  input.add_point({{3.f, 0.f, 4.f}, 4});
  EXPECT_FALSE(called);
  input.add_point({{5.f, 0.f, 6.f}, 7});
  EXPECT_TRUE(called);
  ASSERT_EQ(completed.size(), 2u);
  EXPECT_EQ(completed[0].entity_id, 4u);
  EXPECT_EQ(completed[1].entity_id, 7u);
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

// v7 事务：插件批量改参数时，用户按一次撤销就该全部退回。
TEST(PluginHost, TransactionBatchesDispatches) {
  CommandRegistry registry;
  register_commands(registry);
  CommandSystem system(registry);
  Document doc("plugin-tx");

  PluginHost host;
  int edits = 0;
  host.bind(&doc, &system, [&] { ++edits; });
  const HostApi& api = host.native_api();

  ASSERT_EQ(api.begin_transaction(api.context, "批量"), 0);
  for (int i = 0; i < 2; ++i) {
    const std::string args = "v:origin=" + std::to_string(i) + ",0,0;d:height=3";
    ASSERT_EQ(api.dispatch(api.context, "create_column", args.c_str()), 0);
  }
  EXPECT_EQ(doc.entities().size(), 2u);
  EXPECT_FALSE(system.can_undo()) << "事务未提交，撤销栈上不该有东西";

  ASSERT_EQ(api.commit_transaction(api.context), 0);
  EXPECT_TRUE(system.can_undo());
  system.undo();
  EXPECT_TRUE(doc.entities().empty()) << "一次撤销退回两条命令";
  EXPECT_FALSE(system.can_undo());

  // 没开事务就 commit / 嵌套 begin：报错而不是糊过去。
  EXPECT_EQ(api.commit_transaction(api.context), -1);
  EXPECT_EQ(api.abort_transaction(api.context), -1);
  ASSERT_EQ(api.begin_transaction(api.context, nullptr), 0);
  EXPECT_EQ(api.begin_transaction(api.context, "嵌套"), -1);

  // abort 返回回滚条数，并且不留撤销记录。
  ASSERT_EQ(api.dispatch(api.context, "create_column", "v:origin=0,0,0"), 0);
  EXPECT_EQ(api.abort_transaction(api.context), 1);
  EXPECT_TRUE(doc.entities().empty());
  EXPECT_FALSE(system.can_undo());
  EXPECT_GE(edits, 3);  // 每次成功 dispatch 都要让壳刷新
}

// v6 宽读：特征树 + 参数。以前插件只能猜 feature_id，现在能枚举出来。
TEST(PluginHost, ReadsFeatureTreeAndParams) {
  CommandRegistry registry;
  register_commands(registry);
  CommandSystem system(registry);

  Document doc("features");
  ASSERT_TRUE(system.dispatch(doc, "create_column",
                              {{"origin", Vec3{1.f, 0.f, 2.f}},
                               {"width", 0.5},
                               {"depth", 0.4},
                               {"height", 3.0}}));
  ASSERT_EQ(doc.entities().size(), 1u);
  const std::uint64_t entity_id = doc.entities().begin()->first;

  PluginHost host;
  host.bind(&doc, &system, {});
  const HostApi& api = host.native_api();

  ASSERT_EQ(api.entity_feature_count(api.context, entity_id), 2);

  // 第 0 条：矩形轮廓，无输入，两个参数（width / height）。
  std::uint64_t profile_id = 0;
  std::int32_t kind = -1;
  std::int32_t input_count = -1;
  std::int32_t param_count = -1;
  ASSERT_EQ(api.entity_feature_at(api.context, entity_id, 0, &profile_id, &kind,
                                  &input_count, &param_count),
            0);
  EXPECT_EQ(kind, static_cast<std::int32_t>(FeatureKind::RectProfile));
  EXPECT_EQ(input_count, 0);
  ASSERT_EQ(param_count, 2);

  // 参数按键升序（模型里是无序表，接口必须给出确定的第 N 个）。
  char name[64];
  double value = 0.0;
  ASSERT_GT(api.feature_param_at(api.context, entity_id, profile_id, 0, name, 64, &value), 0);
  EXPECT_STREQ(name, "height");
  EXPECT_DOUBLE_EQ(value, 0.4);  // 柱截面的 depth 存在轮廓的 height 参数上
  ASSERT_GT(api.feature_param_at(api.context, entity_id, profile_id, 1, name, 64, &value), 0);
  EXPECT_STREQ(name, "width");
  EXPECT_DOUBLE_EQ(value, 0.5);

  // 只要值可以不给名字缓冲。
  ASSERT_EQ(api.feature_param_at(api.context, entity_id, profile_id, 1, nullptr, 0, &value), 0);
  EXPECT_DOUBLE_EQ(value, 0.5);

  // 第 1 条：拉伸，依赖第 0 条。
  std::uint64_t extrude_id = 0;
  ASSERT_EQ(api.entity_feature_at(api.context, entity_id, 1, &extrude_id, &kind,
                                  &input_count, &param_count),
            0);
  EXPECT_EQ(kind, static_cast<std::int32_t>(FeatureKind::Extrude));
  ASSERT_EQ(input_count, 1);
  EXPECT_EQ(param_count, 1);
  std::uint64_t input_id = 0;
  ASSERT_EQ(api.feature_input_at(api.context, entity_id, extrude_id, 0, &input_id), 0);
  EXPECT_EQ(input_id, profile_id);
  ASSERT_EQ(api.feature_param_count(api.context, entity_id, extrude_id), 1);
  ASSERT_GT(api.feature_param_at(api.context, entity_id, extrude_id, 0, name, 64, &value), 0);
  EXPECT_STREQ(name, "depth");
  EXPECT_DOUBLE_EQ(value, 3.0);

  // 越界 / 不存在的 id：报错而不是崩。
  EXPECT_EQ(api.entity_feature_count(api.context, 9999), -1);
  EXPECT_EQ(api.entity_feature_count(api.context, 0), -1);
  EXPECT_EQ(api.entity_feature_at(api.context, entity_id, 2, &profile_id, &kind, &input_count,
                                  &param_count),
            -1);
  EXPECT_EQ(api.entity_feature_at(api.context, entity_id, -1, &profile_id, &kind, &input_count,
                                  &param_count),
            -1);
  EXPECT_EQ(api.feature_param_count(api.context, entity_id, 9999), -1);
  EXPECT_EQ(api.feature_param_at(api.context, entity_id, extrude_id, 5, name, 64, &value), -1);
  EXPECT_EQ(api.feature_input_at(api.context, entity_id, extrude_id, 5, &input_id), -1);

  // 读不写：枚举完特征树，文档一个字都没变。
  EXPECT_EQ(api.entity_feature_count(api.context, entity_id), 2);
  EXPECT_EQ(doc.entities().size(), 1u);
}

TEST(PluginHost, SetSelectionAndShowDialog) {
  CommandRegistry registry;
  register_commands(registry);
  CommandSystem system(registry);

  Document doc("plugin-ui");
  const auto id = add_wall_entity(doc, {0.f, 0.f, 0.f}, {4.f, 0.f, 0.f});

  PluginHost host;
  int selection_events = 0;
  host.bind(&doc, &system, {});
  host.set_selection_changed([&] { ++selection_events; });
  host.set_dialog_handler([](std::int32_t kind, std::int32_t, std::string_view spec,
                             std::string& out) {
    EXPECT_EQ(kind, 1);
    EXPECT_NE(spec.find("Rename"), std::string_view::npos);
    out = "Wall A";
    return 0;
  });

  const HostApi& api = host.native_api();
  const std::uint64_t ids[] = {id};
  ASSERT_EQ(api.set_selection(api.context, ids, 1), 0);
  EXPECT_EQ(selection_events, 1);
  ASSERT_EQ(doc.selected_ids().size(), 1u);
  EXPECT_EQ(doc.selected_ids()[0], id);
  ASSERT_EQ(api.set_selection(api.context, nullptr, 0), 0);
  EXPECT_TRUE(doc.selected_ids().empty());

  char buf[64];
  ASSERT_EQ(api.show_dialog(api.context, 1, 0, "T|Rename\nL|Name\nV|Wall", buf, 64), 0);
  EXPECT_STREQ(buf, "Wall A");
}

TEST(PluginPromptSpec, ParsesFormAndValues) {
  auto spec = parse_plugin_prompt_spec(
      "T|Create Wall\nF|n|thickness|Thickness|0.2|0.01|10\nF|s|name|Name|Wall\nF|b|snap|Snap|1\n");
  ASSERT_TRUE(spec) << spec.error();
  EXPECT_EQ(spec->title, "Create Wall");
  ASSERT_EQ(spec->fields.size(), 3u);
  EXPECT_DOUBLE_EQ(spec->fields[0].number, 0.2);
  EXPECT_EQ(spec->fields[1].text, "Wall");
  EXPECT_TRUE(spec->fields[2].flag);
  spec->fields[0].number = 0.25;
  spec->fields[1].text = "Wall A";
  spec->fields[2].flag = false;
  EXPECT_EQ(serialize_plugin_form_values(*spec), "n:thickness=0.25;s:name=Wall A;b:snap=0");
}

// 真托管宿主：加载、命令登记，再跑一次 v6 的宽读。
// 后者是唯一能证明 C# `HostApi` 结构体与 C++ 表**逐字段对齐**的检查——
// 版本号对得上只说明第一个字段没错位。
// 一个进程只在一个用例里拉 CLR：hostfxr 二次初始化会失败，拆成两个用例会静默跳过。
TEST(PluginHost, ManagedHostEndToEnd) {
  CommandRegistry registry;
  register_commands(registry);
  CommandSystem system(registry);

  Document doc("managed-features");
  std::uint64_t entity_id = 0;
  {
    ASSERT_TRUE(system.dispatch(doc, "create_column",
                                {{"origin", Vec3{0.f, 0.f, 0.f}},
                                 {"width", 0.5},
                                 {"depth", 0.4},
                                 {"height", 3.0}}));
    ASSERT_EQ(doc.entities().size(), 1u);
    entity_id = doc.entities().begin()->first;
    doc.select(entity_id);
  }

  PluginHost host;
  std::string log;
  host.set_log_sink([&log](std::string_view message) {
    log.append(message);
    log.push_back('\n');
  });
  auto loaded = host.load();
  if (!loaded) {
    GTEST_SKIP() << loaded.error();
  }
  host.bind(&doc, &system, {});
  ASSERT_FALSE(host.commands().empty()) << "Tamias.Hello should register Ribbon commands";
  ASSERT_FALSE(host.plugins().empty()) << "Tamias.Hello should register as a loaded plugin";
  bool list_selection = false;
  bool delete_selected = false;
  bool create_nurbs = false;
  bool hello_plugin = false;
  bool create_wall = false;
  bool pick_entities = false;
  bool list_features = false;
  bool widen_params = false;
  bool sample_plugin = false;
  bool sample_features = false;
  bool sample_set_depth = false;
  bool sample_where = false;
  for (const auto& plugin : host.plugins()) {
    if (plugin.id == "tamias.hello") {
      hello_plugin = true;
      EXPECT_EQ(plugin.author, "Tamias");
      EXPECT_EQ(plugin.version, "1.1.0");
      EXPECT_EQ(plugin.release_date, "2026-09-06");
      EXPECT_TRUE(plugin.built_in);
      EXPECT_FALSE(plugin.homepage_url.empty());
    }
    // 目录式扩展：plugins/Tamias.Sample.Tools（main.cs + extension.json，加载时现编译）。
    if (plugin.id == "tamias.sample.tools") {
      sample_plugin = true;
      EXPECT_EQ(plugin.title, "示例扩展（源码）");  // 清单里的 UTF-8 要原样过来
      EXPECT_EQ(plugin.author, "Tamias");
      EXPECT_EQ(plugin.version, "1.0.0");
      EXPECT_EQ(plugin.release_date, "2026-09-25");
      EXPECT_TRUE(plugin.built_in) << "来自第一个根（随 exe 发布的 plugins/）";
      EXPECT_FALSE(plugin.icon_path.empty()) << "清单里的 icon 应当解析成绝对路径";
    }
  }
  for (const auto& cmd : host.commands()) {
    list_selection = list_selection || cmd.id == "hello.list_selection";
    delete_selected = delete_selected || cmd.id == "hello.delete_selected";
    create_wall = create_wall || cmd.id == "hello.create_wall";
    pick_entities = pick_entities || cmd.id == "hello.pick_entities";
    list_features = list_features || cmd.id == "hello.list_features";
    widen_params = widen_params || cmd.id == "hello.widen_params";
    sample_features = sample_features || cmd.id == "sample.features";
    sample_set_depth = sample_set_depth || cmd.id == "sample.set_depth";
    sample_where = sample_where || cmd.id == "sample.where";
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
  EXPECT_TRUE(create_wall);
  EXPECT_TRUE(pick_entities);
  EXPECT_TRUE(list_features);
  EXPECT_TRUE(widen_params);
  EXPECT_TRUE(create_nurbs);
  EXPECT_TRUE(sample_plugin) << "目录式扩展应当被扫到并加载";
  EXPECT_TRUE(sample_features);
  EXPECT_TRUE(sample_set_depth);
  EXPECT_TRUE(sample_where);

  // 跑一下源码扩展：这一步同时证明三件事——main.cs 真的被编译了、
  // Tamias.Api 解析到了宿主那一份（否则 Entry.Load 收到的 IHost 是另一个类型）、
  // ExtensionContext.SourcePath 能告诉扩展自己从哪来。
  log.clear();
  auto where = host.invoke("sample.where");
  ASSERT_TRUE(where) << where.error();
  EXPECT_NE(log.find("Tamias.Sample.Tools"), std::string::npos) << log;

  // v6 宽读走一遍托管侧：`hello.list_features` 用 `IHost.Features` 枚举选中实体的特征树。
  log.clear();
  auto invoked = host.invoke("hello.list_features");
  ASSERT_TRUE(invoked) << invoked.error();
  EXPECT_NE(log.find("RectProfile"), std::string::npos) << log;
  EXPECT_NE(log.find("Extrude"), std::string::npos) << log;
  EXPECT_NE(log.find("width=0.5"), std::string::npos) << log;
  EXPECT_NE(log.find("depth=3"), std::string::npos) << log;

  // v7 事务走一遍托管侧：批量改参数只占**一步**撤销。
  // 读轮廓的 width（特征 0 的参数按键升序：height, width）。
  auto profile_width = [&]() -> double {
    const HostApi& a = host.native_api();
    std::uint64_t feature_id = 0;
    std::int32_t kind = 0;
    std::int32_t input_count = 0;
    std::int32_t param_count = 0;
    if (a.entity_feature_at(a.context, entity_id, 0, &feature_id, &kind, &input_count,
                            &param_count) != 0) {
      return -1.0;
    }
    char param_name[64];
    double value = 0.0;
    if (a.feature_param_at(a.context, entity_id, feature_id, 1, param_name, 64, &value) < 0) {
      return -1.0;
    }
    return value;
  };
  const double width_before = profile_width();
  ASSERT_GT(width_before, 0.0) << log;
  log.clear();
  auto widened = host.invoke("hello.widen_params");
  ASSERT_TRUE(widened) << widened.error();
  EXPECT_NEAR(profile_width(), width_before + 0.1, 1e-9) << log;
  system.undo();  // 只撤一步
  EXPECT_NEAR(profile_width(), width_before, 1e-9)
      << "整批参数应当一起退回，说明它是一条撤销记录";

  // 控制台求值（M4）：最后那个表达式的值要带回来，`host.Log` 落到同一个日志口。
  log.clear();
  auto evaluated = host.evaluate("host.Log(\"from script\"); host.Selection.Count");
  ASSERT_TRUE(evaluated) << evaluated.error();
  EXPECT_EQ(*evaluated, "1");
  EXPECT_NE(log.find("from script"), std::string::npos) << log;

  // 脚本里改参数：整段自带一个事务，同样只占一步撤销。
  const double width_now = profile_width();
  auto bumped = host.evaluate(
      "foreach (var f in host.Features(host.Selection[0]))"
      "  foreach (var p in f.Params)"
      "    host.Dispatch(\"set_param\", new CommandArgs()"
      "      .SetInt(\"entity_id\", (long)host.Selection[0])"
      "      .SetInt(\"feature_id\", (long)f.Id)"
      "      .SetString(\"param_name\", p.Name)"
      "      .SetDouble(\"value\", p.Value + 0.2));");
  ASSERT_TRUE(bumped) << bumped.error();
  EXPECT_NEAR(profile_width(), width_now + 0.2, 1e-9) << log;
  system.undo();
  EXPECT_NEAR(profile_width(), width_now, 1e-9) << "整段脚本应当只占一步撤销";

  // 语法错误：报错文本要带回来（面板上显示成错误行），不能崩。
  auto broken = host.evaluate("this is not C#");
  EXPECT_FALSE(broken);
  EXPECT_FALSE(broken.error().empty());

  // ── 扩展重载 ──────────────────────────────────────────────────────────
  // 文件监视那条路最终走的就是 reload_extensions()，这里直接在约定目录里
  // 现场造一个源码扩展，走一遍：装上 → 改内容 → 写坏 → 删掉 → 幂等。
  const auto reload_dir = executable_directory() / "plugins" / "Tamias.Test.Reload";
  const auto cleanup = [&reload_dir] {
    std::error_code ec;
    std::filesystem::remove_all(reload_dir, ec);
  };
  cleanup();
  std::filesystem::create_directories(reload_dir);

  const auto write_extension = [&reload_dir](const std::string& body) {
    {
      std::ofstream manifest(reload_dir / "extension.json",
                             std::ios::binary | std::ios::trunc);
      manifest << R"({"id":"tamias.test.reload","name":"Reload Test","version":"1.0.0"})";
    }
    std::ofstream entry(reload_dir / "main.cs", std::ios::binary | std::ios::trunc);
    // 元数据写在**代码里**：Name / Author / Version 应当盖掉清单里的，Id 也是——
    // 清单里 id 是 tamias.test.reload，代码里故意写成另一个，验证两件事：
    // ① 代码优先（和预编译扩展同一条规则）② 摘的时候按真正登记的 id 摘干净。
    entry << "using Tamias.Api;\npublic static class Entry {\n"
          << "  public static PluginMetadata Metadata => new() {\n"
          << "    Id = \"tamias.test.codeid\", Name = \"Code Name\",\n"
          << "    Author = \"Code Author\", Version = \"9.9.9\" };\n"
          << "  public static void Load(IHost host) {\n"
          << body << "\n  }\n}\n";
  };
  const auto has_command = [&host](const char* id) {
    for (const auto& cmd : host.commands()) {
      if (cmd.id == id) {
        return true;
      }
    }
    return false;
  };
  const auto has_plugin = [&host](const char* id) {
    for (const auto& plugin : host.plugins()) {
      if (plugin.id == id) {
        return true;
      }
    }
    return false;
  };

  write_extension(R"(host.AddCommand("reloadtest.one", "One", () => host.Log("one"));)");
  log.clear();
  auto first = host.reload_extensions();
  ASSERT_TRUE(first) << first.error();
  EXPECT_FALSE(first->empty());
  EXPECT_TRUE(has_command("reloadtest.one")) << *first;
  // 代码里的 Id 优先于清单：装上去的是 tamias.test.codeid，清单那个不该出现。
  EXPECT_TRUE(has_plugin("tamias.test.codeid")) << *first;
  EXPECT_FALSE(has_plugin("tamias.test.reload")) << "清单里的 id 不该盖过代码：" << *first;
  for (const auto& plugin : host.plugins()) {
    if (plugin.id == "tamias.test.codeid") {
      EXPECT_EQ(plugin.title, "Code Name") << "名字/作者/版本应当以代码里的为准";
      EXPECT_EQ(plugin.author, "Code Author");
      EXPECT_EQ(plugin.version, "9.9.9");
    }
  }
  EXPECT_NE(log.find("code declares Id"), std::string::npos)
      << "代码 id 与目录/清单不一致时应当留一条日志：" << log;

  // 改内容：旧命令必须消失（说明旧登记摘干净了），新命令出现。
  write_extension(R"(host.AddCommand("reloadtest.two", "Two", () => host.Log("two"));)");
  auto second = host.reload_extensions();
  ASSERT_TRUE(second) << second.error();
  EXPECT_TRUE(has_command("reloadtest.two")) << *second;
  EXPECT_FALSE(has_command("reloadtest.one")) << "旧登记没摘干净：" << *second;

  // 写坏：编译不过时**旧版本继续用**——保存到一半最需要这个。
  {
    std::ofstream entry(reload_dir / "main.cs", std::ios::binary | std::ios::trunc);
    entry << "this is not C#\n";
  }
  auto failed = host.reload_extensions();
  ASSERT_TRUE(failed) << failed.error();
  EXPECT_NE(failed->find("failed"), std::string::npos) << *failed;
  EXPECT_TRUE(has_command("reloadtest.two")) << "编译失败不该把还在跑的旧版本干掉：" << *failed;

  // 目录删掉：扩展连同它的命令一起消失。
  cleanup();
  auto removed = host.reload_extensions();
  ASSERT_TRUE(removed) << removed.error();
  EXPECT_FALSE(has_command("reloadtest.two")) << *removed;
  EXPECT_FALSE(has_plugin("tamias.test.codeid")) << "摘的时候要按真正登记的 id 摘：" << *removed;

  // 幂等：什么都没变就不该有摘要（壳据此决定要不要重建 Ribbon）。
  auto idle = host.reload_extensions();
  ASSERT_TRUE(idle) << idle.error();
  EXPECT_TRUE(idle->empty()) << "没变化却报了：" << *idle;

  // ── loader.cs：总入口 ─────────────────────────────────────────────────
  // 约定：根顶层的 loader.cs 会被执行，里面用 host.LoadExtension(path) 指向**任何地方**
  // 的工程。这里现场造一个约定目录之外的工程，走一遍装上 → 改它 → 摘掉的完整循环。
  const auto loader_file = executable_directory() / "plugins" / "loader.cs";
  const auto project_dir = std::filesystem::temp_directory_path() / "tamias-loader-project";
  const auto cleanup_loader = [&] {
    std::error_code ec;
    std::filesystem::remove(loader_file, ec);
    std::filesystem::remove_all(project_dir, ec);
  };
  cleanup_loader();
  std::filesystem::create_directories(project_dir);

  const auto write_project = [&project_dir](const std::string& command_id) {
    std::ofstream entry(project_dir / "main.cs", std::ios::binary | std::ios::trunc);
    entry << "using Tamias.Api;\npublic static class Entry {\n"
          << "  public static void Load(IHost host) {\n"
          << "    host.AddCommand(\"" << command_id << "\", \"Loader\", () => host.Log(\"loader\"));\n"
          << "  }\n}\n";
  };
  {
    // 路径写成正斜杠，省得在 C# 字符串里转义反斜杠。
    std::ofstream loader(loader_file, std::ios::binary | std::ios::trunc);
    loader << "using Tamias.Api;\npublic static class Entry {\n"
           << "  public static void Load(IHost host) {\n"
           << "    host.LoadExtension(\"" << project_dir.generic_string() << "\");\n"
           << "  }\n}\n";
  }
  write_project("loadertest.project");

  auto loader_added = host.reload_extensions();
  ASSERT_TRUE(loader_added) << loader_added.error();
  // loader 的 id 按根区分（<根目录名>.loader）：官方根和用户根各一个时不会互相覆盖。
  EXPECT_TRUE(has_plugin("plugins.loader")) << *loader_added;
  EXPECT_TRUE(has_command("loadertest.project"))
      << "loader 应当把约定目录之外的工程装进来：" << *loader_added;
  // 声明的根要回传给 C++ 侧（文件监视靠它盯那个工程目录）。
  bool watches_project = false;
  for (const auto& root : host.extension_roots()) {
    watches_project = watches_project || root == project_dir;
  }
  EXPECT_TRUE(watches_project) << "LoadExtension 登记的根应当进 extension_roots()";

  // 改**外部工程**的文件就该生效：它跟着重扫走，不必碰 loader.cs。
  write_project("loadertest.project2");
  auto project_changed = host.reload_extensions();
  ASSERT_TRUE(project_changed) << project_changed.error();
  EXPECT_TRUE(has_command("loadertest.project2")) << *project_changed;
  EXPECT_FALSE(has_command("loadertest.project")) << "旧登记没摘干净：" << *project_changed;

  // 删掉 loader.cs：总入口连同它装的东西一起摘掉。
  cleanup_loader();
  auto loader_removed = host.reload_extensions();
  ASSERT_TRUE(loader_removed) << loader_removed.error();
  EXPECT_FALSE(has_plugin("plugins.loader")) << *loader_removed;
  EXPECT_FALSE(has_command("loadertest.project2")) << *loader_removed;

  // 指到"编译型工程的源码目录"（有 .cs，但没有 main.cs / 清单 / dll）不该静默什么都不做：
  // 那种工程的入口是它的 **publish 输出**，得给一句照着改的提示。顺带走一遍控制台怎么调。
  const auto project_source_dir = std::filesystem::temp_directory_path() / "tamias-loader-csproj";
  std::error_code ec;
  std::filesystem::remove_all(project_source_dir, ec);
  std::filesystem::create_directories(project_source_dir);
  {
    std::ofstream source(project_source_dir / "MyCompany.Plugin.cs", std::ios::binary | std::ios::trunc);
    source << "public sealed class MyCompanyPlugin { }\n";
  }
  log.clear();
  auto from_console = host.evaluate(
      "host.LoadExtension(@\"" + project_source_dir.generic_string() + "\");");
  ASSERT_TRUE(from_console) << from_console.error();
  EXPECT_NE(log.find("Point at a source extension's entry file"), std::string::npos)
      << "指到工程源码目录应当提示去指它的输出：" << log;

  // 指入口**文件**本身也认：这时它所在目录就是扩展目录。改这个文件保存同样重载。
  const auto single_file = project_source_dir / "MyCompany.Entry.cs";
  {
    std::ofstream source(single_file, std::ios::binary | std::ios::trunc);
    source << "using Tamias.Api;\npublic static class Entry {\n"
           << "  public static void Load(IHost host) {\n"
           << "    host.AddCommand(\"loadertest.file\", \"File\", () => host.Log(\"file\"));\n"
           << "  }\n}\n";
    // 指文件也要读同目录的清单：否则"指文件"和"指目录"会得到两个不同的 id。
    std::ofstream manifest(project_source_dir / "extension.json", std::ios::binary | std::ios::trunc);
    manifest << R"({"id":"tamias.test.single","name":"Single File","version":"2.0.0"})";
  }
  log.clear();
  auto file_loaded = host.evaluate(
      "host.LoadExtension(@\"" + single_file.generic_string() + "\");");
  ASSERT_TRUE(file_loaded) << file_loaded.error();
  EXPECT_TRUE(has_command("loadertest.file")) << "应当能直接指入口文件：" << log;
  EXPECT_TRUE(has_plugin("tamias.test.single"))
      << "指入口文件时也要认同目录的 extension.json：" << log;

  std::filesystem::remove_all(project_source_dir, ec);
  auto file_removed = host.reload_extensions();
  ASSERT_TRUE(file_removed) << file_removed.error();
  EXPECT_FALSE(has_command("loadertest.file")) << "文件没了，扩展也该摘掉：" << *file_removed;
}

// 没有 .NET 时控制台不该崩，只是求值不可用。
TEST(PluginHost, EvaluateWithoutHostIsAnError) {
  PluginHost host;
  auto result = host.evaluate("1 + 1");
  EXPECT_FALSE(result);
  EXPECT_NE(result.error().find("C# host"), std::string::npos) << result.error();
}
