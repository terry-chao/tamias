#include "command/command_system.h"

#include "command/add_feature_command.h"
#include "command/boolean_command.h"
#include "command/create_primitive_command.h"
#include "command/create_wall_command.h"
#include "command/set_feature_param_command.h"
#include "command/set_material_command.h"

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

std::string arg_string(const CommandArgs& args, const std::string& name, std::string fallback) {
  const auto it = args.find(name);
  return (it != args.end() && std::holds_alternative<std::string>(it->second))
             ? std::get<std::string>(it->second)
             : std::move(fallback);
}

}  // namespace

void register_commands(CommandRegistry& registry) {
  registry.register_command("create_wall", [](Document& doc, const CommandArgs& args) {
    return std::make_unique<CreateWallCommand>(doc, arg_double(args, "thickness", 0.2),
                                               arg_double(args, "height", 3.0));
  });

  registry.register_command("create_box", [](Document& doc, const CommandArgs& args) {
    (void)args;
    return std::make_unique<CreatePrimitiveCommand>(doc, PrimitiveKind::Box);
  });

  registry.register_command("create_cylinder", [](Document& doc, const CommandArgs& args) {
    (void)args;
    return std::make_unique<CreatePrimitiveCommand>(doc, PrimitiveKind::Cylinder);
  });

  registry.register_command("set_param", [](Document& doc, const CommandArgs& args) {
    return std::make_unique<SetFeatureParamCommand>(
        doc, static_cast<std::uint64_t>(arg_int(args, "entity_id", 0)),
        static_cast<std::uint64_t>(arg_int(args, "feature_id", 0)),
        arg_string(args, "param_name", ""), arg_double(args, "value", 0.0));
  });

  registry.register_command("fillet", [](Document& doc, const CommandArgs& args) {
    return std::make_unique<AddFeatureCommand>(
        doc, static_cast<std::uint64_t>(arg_int(args, "entity_id", 0)), FeatureKind::Fillet,
        std::unordered_map<std::string, double>{
            {"radius", arg_double(args, "radius", 0.1)},
            {"edge", static_cast<double>(arg_int(args, "edge", 0))}});
  });

  registry.register_command("chamfer", [](Document& doc, const CommandArgs& args) {
    return std::make_unique<AddFeatureCommand>(
        doc, static_cast<std::uint64_t>(arg_int(args, "entity_id", 0)), FeatureKind::Chamfer,
        std::unordered_map<std::string, double>{
            {"distance", arg_double(args, "distance", 0.1)},
            {"edge", static_cast<double>(arg_int(args, "edge", 0))}});
  });

  registry.register_command("boolean", [](Document& doc, const CommandArgs& args) {
    return std::make_unique<BooleanCommand>(
        doc, static_cast<std::uint64_t>(arg_int(args, "a", 0)),
        static_cast<std::uint64_t>(arg_int(args, "b", 0)),
        static_cast<BooleanOp>(arg_int(args, "operation", 0)));
  });

  registry.register_command("set_material", [](Document& doc, const CommandArgs& args) {
    Material material{};
    material.id = static_cast<std::uint64_t>(arg_int(args, "material_id", 0));
    material.name = arg_string(args, "name", "");
    if (const auto it = args.find("base_color");
        it != args.end() && std::holds_alternative<Vec3>(it->second)) {
      material.base_color = std::get<Vec3>(it->second);
    }
    material.roughness = static_cast<float>(arg_double(args, "roughness", 0.6));
    material.metallic = static_cast<float>(arg_double(args, "metallic", 0.0));
    material.albedo_texture_id =
        static_cast<std::uint64_t>(arg_int(args, "albedo_texture_id", 0));
    material.normal_texture_id =
        static_cast<std::uint64_t>(arg_int(args, "normal_texture_id", 0));
    return std::make_unique<SetMaterialCommand>(
        doc, static_cast<std::uint64_t>(arg_int(args, "entity_id", 0)), std::move(material));
  });
}

}  // namespace tamias
