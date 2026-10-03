#include "mcp/mcp_backend.h"

#include <utility>

namespace tamias::mcp::schema {

namespace {

Json typed(std::string type, std::string description) {
  return Json::object({
      {"type", Json::string(std::move(type))},
      {"description", Json::string(std::move(description))},
  });
}

}  // namespace

Json string(std::string description) { return typed("string", std::move(description)); }

Json integer(std::string description) { return typed("integer", std::move(description)); }

Json number(std::string description) { return typed("number", std::move(description)); }

Json boolean(std::string description) { return typed("boolean", std::move(description)); }

Json array(std::string description, Json items) {
  return Json::object({
      {"type", Json::string("array")},
      {"description", Json::string(std::move(description))},
      {"items", std::move(items)},
  });
}

Json freeform(std::string description) {
  return Json::object({{"description", Json::string(std::move(description))}});
}

Json object(std::string description, std::vector<Property> properties,
            std::vector<std::string> required) {
  Json props = Json::object();
  for (auto& property : properties) {
    props.set(property.name, std::move(property.schema));
  }
  Json required_array = Json::array();
  for (auto& name : required) {
    required_array.push_back(Json::string(std::move(name)));
  }
  return Json::object({
      {"type", Json::string("object")},
      {"description", Json::string(std::move(description))},
      {"properties", std::move(props)},
      {"required", std::move(required_array)},
      {"additionalProperties", Json::boolean(false)},
  });
}

}  // namespace tamias::mcp::schema
