#include "mcp/mcp_server.h"

#include <utility>

namespace tamias::mcp {

namespace {

// 客户端可能带着较旧的协议版本进来；认得的就照它回，认不得就报我们自己最新的。
// 这是 MCP 的协商规则：服务端返回它实际使用的版本，客户端不认就断开。
constexpr std::string_view kKnownProtocolVersions[] = {
    "2025-06-18",
    "2025-03-26",
    "2024-11-05",
};

bool is_known_protocol_version(std::string_view version) {
  for (const std::string_view known : kKnownProtocolVersions) {
    if (known == version) {
      return true;
    }
  }
  return false;
}

bool is_notification_method(std::string_view method) {
  if (method == "notifications/initialized" || method == "notifications/cancelled" ||
      method == "notifications/progress") {
    return true;
  }
  return method.starts_with("notifications/");
}

}  // namespace

McpServer::McpServer(McpBackend& backend, std::string server_name, std::string server_version)
    : backend_(backend),
      server_name_(std::move(server_name)),
      server_version_(std::move(server_version)) {}

std::optional<Json> McpServer::handle_text(std::string_view text) {
  std::string error;
  std::optional<Json> parsed = Json::parse(text, &error);
  if (!parsed) {
    return make_error(Json::null(), kParseError,
                      error.empty() ? std::string("parse error") : error);
  }
  return handle(*parsed);
}

std::optional<Json> McpServer::handle(const Json& message) {
  if (message.is_object()) {
    return dispatch(message);
  }
  if (message.is_array()) {
    // JSON-RPC 批量：逐个处理，只收集需要回包的响应。
    Json responses = Json::array();
    for (const Json& entry : message.items()) {
      std::optional<Json> response = dispatch(entry);
      if (response) {
        responses.push_back(std::move(*response));
      }
    }
    if (responses.empty()) {
      return std::nullopt;
    }
    return responses;
  }
  return make_error(Json::null(), kInvalidRequest, "request must be a JSON object");
}

std::optional<Json> McpServer::dispatch(const Json& message) {
  const Json* version = message.find("jsonrpc");
  const Json* method = message.find("method");
  if (!message.is_object() || version == nullptr || !version->is_string() ||
      version->as_string() != "2.0" || method == nullptr || !method->is_string()) {
    const Json* id = message.is_object() ? message.find("id") : nullptr;
    return make_error(id != nullptr ? *id : Json::null(), kInvalidRequest,
                      "not a valid JSON-RPC 2.0 request");
  }

  const Json* id = message.find("id");
  const bool is_notification = id == nullptr || is_notification_method(method->as_string());
  const Json response_id = id != nullptr ? *id : Json::null();
  const Json empty = Json::object();
  const Json* params = message.find("params");
  if (params == nullptr) {
    params = &empty;
  }

  if (is_notification) {
    return std::nullopt;  // 通知不回包，也不报未知方法
  }

  const std::string& name = method->as_string();
  if (name == "initialize") {
    return handle_initialize(*params, response_id);
  }
  if (name == "ping") {
    return make_result(response_id, Json::object());
  }
  if (name == "tools/list") {
    return handle_tools_list(response_id);
  }
  if (name == "tools/call") {
    return handle_tools_call(*params, response_id);
  }
  if (name == "resources/list") {
    return handle_resources_list(response_id);
  }
  if (name == "resources/templates/list") {
    return handle_resources_templates_list(response_id);
  }
  if (name == "resources/read") {
    return handle_resources_read(*params, response_id);
  }
  return make_error(response_id, kMethodNotFound, "unknown method: " + name);
}

Json McpServer::handle_initialize(const Json& params, const Json& id) {
  std::string_view negotiated = supported_protocol_version();
  if (const Json* requested = params.find("protocolVersion");
      requested != nullptr && requested->is_string() &&
      is_known_protocol_version(requested->as_string())) {
    negotiated = requested->as_string();
  }

  Json tools_capability = Json::object({{"listChanged", Json::boolean(false)}});
  Json resources_capability = Json::object({
      {"listChanged", Json::boolean(false)},
      {"subscribe", Json::boolean(false)},
  });
  Json capabilities = Json::object({
      {"tools", std::move(tools_capability)},
      {"resources", std::move(resources_capability)},
  });
  Json server_info = Json::object({
      {"name", Json::string(server_name_)},
      {"version", Json::string(server_version_)},
  });
  return make_result(id, Json::object({
                             {"protocolVersion", Json::string(std::string(negotiated))},
                             {"capabilities", std::move(capabilities)},
                             {"serverInfo", std::move(server_info)},
                         }));
}

Json McpServer::handle_tools_list(const Json& id) {
  Json tools = Json::array();
  for (const McpTool& tool : backend_.tools()) {
    tools.push_back(Json::object({
        {"name", Json::string(tool.name)},
        {"description", Json::string(tool.description)},
        {"inputSchema", tool.input_schema},
    }));
  }
  return make_result(id, Json::object({{"tools", std::move(tools)}}));
}

Json McpServer::handle_tools_call(const Json& params, const Json& id) {
  const Json* name = params.find("name");
  if (name == nullptr || !name->is_string()) {
    return make_error(id, kInvalidParams, "tools/call requires a string 'name'");
  }

  // 未知工具是协议级错误（模型写错了工具名）；工具自己执行失败走 isError 结果。
  bool known = false;
  for (const McpTool& tool : backend_.tools()) {
    if (tool.name == name->as_string()) {
      known = true;
      break;
    }
  }
  if (!known) {
    return make_error(id, kInvalidParams, "unknown tool: " + name->as_string());
  }

  const Json* arguments = params.find("arguments");
  const Json empty_args = Json::object();
  if (arguments == nullptr) {
    arguments = &empty_args;
  } else if (!arguments->is_object() && !arguments->is_null()) {
    return make_error(id, kInvalidParams, "tools/call 'arguments' must be an object");
  }

  if (tool_gate_) {
    const McpToolGateDecision decision = tool_gate_(name->as_string(), *arguments);
    if (!decision.allow) {
      const McpToolResult denied{true, decision.reason};
      if (tool_observer_) {
        tool_observer_(name->as_string(), *arguments, denied);
      }
      return make_result(id, content_of(denied));
    }
  }

  const McpToolResult result = backend_.call_tool(name->as_string(), *arguments);
  if (tool_observer_) {
    tool_observer_(name->as_string(), *arguments, result);
  }
  return make_result(id, content_of(result));
}

Json McpServer::content_of(const McpToolResult& result) {
  Json content = Json::array();
  content.push_back(Json::object({
      {"type", Json::string("text")},
      {"text", Json::string(result.text)},
  }));
  return Json::object({
      {"content", std::move(content)},
      {"isError", Json::boolean(result.is_error)},
  });
}

Json McpServer::handle_resources_list(const Json& id) {
  Json resources = Json::array();
  for (const McpResource& resource : backend_.resources()) {
    resources.push_back(Json::object({
        {"uri", Json::string(resource.uri)},
        {"name", Json::string(resource.name)},
        {"description", Json::string(resource.description)},
        {"mimeType", Json::string(resource.mime_type)},
    }));
  }
  return make_result(id, Json::object({{"resources", std::move(resources)}}));
}

Json McpServer::handle_resources_templates_list(const Json& id) {
  Json templates = Json::array();
  for (const McpResourceTemplate& entry : backend_.resource_templates()) {
    templates.push_back(Json::object({
        {"uriTemplate", Json::string(entry.uri_template)},
        {"name", Json::string(entry.name)},
        {"description", Json::string(entry.description)},
        {"mimeType", Json::string(entry.mime_type)},
    }));
  }
  return make_result(id, Json::object({{"resourceTemplates", std::move(templates)}}));
}

Json McpServer::handle_resources_read(const Json& params, const Json& id) {
  const Json* uri = params.find("uri");
  if (uri == nullptr || !uri->is_string()) {
    return make_error(id, kInvalidParams, "resources/read requires a string 'uri'");
  }
  std::optional<McpResourceContent> content = backend_.read_resource(uri->as_string());
  if (!content) {
    return make_error(id, kInvalidParams, "unknown resource: " + uri->as_string());
  }
  Json contents = Json::array();
  contents.push_back(Json::object({
      {"uri", Json::string(content->uri)},
      {"mimeType", Json::string(content->mime_type)},
      {"text", Json::string(content->text)},
  }));
  return make_result(id, Json::object({{"contents", std::move(contents)}}));
}

Json McpServer::make_result(Json id, Json result) {
  return Json::object({
      {"jsonrpc", Json::string("2.0")},
      {"id", std::move(id)},
      {"result", std::move(result)},
  });
}

Json McpServer::make_error(Json id, int code, std::string message) {
  return Json::object({
      {"jsonrpc", Json::string("2.0")},
      {"id", std::move(id)},
      {"error",
       Json::object({
           {"code", Json::integer(code)},
           {"message", Json::string(std::move(message))},
       })},
  });
}

}  // namespace tamias::mcp
