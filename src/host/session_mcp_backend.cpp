#include "host/session_mcp_backend.h"

#include "engine/modeling/feature/feature.h"
#include "host/command_arg_json.h"
#include "host/command_arg_text.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <string>
#include <utility>

namespace tamias {
namespace {

using mcp::Json;

[[nodiscard]] std::string ascii_lower(std::string text) {
  for (char& ch : text) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return text;
}

[[nodiscard]] mcp::McpToolResult ok(Json payload) {
  return mcp::McpToolResult{false, payload.dump()};
}

[[nodiscard]] mcp::McpToolResult failure(std::string message) {
  return mcp::McpToolResult{true, std::move(message)};
}

[[nodiscard]] std::optional<std::int64_t> opt_int(const Json& args, std::string_view key) {
  const Json* value = args.find(key);
  if (value == nullptr || !value->is_number()) {
    return std::nullopt;
  }
  return value->as_int();
}

[[nodiscard]] std::optional<double> opt_double(const Json& args, std::string_view key) {
  const Json* value = args.find(key);
  if (value == nullptr || !value->is_number()) {
    return std::nullopt;
  }
  return value->as_double();
}

// 写工具名单只有这一处：tools() 用它打标记，call_tool 用它决定是否刷新视口。
[[nodiscard]] bool tool_mutates(std::string_view name) {
  return name == "tamias_dispatch" || name == "tamias_set_param" ||
         name == "tamias_create_wall" || name == "tamias_move_entities" ||
         name == "tamias_delete_entity" || name == "tamias_set_selection" ||
         name == "tamias_batch" || name == "tamias_undo" || name == "tamias_redo" ||
         name == "tamias_evaluate";
}

[[nodiscard]] Json ids_json(const std::vector<std::uint64_t>& ids) {
  Json array = Json::array();
  for (const std::uint64_t id : ids) {
    array.push_back(Json::integer(static_cast<std::int64_t>(id)));
  }
  return array;
}

// 命令目录：模型最容易犯的错是编命令名与参数名，所以把这张表当资源发出去。
// 现在手写维护，待拍板项：构建期从 command/core/register_commands.cpp 生成
//（见 docs/DECISION-AI-INTEGRATION.md §9）。
[[nodiscard]] Json command_catalog() {
  struct Entry {
    const char* name;
    const char* params;
    const char* note;
  };
  static constexpr Entry kCommands[] = {
      {"delete_entity", "entity_id", "删一个实体"},
      {"set_param", "entity_id, feature_id, param_name, value", "改特征参数并重算几何"},
      {"fillet", "entity_id, radius=0.1, edge=0", "追加圆角特征"},
      {"chamfer", "entity_id, distance=0.1, edge=0", "追加倒角特征"},
      {"boolean", "a, b, operation(0=Fuse/1=Common/2=Cut)", "布尔运算"},
      {"set_material", "entity_id, material_id/name/base_color/roughness/...", "赋材质（只写不可读）"},
      {"create_storey", "name=Storey, elevation=0", "新建楼层"},
      {"set_location", "entity_id, storey_id, elevation_offset", "把实体放到某楼层"},
      {"create_wall", "points(2), thickness=0.2, height=3.0, leaf", "给齐点立即建墙，否则武装工具"},
      {"create_structural_wall", "points(2), thickness=0.3, height=3.0", "结构墙"},
      {"create_curtain_wall", "points(2), thickness=0.15, height=3.0", "幕墙"},
      {"create_beam", "points(2), width=0.3, depth=0.5, sub_type", "梁；T/I 断面只支持交互式"},
      {"create_slab", "points(2), thickness=0.2, elevation", "楼板"},
      {"create_column", "points(1)/origin, sub_type(rect/circle), width/depth/diameter, height", "柱"},
      {"create_columns_on_grid", "sub_type, width/depth/diameter, height, axis_ids", "轴网交点批量布柱"},
      {"create_foundation", "points(1)/origin, sub_type, length/width/height/diameter", "基础"},
      {"create_door", "points(1)/origin, width=1.0, height=2.1, thickness, sill, host_id", "门"},
      {"create_window", "points(1)/origin, width=1.2, height=1.2, thickness, sill, host_id", "窗"},
      {"create_line", "points(2)", "直线"},
      {"create_polyline", "points(>=2)", "折线"},
      {"create_circle", "points(2: 圆心+半径点)", "圆"},
      {"create_arc", "points(3: 起点/经过/终点)", "圆弧"},
      {"create_rectangle", "points(2: 对角点)", "矩形"},
      {"create_curve", "curve_kind(line/polyline/bezier/bspline/nurbs), points, weights, degree", "通用曲线"},
      {"move_entities", "ids/entity_id + delta 或 points(2)", "平移"},
      {"copy_entities", "ids/entity_id + delta 或 points(2)", "复制"},
      {"rotate_entities", "ids/entity_id, angle(度), center", "绕 Y 轴旋转"},
      {"mirror_entities", "ids/entity_id, points(2: 镜面线)", "镜像"},
      {"array_entities", "ids/entity_id, mode(linear/polar), count, direction/spacing 或 center/step_angle", "阵列"},
      {"copy_storey", "source_storey_id, target_storey_id(s)", "整层复制"},
      {"create_grid_axis", "name, direction(x/z), position, start, end", "单根轴线"},
      {"auto_grid", "origin_x, origin_z, x_spacings, z_spacings, margin=1.0", "按间距生成正交轴网"},
      {"create_text", "kind, text, position, size_px=14, color, align", "文字注记"},
      {"set_selection", "ids", "写选择（不是命令，AI 应使用 tamias_set_selection 工具）"},
  };

  Json commands = Json::array();
  for (const Entry& entry : kCommands) {
    commands.push_back(Json::object({
        {"name", Json::string(entry.name)},
        {"params", Json::string(entry.params)},
        {"note", Json::string(entry.note)},
    }));
  }
  return commands;
}

}  // namespace

std::vector<mcp::McpTool> SessionMcpBackend::tools() const {
  using mcp::schema::Property;
  std::vector<mcp::McpTool> tools;

  tools.push_back(mcp::McpTool{
      "tamias_document_info", "当前文档摘要：名称、实体数、按种类统计、选择数。",
      mcp::schema::object("无参数", {})});

  tools.push_back(mcp::McpTool{
      "tamias_list_entities",
      "分页列出实体 (id, kind, name)。默认最多 100 条，用 offset 翻页。",
      mcp::schema::object(
          "过滤与分页",
          {Property{"kind", mcp::schema::string("按 EntityKind 过滤，如 Wall / Column")},
           Property{"nameContains", mcp::schema::string("名字包含该子串（不区分大小写）")},
           Property{"offset", mcp::schema::integer("从第几条开始，默认 0")},
           Property{"limit", mcp::schema::integer("返回条数，默认 100，最大 1000")}})});

  tools.push_back(mcp::McpTool{
      "tamias_get_selection", "当前选中的实体 id（按选择顺序）。",
      mcp::schema::object("无参数", {})});

  tools.push_back(mcp::McpTool{
      "tamias_get_features",
      "某个实体的特征树：id、种类、依赖边、参数（按名字排序）。改参数前先读它。",
      mcp::schema::object("实体 id", {Property{"entityId", mcp::schema::integer("实体 id")}},
                          {"entityId"})});

  tools.push_back(mcp::McpTool{
      "tamias_dispatch",
      "通用写入口：按名字发一条内核命令。命令目录见资源 tamias://commands。"
      "参数用结构化 JSON：整数→i:、浮点→d:、字符串→s:、{x,y,z}→v:、"
      "[{x,y,z}]→p:（点列）、[数字]→a:（数组）。参数给不齐的创建类命令只会"
      "武装工具，等用户在视口点。",
      mcp::schema::object(
          "命令名 + 参数",
          {Property{"command", mcp::schema::string("命令名，如 create_wall")},
           Property{"args", mcp::schema::freeform("结构化参数对象，可省略")}},
          {"command"})});

  tools.push_back(mcp::McpTool{
      "tamias_set_param", "改某个特征的数值参数并重算几何（等价 set_param 命令）。",
      mcp::schema::object(
          "目标特征参数",
          {Property{"entityId", mcp::schema::integer("实体 id")},
           Property{"featureId", mcp::schema::integer("特征 id")},
           Property{"paramName", mcp::schema::string("参数名，如 depth")},
           Property{"value", mcp::schema::number("新值")}},
          {"entityId", "featureId", "paramName", "value"})});

  tools.push_back(mcp::McpTool{
      "tamias_create_wall",
      "建墙。给 points 或 start+end 就立即创建；都不给则武装墙工具等用户点两个点。",
      mcp::schema::object(
          "墙的几何与尺寸",
          {Property{"points", mcp::schema::array("两个点", mcp::schema::freeform("{x,y,z}"))},
           Property{"start", mcp::schema::freeform("起点 {x,y,z}")},
           Property{"end", mcp::schema::freeform("终点 {x,y,z}")},
           Property{"thickness", mcp::schema::number("厚度，默认 0.2")},
           Property{"height", mcp::schema::number("高度，默认 3.0")},
           Property{"leaf", mcp::schema::number(">0 = 空心墙")}})});

  tools.push_back(mcp::McpTool{
      "tamias_move_entities",
      "平移实体。目标取 ids → entityId → 当前选择；位移给 delta 或 points(2)。",
      mcp::schema::object(
          "目标与位移",
          {Property{"ids", mcp::schema::array("实体 id 数组", mcp::schema::integer("id"))},
           Property{"entityId", mcp::schema::integer("单个实体 id")},
           Property{"delta", mcp::schema::freeform("位移 {x,y,z}")},
           Property{"points", mcp::schema::array("两个点表示位移", mcp::schema::freeform("{x,y,z}"))}})});

  tools.push_back(mcp::McpTool{
      "tamias_delete_entity", "删除一个实体（可撤销）。",
      mcp::schema::object("实体 id", {Property{"entityId", mcp::schema::integer("实体 id")}},
                          {"entityId"})});

  tools.push_back(mcp::McpTool{
      "tamias_set_selection", "写入选择集（只改选择，不改文档，不进撤销栈）。",
      mcp::schema::object(
          "要选中的 id",
          {Property{"ids", mcp::schema::array("实体 id 数组，空数组 = 清空选择",
                                              mcp::schema::integer("id"))}},
          {"ids"})});

  tools.push_back(mcp::McpTool{
      "tamias_batch",
      "一组命令合成一个事务：成功失败都只占一步撤销（失败整体回滚）。"
      "注意：事务里不能放点没给齐的交互式命令。",
      mcp::schema::object(
          "操作列表",
          {Property{"operations",
                    mcp::schema::array(
                        "每项 {command, args?}",
                        mcp::schema::object(
                            "一条命令",
                            {Property{"command", mcp::schema::string("命令名")},
                             Property{"args", mcp::schema::freeform("参数对象，可省略")}},
                            {"command"}))}},
          {"operations"})});

  tools.push_back(mcp::McpTool{
      "tamias_undo", "撤销一步（受用户 Ctrl+Z 的同一条撤销栈控制）。",
      mcp::schema::object("无参数", {})});
  tools.push_back(mcp::McpTool{
      "tamias_redo", "重做一步。", mcp::schema::object("无参数", {})});

  // 逃生舱：名单里有它，模型就可能用；所以只在壳明确打开时才列出来。
  if (evaluate_) {
    tools.push_back(mcp::McpTool{
        "tamias_evaluate",
        "在 Tamias 进程里直接求值一段 C#（全信任：能读文档、发命令、弹对话框）。"
        "能用结构化工具就别用它——它没有参数校验，也更容易写错。",
        mcp::schema::object("C# 源码",
                            {Property{"code", mcp::schema::string("一段 C# 片段")}},
                            {"code"})});
  }

  // 写工具才会触发视口刷新；读工具不该让画面重绘。
  for (mcp::McpTool& tool : tools) {
    tool.mutates = tool_mutates(tool.name);
  }
  return tools;
}

bool SessionMcpBackend::mutates(std::string_view tool) { return tool_mutates(tool); }

std::vector<mcp::McpResource> SessionMcpBackend::resources() const {
  return {
      mcp::McpResource{"tamias://document", "文档摘要", "名称、实体数、按种类统计",
                       "application/json"},
      mcp::McpResource{"tamias://entities", "实体表",
                       "实体 (id, kind, name)；超过 1000 条用 tamias_list_entities 分页",
                       "application/json"},
      mcp::McpResource{"tamias://selection", "当前选择", "选中的实体 id", "application/json"},
      mcp::McpResource{"tamias://commands", "命令目录", "可 dispatch 的命令、参数与说明",
                       "application/json"},
  };
}

std::vector<mcp::McpResourceTemplate> SessionMcpBackend::resource_templates() const {
  return {
      mcp::McpResourceTemplate{"tamias://entity/{entityId}/features", "实体特征树",
                               "某个实体的特征树与参数", "application/json"},
  };
}

mcp::McpToolResult SessionMcpBackend::call_tool(std::string_view name, const mcp::Json& args) {
  mcp::McpToolResult result = call_tool_impl(name, args);
  // 一次工具调用只刷新一次：批量命令在事务里执行完也只重绘一遍。
  if (after_edit_ && !result.is_error && tool_mutates(name)) {
    after_edit_();
  }
  return result;
}

mcp::McpToolResult SessionMcpBackend::call_tool_impl(std::string_view name,
                                                     const mcp::Json& args) {
  // 读工具在没有文档时返回空表；写工具必须报错——不能让模型以为改动生效了。
  const bool read_only = name == "tamias_document_info" || name == "tamias_list_entities" ||
                         name == "tamias_get_selection" || name == "tamias_get_features";
  if (session_ == nullptr && !read_only) {
    return failure("no active document");
  }
  if (name == "tamias_document_info") {
    return ok(document_json());
  }
  if (name == "tamias_list_entities") {
    const Json* kind = args.find("kind");
    const Json* name_filter = args.find("nameContains");
    const int offset = static_cast<int>(opt_int(args, "offset").value_or(0));
    const int limit = static_cast<int>(opt_int(args, "limit").value_or(100));
    return ok(entities_json(kind != nullptr && kind->is_string() ? kind->as_string() : "",
                            name_filter != nullptr && name_filter->is_string()
                                ? name_filter->as_string()
                                : "",
                            std::max(0, offset), limit));
  }
  if (name == "tamias_get_selection") {
    return ok(selection_json());
  }
  if (name == "tamias_get_features") {
    const std::optional<std::int64_t> id = opt_int(args, "entityId");
    if (!id) {
      return failure("entityId is required and must be an integer");
    }
    const Entity* entity =
        session_ == nullptr ? nullptr : session_->document().entity(static_cast<std::uint64_t>(*id));
    if (entity == nullptr) {
      return failure("entity " + std::to_string(*id) + " does not exist");
    }
    return ok(features_json(static_cast<std::uint64_t>(*id)));
  }
  if (name == "tamias_dispatch") {
    const Json* command = args.find("command");
    if (command == nullptr || !command->is_string()) {
      return failure("command is required and must be a string");
    }
    const Json* command_args = args.find("args");
    const Json empty = Json::object();
    return dispatch_json_command(command->as_string(),
                                 command_args != nullptr ? *command_args : empty);
  }
  if (name == "tamias_set_param") {
    const Json* param_name = args.find("paramName");
    if (!opt_int(args, "entityId") || !opt_int(args, "featureId") || param_name == nullptr ||
        !param_name->is_string() || !opt_double(args, "value")) {
      return failure(
          "tamias_set_param needs entityId, featureId, paramName (string) and value (number)");
    }
    Json command_args = Json::object({
        {"entity_id", Json::integer(*opt_int(args, "entityId"))},
        {"feature_id", Json::integer(*opt_int(args, "featureId"))},
        {"param_name", Json::string(param_name->as_string())},
        {"value", Json::number(*opt_double(args, "value"))},
    });
    return dispatch_json_command("set_param", command_args);
  }
  if (name == "tamias_create_wall") {
    Json command_args = Json::object();
    if (const Json* points = args.find("points"); points != nullptr) {
      if (!points->is_array() || points->size() != 2) {
        return failure("points must be an array of exactly two {x,y,z} points");
      }
      command_args.set("points", *points);
    } else if (args.contains("start") || args.contains("end")) {
      if (!args.contains("start") || !args.contains("end")) {
        return failure("create_wall needs both start and end, or neither");
      }
      Json points = Json::array();
      points.push_back(*args.find("start"));
      points.push_back(*args.find("end"));
      command_args.set("points", std::move(points));
    }
    for (const char* key : {"thickness", "height", "leaf"}) {
      if (const Json* value = args.find(key); value != nullptr) {
        command_args.set(key, *value);
      }
    }
    return dispatch_json_command("create_wall", command_args);
  }
  if (name == "tamias_move_entities") {
    Json command_args = Json::object();
    if (const Json* ids = args.find("ids"); ids != nullptr) {
      command_args.set("ids", *ids);
    } else if (const std::optional<std::int64_t> id = opt_int(args, "entityId"); id) {
      command_args.set("entity_id", Json::integer(*id));
    }
    if (const Json* delta = args.find("delta"); delta != nullptr) {
      command_args.set("delta", *delta);
    } else if (const Json* points = args.find("points"); points != nullptr) {
      command_args.set("points", *points);
    }
    return dispatch_json_command("move_entities", command_args);
  }
  if (name == "tamias_delete_entity") {
    const std::optional<std::int64_t> id = opt_int(args, "entityId");
    if (!id) {
      return failure("entityId is required and must be an integer");
    }
    return dispatch_json_command(
        "delete_entity", Json::object({{"entity_id", Json::integer(*id)}}));
  }
  if (name == "tamias_set_selection") {
    const Json* ids = args.find("ids");
    if (ids == nullptr || !ids->is_array()) {
      return failure("ids is required and must be an array of integers");
    }
    std::vector<std::uint64_t> selection;
    for (const Json& item : ids->items()) {
      if (!item.is_number()) {
        return failure("ids must contain only integers");
      }
      selection.push_back(static_cast<std::uint64_t>(item.as_int()));
    }
    session_->set_selection(selection);
    return ok(selection_json());
  }
  if (name == "tamias_batch") {
    const Json* operations = args.find("operations");
    if (operations == nullptr || !operations->is_array() || operations->empty()) {
      return failure("operations is required and must be a non-empty array");
    }
    return run_batch(*operations);
  }
  if (name == "tamias_undo") {
    if (!session_->can_undo()) {
      return failure("nothing to undo");
    }
    session_->undo();
    return ok(Json::object({{"canUndo", Json::boolean(session_->can_undo())},
                            {"canRedo", Json::boolean(session_->can_redo())}}));
  }
  if (name == "tamias_redo") {
    if (!session_->can_redo()) {
      return failure("nothing to redo");
    }
    session_->redo();
    return ok(Json::object({{"canUndo", Json::boolean(session_->can_undo())},
                            {"canRedo", Json::boolean(session_->can_redo())}}));
  }
  if (name == "tamias_evaluate") {
    if (!evaluate_) {
      return failure("tamias_evaluate is not enabled for this server");
    }
    const Json* code = args.find("code");
    if (code == nullptr || !code->is_string()) {
      return failure("code is required and must be a string");
    }
    const auto result = evaluate_(code->as_string());
    if (!result) {
      return failure(result.error());
    }
    return ok(Json::object({{"result", Json::string(*result)}}));
  }
  return failure("unimplemented tool: " + std::string(name));
}

std::optional<mcp::McpResourceContent> SessionMcpBackend::read_resource(std::string_view uri) {
  if (uri == "tamias://document") {
    return mcp::McpResourceContent{std::string(uri), "application/json", document_json().dump()};
  }
  if (uri == "tamias://entities") {
    return mcp::McpResourceContent{std::string(uri), "application/json",
                                   entities_json("", "", 0, 100000).dump()};
  }
  if (uri == "tamias://selection") {
    return mcp::McpResourceContent{std::string(uri), "application/json", selection_json().dump()};
  }
  if (uri == "tamias://commands") {
    return mcp::McpResourceContent{std::string(uri), "application/json", command_catalog().dump()};
  }
  // tamias://entity/{id}/features
  constexpr std::string_view kPrefix = "tamias://entity/";
  constexpr std::string_view kSuffix = "/features";
  if (uri.starts_with(kPrefix) && uri.ends_with(kSuffix)) {
    const std::string_view id_text =
        uri.substr(kPrefix.size(), uri.size() - kPrefix.size() - kSuffix.size());
    if (id_text.empty()) {
      return std::nullopt;
    }
    std::uint64_t id = 0;
    for (const char ch : id_text) {
      if (ch < '0' || ch > '9') {
        return std::nullopt;
      }
      id = id * 10 + static_cast<std::uint64_t>(ch - '0');
    }
    if (session_ == nullptr || session_->document().entity(id) == nullptr) {
      return std::nullopt;
    }
    return mcp::McpResourceContent{std::string(uri), "application/json", features_json(id).dump()};
  }
  return std::nullopt;
}

Json SessionMcpBackend::document_json() const {
  if (session_ == nullptr) {
    return Json::object({
        {"name", Json::string("")},
        {"entityCount", Json::integer(0)},
        {"selectionCount", Json::integer(0)},
        {"byKind", Json::object()},
    });
  }
  const Document& doc = session_->document();
  std::map<std::string, std::int64_t> counts;
  for (const auto& [id, entity] : doc.entities()) {
    (void)id;
    ++counts[entity_kind_name(entity->kind())];
  }
  Json by_kind = Json::object();
  for (const auto& [kind, count] : counts) {
    by_kind.set(kind, Json::integer(count));
  }
  return Json::object({
      {"name", Json::string(doc.name())},
      {"entityCount", Json::integer(static_cast<std::int64_t>(doc.entities().size()))},
      {"selectionCount", Json::integer(static_cast<std::int64_t>(doc.selected_ids().size()))},
      {"byKind", std::move(by_kind)},
  });
}

Json SessionMcpBackend::entities_json(std::string_view kind_filter, std::string_view name_filter,
                                      int offset, int limit) const {
  if (session_ == nullptr) {
    return Json::object({
        {"total", Json::integer(0)},
        {"offset", Json::integer(offset)},
        {"limit", Json::integer(std::clamp(limit, 1, 1000))},
        {"entities", Json::array()},
    });
  }
  const Document& doc = session_->document();
  const std::string wanted_kind = ascii_lower(std::string(kind_filter));
  const std::string wanted_name = ascii_lower(std::string(name_filter));
  const int capped_limit = std::clamp(limit, 1, 1000);

  std::vector<std::uint64_t> ids;
  ids.reserve(doc.entities().size());
  for (const auto& [id, entity] : doc.entities()) {
    if (!wanted_kind.empty() && ascii_lower(entity_kind_name(entity->kind())) != wanted_kind) {
      continue;
    }
    if (!wanted_name.empty() &&
        ascii_lower(entity->name).find(wanted_name) == std::string::npos) {
      continue;
    }
    ids.push_back(id);
  }
  std::sort(ids.begin(), ids.end());

  Json entities = Json::array();
  for (std::size_t i = static_cast<std::size_t>(offset); i < ids.size() &&
       entities.size() < static_cast<std::size_t>(capped_limit);
       ++i) {
    const Entity* entity = doc.entity(ids[i]);
    entities.push_back(Json::object({
        {"id", Json::integer(static_cast<std::int64_t>(ids[i]))},
        {"kind", Json::string(entity_kind_name(entity->kind()))},
        {"name", Json::string(entity->name)},
    }));
  }
  return Json::object({
      {"total", Json::integer(static_cast<std::int64_t>(ids.size()))},
      {"offset", Json::integer(offset)},
      {"limit", Json::integer(capped_limit)},
      {"entities", std::move(entities)},
  });
}

Json SessionMcpBackend::features_json(std::uint64_t entity_id) const {
  Json features = Json::array();
  const Entity* entity =
      session_ == nullptr ? nullptr : session_->document().entity(entity_id);
  if (entity == nullptr) {
    return Json::object({{"entityId", Json::integer(static_cast<std::int64_t>(entity_id))},
                         {"features", std::move(features)}});
  }
  for (const Feature& feature : entity->model.features()) {
    Json inputs = Json::array();
    for (const std::uint64_t input : feature.inputs) {
      inputs.push_back(Json::integer(static_cast<std::int64_t>(input)));
    }
    std::map<std::string, double> sorted_params(feature.params.begin(), feature.params.end());
    Json params = Json::object();
    for (const auto& [name, value] : sorted_params) {
      params.set(name, Json::number(value));
    }
    features.push_back(Json::object({
        {"id", Json::integer(static_cast<std::int64_t>(feature.id))},
        {"kind", Json::string(feature_kind_name(feature.kind))},
        {"inputs", std::move(inputs)},
        {"params", std::move(params)},
    }));
  }
  return Json::object({
      {"entityId", Json::integer(static_cast<std::int64_t>(entity_id))},
      {"kind", Json::string(entity_kind_name(entity->kind()))},
      {"name", Json::string(entity->name)},
      {"features", std::move(features)},
  });
}

Json SessionMcpBackend::selection_json() const {
  if (session_ == nullptr) {
    return Json::object({{"ids", Json::array()}});
  }
  return Json::object({
      {"ids", ids_json(session_->selection())},
  });
}

mcp::McpToolResult SessionMcpBackend::dispatch_json_command(std::string_view command,
                                                            const Json& args) {
  if (session_ == nullptr) {
    return failure("no active document");
  }
  auto text = format_command_arg_text(args);
  if (!text) {
    return failure(text.error());
  }
  auto parsed = parse_command_arg_text(*text);
  if (!parsed) {
    return failure(parsed.error());
  }
  const auto result = session_->dispatch(command, *parsed);
  if (!result) {
    return failure(result.error());
  }
  // armed = 交互式命令已架起，等用户在视口点。这是正常结果，不是错误。
  return ok(Json::object({
      {"dispatched", Json::string(std::string(command))},
      {"armed", Json::boolean(session_->command_system().has_pending())},
      {"canUndo", Json::boolean(session_->can_undo())},
      {"canRedo", Json::boolean(session_->can_redo())},
  }));
}

mcp::McpToolResult SessionMcpBackend::run_batch(const Json& operations) {
  if (session_ == nullptr) {
    return failure("no active document");
  }
  const auto begin = session_->command_system().begin_transaction("AI batch");
  if (!begin) {
    return failure(begin.error());
  }
  int executed = 0;
  for (const Json& operation : operations.items()) {
    if (!operation.is_object()) {
      (void)session_->command_system().abort_transaction();
      return failure("every operation must be an object {command, args?}");
    }
    const Json* command = operation.find("command");
    if (command == nullptr || !command->is_string()) {
      (void)session_->command_system().abort_transaction();
      return failure("every operation needs a string 'command'");
    }
    const Json* args = operation.find("args");
    const Json empty = Json::object();
    auto text = format_command_arg_text(args != nullptr ? *args : empty);
    if (!text) {
      (void)session_->command_system().abort_transaction();
      return failure(text.error());
    }
    auto parsed = parse_command_arg_text(*text);
    if (!parsed) {
      (void)session_->command_system().abort_transaction();
      return failure(parsed.error());
    }
    const auto result = session_->dispatch(command->as_string(), *parsed);
    if (!result) {
      (void)session_->command_system().abort_transaction();
      return failure("operation " + std::to_string(executed) + " failed and the batch was "
                     "rolled back: " + result.error());
    }
    ++executed;
  }
  const auto commit = session_->command_system().commit_transaction();
  if (!commit) {
    return failure(commit.error());
  }
  return ok(Json::object({
      {"executed", Json::integer(executed)},
      {"undoSteps", Json::integer(1)},
      {"canUndo", Json::boolean(session_->can_undo())},
  }));
}

}  // namespace tamias
