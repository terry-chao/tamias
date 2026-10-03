#include "mcp/composite_backend.h"

#include <unordered_set>

namespace tamias::mcp {

bool CompositeBackend::add(McpBackend& backend, std::string* error) {
  const std::vector<McpTool> tools = backend.tools();
  const std::vector<McpResource> resources = backend.resources();
  const std::vector<McpResourceTemplate> templates = backend.resource_templates();

  // 先整套检查完再登记：半途失败留下半个后端，比一开始就拒绝更糟。
  for (const McpTool& tool : tools) {
    if (tool_routes_.contains(tool.name)) {
      if (error != nullptr) {
        *error = "工具重名：" + tool.name;
      }
      return false;
    }
  }
  for (const McpResource& resource : resources) {
    if (resource_routes_.contains(resource.uri)) {
      if (error != nullptr) {
        *error = "资源重名：" + resource.uri;
      }
      return false;
    }
  }
  std::unordered_set<std::string> templated;
  for (const McpResourceTemplate& template_ : templates) {
    templated.insert(template_.uri_template);
  }
  for (const McpBackend* existing : backends_) {
    for (const McpResourceTemplate& template_ : existing->resource_templates()) {
      if (templated.contains(template_.uri_template)) {
        if (error != nullptr) {
          *error = "资源模板重名：" + template_.uri_template;
        }
        return false;
      }
    }
  }

  for (const McpTool& tool : tools) {
    tool_routes_.emplace(tool.name, &backend);
  }
  for (const McpResource& resource : resources) {
    resource_routes_.emplace(resource.uri, &backend);
  }
  backends_.push_back(&backend);
  return true;
}

std::vector<McpTool> CompositeBackend::tools() const {
  std::vector<McpTool> merged;
  for (McpBackend* backend : backends_) {
    const std::vector<McpTool> tools = backend->tools();
    merged.insert(merged.end(), tools.begin(), tools.end());
  }
  return merged;
}

std::vector<McpResource> CompositeBackend::resources() const {
  std::vector<McpResource> merged;
  for (McpBackend* backend : backends_) {
    const std::vector<McpResource> resources = backend->resources();
    merged.insert(merged.end(), resources.begin(), resources.end());
  }
  return merged;
}

std::vector<McpResourceTemplate> CompositeBackend::resource_templates() const {
  std::vector<McpResourceTemplate> merged;
  for (McpBackend* backend : backends_) {
    const std::vector<McpResourceTemplate> templates = backend->resource_templates();
    merged.insert(merged.end(), templates.begin(), templates.end());
  }
  return merged;
}

McpToolResult CompositeBackend::call_tool(std::string_view name, const Json& args) {
  const auto found = tool_routes_.find(std::string(name));
  if (found == tool_routes_.end()) {
    // 协议层保证名字来自 tools()，走到这里说明装配漏了，别静默成功。
    return McpToolResult{true, "没有后端提供工具 " + std::string(name)};
  }
  return found->second->call_tool(name, args);
}

std::optional<McpResourceContent> CompositeBackend::read_resource(std::string_view uri) {
  if (const auto found = resource_routes_.find(std::string(uri));
      found != resource_routes_.end()) {
    return found->second->read_resource(uri);
  }
  // 模板资源（如 tamias://entity/{id}/features）不在 resources() 里，路由表查不到，
  // 只能按注册顺序逐个问。这也是 add() 的顺序会影响行为的地方，所以它要稳定。
  for (McpBackend* backend : backends_) {
    if (std::optional<McpResourceContent> content = backend->read_resource(uri); content) {
      return content;
    }
  }
  return std::nullopt;
}

}  // namespace tamias::mcp
