#include "command/command_system.h"

#include "command/create_primitive_command.h"
#include "command/create_wall_command.h"
#include "command/set_feature_param_command.h"

namespace tamias {
namespace {

double arg_double(const CommandArgs& args, const std::string& name, double fallback) {
  const auto it = args.find(name);
  return (it != args.end() && std::holds_alternative<double>(it->second))
             ? std::get<double>(it->second)
             : fallback;
}

std::int64_t arg_int(const CommandArgs& args, const std::string& name, std::int64_t fallback) {
  const auto it = args.find(name);
  return (it != args.end() && std::holds_alternative<std::int64_t>(it->second))
             ? std::get<std::int64_t>(it->second)
             : fallback;
}

Vec3 arg_vec3(const CommandArgs& args, const std::string& name, Vec3 fallback) {
  const auto it = args.find(name);
  return (it != args.end() && std::holds_alternative<Vec3>(it->second))
             ? std::get<Vec3>(it->second)
             : fallback;
}

std::string arg_string(const CommandArgs& args, const std::string& name, std::string fallback) {
  const auto it = args.find(name);
  return (it != args.end() && std::holds_alternative<std::string>(it->second))
             ? std::get<std::string>(it->second)
             : std::move(fallback);
}

}  // namespace

void register_commands(CommandRegistry& registry) {
  registry.register_command("create_wall", [](Document& doc, const CommandArgs& args) {
    return std::make_unique<CreateWallCommand>(
        doc, arg_vec3(args, "start", {}), arg_vec3(args, "end", {}),
        arg_double(args, "thickness", 0.2), arg_double(args, "height", 3.0));
  });

  registry.register_command("create_box", [](Document& doc, const CommandArgs& args) {
    return std::make_unique<CreatePrimitiveCommand>(doc, PrimitiveKind::Box,
                                                    arg_vec3(args, "position", {}));
  });

  registry.register_command("create_cylinder", [](Document& doc, const CommandArgs& args) {
    return std::make_unique<CreatePrimitiveCommand>(doc, PrimitiveKind::Cylinder,
                                                    arg_vec3(args, "position", {}));
  });

  registry.register_command("set_param", [](Document& doc, const CommandArgs& args) {
    return std::make_unique<SetFeatureParamCommand>(
        doc, static_cast<std::uint64_t>(arg_int(args, "entity_id", 0)),
        static_cast<std::uint64_t>(arg_int(args, "feature_id", 0)),
        arg_string(args, "param_name", ""), arg_double(args, "value", 0.0));
  });
}

}  // namespace tamias
