#pragma once

#include "mcp/json.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tamias::mcp {

// 门面与「谁提供能力」之间唯一的接缝。协议层（McpServer）只认这个接口，
// 不知道 Session、Document、命令注册表的存在——所以它能用假后端单测，
// 也能在没有 Qt / engine 的构建里编译。
//
// 后端实现方（B1 是 SessionMcpBackend）负责把工具调用翻译成
// 「开事务 → dispatch → commit」以及从文档快照里读宽读面。

struct McpTool {
  std::string name;
  std::string description;
  Json input_schema;  // JSON Schema 对象
  // 这个工具会不会改文档。服务层据此在调用成功后刷新视口，
  // 也是 B3 审批闸门与只读模式判定的依据。
  bool mutates = false;
};

struct McpResource {
  std::string uri;
  std::string name;
  std::string description;
  std::string mime_type = "application/json";
};

// 动态资源（URI 里带实体 id 之类）用模板声明，客户端据此知道能读什么。
struct McpResourceTemplate {
  std::string uri_template;  // 例如 tamias://entity/{entityId}/features
  std::string name;
  std::string description;
  std::string mime_type = "application/json";
};

// 工具执行结果。当前只需要文本内容：MCP 的 content 数组允许 text，
// 而 Tamias 的结果（实体表、特征树、错误文本）本来就是文本/JSON。
// 将来要发视口截图，在这里加 image 内容块即可。
struct McpToolResult {
  bool is_error = false;
  std::string text;
};

struct McpResourceContent {
  std::string uri;
  std::string mime_type = "application/json";
  std::string text;
};

class McpBackend {
 public:
  virtual ~McpBackend() = default;

  // 工具 / 资源的描述只在 tools/list、resources/list 时取，允许实现方动态计算。
  [[nodiscard]] virtual std::vector<McpTool> tools() const = 0;
  [[nodiscard]] virtual std::vector<McpResource> resources() const = 0;
  [[nodiscard]] virtual std::vector<McpResourceTemplate> resource_templates() const = 0;

  // 调用一个工具。名字一定来自 tools()，实现方不必再判存在性；
  // 执行失败用 is_error = true + 文本返回（协议层会包成 MCP 的 isError 结果）。
  [[nodiscard]] virtual McpToolResult call_tool(std::string_view name, const Json& args) = 0;

  // 读一个资源。uri 不在 resources() 里时返回 nullopt，协议层报 invalid params。
  [[nodiscard]] virtual std::optional<McpResourceContent> read_resource(
      std::string_view uri) = 0;
};

// —— JSON Schema 构造小工具 ——
// 工具参数 schema 是机器读的，手搓字符串最容易出错；这几个函数只覆盖
// 「对象 + 少量属性 + required」这一种形状，够 Temias 的门面用。
namespace schema {

[[nodiscard]] Json string(std::string description);
[[nodiscard]] Json integer(std::string description);
[[nodiscard]] Json number(std::string description);
[[nodiscard]] Json boolean(std::string description);
[[nodiscard]] Json array(std::string description, Json items);
// 值任意（用于 args 这种「命令名 + 自由参数」的场景）。
[[nodiscard]] Json freeform(std::string description);

struct Property {
  std::string name;
  Json schema;
};

[[nodiscard]] Json object(std::string description, std::vector<Property> properties,
                          std::vector<std::string> required = {});

}  // namespace schema

}  // namespace tamias::mcp
