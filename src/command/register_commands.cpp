#include "command/command_system.h"

#include "command/add_feature_command.h"
#include "command/boolean_command.h"
#include "command/create_beam_command.h"
#include "command/create_curve_command.h"
#include "command/create_primitive_command.h"
#include "command/create_sketch_command.h"
#include "command/create_slab_command.h"
#include "command/create_storey_command.h"
#include "command/create_wall_command.h"
#include "command/delete_entity_command.h"
#include "command/set_feature_param_command.h"
#include "command/set_location_command.h"
#include "command/set_material_command.h"
#include "bim/wall_size.h"

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

std::vector<Vec3> arg_points(const CommandArgs& args, const std::string& name) {
  const auto it = args.find(name);
  return (it != args.end() && std::holds_alternative<std::vector<Vec3>>(it->second))
             ? std::get<std::vector<Vec3>>(it->second)
             : std::vector<Vec3>{};
}

std::vector<double> arg_doubles(const CommandArgs& args, const std::string& name) {
  const auto it = args.find(name);
  return (it != args.end() && std::holds_alternative<std::vector<double>>(it->second))
             ? std::get<std::vector<double>>(it->second)
             : std::vector<double>{};
}

CurveKind curve_kind_from_name(const std::string& name) {
  if (name == "polyline") {
    return CurveKind::Polyline;
  }
  if (name == "bezier") {
    return CurveKind::Bezier;
  }
  if (name == "bspline") {
    return CurveKind::BSpline;
  }
  if (name == "nurbs") {
    return CurveKind::Nurbs;
  }
  return name == "line" ? CurveKind::Line : CurveKind::Unknown;
}

}  // namespace

void register_commands(CommandRegistry& registry) {
  registry.register_command("create_storey", [](Document& doc, const CommandArgs& args) {
    return std::make_unique<CreateStoreyCommand>(
        doc, arg_string(args, "name", "Storey"),
        arg_double(args, "elevation", 0.0));
  });
  registry.register_command("set_location", [](Document& doc, const CommandArgs& args) {
    return std::make_unique<SetLocationCommand>(
        doc, static_cast<std::uint64_t>(arg_int(args, "entity_id", 0)),
        static_cast<std::uint64_t>(arg_int(args, "storey_id", 0)),
        arg_double(args, "elevation_offset", 0.0));
  });

  registry.register_command("create_wall", [](Document& doc, const CommandArgs& args) {
    return std::make_unique<CreateWallCommand>(
        doc, arg_double(args, "thickness", kDefaultWallThickness),
        arg_double(args, "height", kDefaultWallHeight));
  });

  registry.register_command("create_beam", [](Document& doc, const CommandArgs& args) {
    return std::make_unique<CreateBeamCommand>(doc, arg_double(args, "width", 0.3),
                                               arg_double(args, "depth", 0.5));
  });

  registry.register_command("create_box", [](Document& doc, const CommandArgs& args) {
    (void)args;
    return std::make_unique<CreatePrimitiveCommand>(doc, PrimitiveKind::Box);
  });

  registry.register_command("create_cylinder", [](Document& doc, const CommandArgs& args) {
    (void)args;
    return std::make_unique<CreatePrimitiveCommand>(doc, PrimitiveKind::Cylinder);
  });

  registry.register_command("create_column", [](Document& doc, const CommandArgs& args) {
    (void)args;
    return std::make_unique<CreatePrimitiveCommand>(doc, PrimitiveKind::Column);
  });

  registry.register_command("create_slab", [](Document& doc, const CommandArgs& args) {
    const double default_offset =
        doc.bim().active_storey_id() == 0 ? kDefaultWallHeight : 0.0;
    return std::make_unique<CreateSlabCommand>(
        doc, arg_double(args, "thickness", 0.2),
        arg_double(args, "elevation", default_offset));
  });

  registry.register_command("create_door", [](Document& doc, const CommandArgs& args) {
    (void)args;
    return std::make_unique<CreatePrimitiveCommand>(doc, PrimitiveKind::Door);
  });

  registry.register_command("create_window", [](Document& doc, const CommandArgs& args) {
    (void)args;
    return std::make_unique<CreatePrimitiveCommand>(doc, PrimitiveKind::Window);
  });

  registry.register_command("create_line", [](Document& doc, const CommandArgs& args) {
    (void)args;
    return std::make_unique<CreateSketchCommand>(doc, SketchKind::Line);
  });
  registry.register_command("create_polyline", [](Document& doc, const CommandArgs& args) {
    (void)args;
    return std::make_unique<CreateSketchCommand>(doc, SketchKind::Polyline);
  });
  registry.register_command("create_circle", [](Document& doc, const CommandArgs& args) {
    (void)args;
    return std::make_unique<CreateSketchCommand>(doc, SketchKind::Circle);
  });
  registry.register_command("create_arc", [](Document& doc, const CommandArgs& args) {
    (void)args;
    return std::make_unique<CreateSketchCommand>(doc, SketchKind::Arc);
  });
  registry.register_command("create_bezier", [](Document& doc, const CommandArgs& args) {
    (void)args;
    return std::make_unique<CreateSketchCommand>(doc, SketchKind::Bezier);
  });
  registry.register_command("create_rectangle", [](Document& doc, const CommandArgs& args) {
    (void)args;
    return std::make_unique<CreateSketchCommand>(doc, SketchKind::Rectangle);
  });
  registry.register_command("create_bspline", [](Document& doc, const CommandArgs& args) {
    (void)args;
    return std::make_unique<CreateSketchCommand>(doc, SketchKind::BSpline);
  });
  registry.register_command("create_curve", [](Document& doc, const CommandArgs& args) {
    CurveDefinition definition;
    definition.kind = curve_kind_from_name(arg_string(args, "curve_kind", "line"));
    definition.points = arg_points(args, "points");
    definition.weights = arg_doubles(args, "weights");
    definition.degree = static_cast<int>(arg_int(args, "degree", 0));
    return std::make_unique<CreateCurveCommand>(doc, std::move(definition));
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

  registry.register_command("delete_entity", [](Document& doc, const CommandArgs& args) {
    return std::make_unique<DeleteEntityCommand>(
        doc, static_cast<std::uint64_t>(arg_int(args, "entity_id", 0)));
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
