#include "bim/ifc_spatial_tree.h"

#include "ifcparse/IfcException.h"
#include "ifcparse/IfcFile.h"

#include <boost/shared_ptr.hpp>

#include <cstdint>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace tamias {
namespace {

std::string path_utf8(const std::filesystem::path& path) {
  const auto u8 = path.u8string();
  return std::string(u8.begin(), u8.end());
}

const char* open_status_text(IfcParse::file_open_status status) {
  switch (status.value()) {
    case IfcParse::file_open_status::SUCCESS:
      return "success";
    case IfcParse::file_open_status::READ_ERROR:
      return "read error";
    case IfcParse::file_open_status::NO_HEADER:
      return "no header";
    case IfcParse::file_open_status::UNSUPPORTED_SCHEMA:
      return "unsupported schema";
    case IfcParse::file_open_status::INVALID_SYNTAX:
      return "invalid syntax";
    default:
      return "unknown error";
  }
}

std::string entity_name(const IfcUtil::IfcBaseEntity* entity) {
  try {
    return entity->get_value<std::string>("Name", "");
  } catch (const IfcParse::IfcException&) {
    return {};
  }
}

std::vector<IfcUtil::IfcBaseEntity*> as_entities(const AttributeValue& value) {
  std::vector<IfcUtil::IfcBaseEntity*> out;
  if (value.isNull()) {
    return out;
  }
  if (value.type() == IfcUtil::Argument_ENTITY_INSTANCE) {
    auto* inst = static_cast<IfcUtil::IfcBaseClass*>(value);
    if (auto* entity = inst->as<IfcUtil::IfcBaseEntity>()) {
      out.push_back(entity);
    }
    return out;
  }
  if (value.type() != IfcUtil::Argument_AGGREGATE_OF_ENTITY_INSTANCE) {
    return out;
  }
  const aggregate_of_instance::ptr list = value;
  if (!list) {
    return out;
  }
  for (auto* inst : *list) {
    if (auto* entity = inst->as<IfcUtil::IfcBaseEntity>()) {
      out.push_back(entity);
    }
  }
  return out;
}

std::vector<IfcUtil::IfcBaseEntity*> related_via(IfcUtil::IfcBaseEntity* entity,
                                                 const char* inverse, const char* attr) {
  std::vector<IfcUtil::IfcBaseEntity*> out;
  boost::shared_ptr<aggregate_of_instance> rels;
  try {
    rels = entity->get_inverse(inverse);
  } catch (const IfcParse::IfcException&) {
    return out;
  }
  if (!rels) {
    return out;
  }
  for (auto* rel_inst : *rels) {
    auto* rel = rel_inst->as<IfcUtil::IfcBaseEntity>();
    if (rel == nullptr) {
      continue;
    }
    try {
      auto kids = as_entities(rel->get(attr));
      out.insert(out.end(), kids.begin(), kids.end());
    } catch (const IfcParse::IfcException&) {
    }
  }
  return out;
}

void dump_entity(std::ostringstream& out, IfcUtil::IfcBaseEntity* entity, int indent,
                 std::unordered_set<std::uint32_t>& visited) {
  if (entity == nullptr) {
    return;
  }
  const auto id = entity->identity();
  if (!visited.insert(id).second) {
    return;
  }
  out << std::string(static_cast<std::size_t>(indent) * 2, ' ') << entity->declaration().name();
  const std::string name = entity_name(entity);
  if (!name.empty()) {
    out << "  " << name;
  }
  out << '\n';

  for (auto* child : related_via(entity, "IsDecomposedBy", "RelatedObjects")) {
    dump_entity(out, child, indent + 1, visited);
  }
  for (auto* child : related_via(entity, "ContainsElements", "RelatedElements")) {
    dump_entity(out, child, indent + 1, visited);
  }
}

}  // namespace

Result<std::string> format_ifc_spatial_tree(const std::filesystem::path& path) {
  if (!std::filesystem::exists(path)) {
    return Err("IFC file not found: " + path_utf8(path));
  }

  IfcParse::IfcFile file(path_utf8(path));
  if (!file.good()) {
    return Err(std::string("IfcOpenShell failed to parse IFC (") + open_status_text(file.good()) +
               "): " + path_utf8(path));
  }

  auto projects = file.instances_by_type("IfcProject");
  if (!projects || projects->size() == 0) {
    return Err("IFC has no IfcProject: " + path_utf8(path));
  }

  std::ostringstream out;
  out << "schema  " << (file.schema() ? file.schema()->name() : "?") << '\n';
  out << "file    " << path.filename().string() << '\n';
  out << "tree\n";

  std::unordered_set<std::uint32_t> visited;
  for (auto* inst : *projects) {
    dump_entity(out, inst->as<IfcUtil::IfcBaseEntity>(), 1, visited);
  }
  return out.str();
}

}  // namespace tamias
