#include "command/core/command_system.h"

#include "command/edit/add_feature_command.h"
#include "command/edit/boolean_command.h"
#include "command/create/create_beam_command.h"
#include "command/create/create_curve_command.h"
#include "command/create/create_curtain_wall_command.h"
#include "command/create/create_foundation_command.h"
#include "command/create/create_grid_axis_command.h"
#include "command/create/create_primitive_command.h"
#include "command/create/create_sketch_command.h"
#include "command/create/create_slab_command.h"
#include "command/create/create_storey_command.h"
#include "command/create/create_text_command.h"
#include "command/create/create_structural_wall_command.h"
#include "command/create/create_wall_command.h"
#include "command/delete/delete_entity_command.h"
#include "command/delete/delete_grid_axis_command.h"
#include "command/delete/delete_text_command.h"
#include "command/edit/copy_entities_command.h"
#include "command/edit/entity_transform.h"
#include "command/edit/mirror_entities_command.h"
#include "command/edit/set_feature_param_command.h"
#include "command/edit/set_location_command.h"
#include "command/edit/set_material_command.h"
#include "command/edit/transform_entities_command.h"
#include "command/edit/transform_tool_command.h"
#include "command/edit/update_grid_command.h"
#include "command/edit/update_text_command.h"
#include "bim/wall_size.h"
#include "entity/family/host/structural/column_entity.h"

#include <cmath>
#include <memory>
#include <optional>

namespace tamias {
namespace {

constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

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

std::optional<Vec3> arg_vec3(const CommandArgs& args, const std::string& name) {
  const auto it = args.find(name);
  if (it != args.end() && std::holds_alternative<Vec3>(it->second)) {
    return std::get<Vec3>(it->second);
  }
  return std::nullopt;
}

std::vector<Vec3> placement_points(const CommandArgs& args) {
  auto points = arg_points(args, "points");
  if (points.empty()) {
    if (auto origin = arg_vec3(args, "origin")) {
      points.push_back(*origin);
    }
  }
  return points;
}

std::unique_ptr<CreateSketchCommand> make_sketch(Document& doc, SketchKind kind,
                                                 const CommandArgs& args) {
  auto points = arg_points(args, "points");
  if (!points.empty()) {
    return std::make_unique<CreateSketchCommand>(doc, kind, std::move(points));
  }
  return std::make_unique<CreateSketchCommand>(doc, kind);
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

// 「对选中集做一次编辑操作」的公共入参：ids（数组）优先，其次单个 entity_id；
// 都没给就用当前选择集（交互式工具和脚本都走这一处）。
std::vector<std::uint64_t> arg_target_ids(Document& doc, const CommandArgs& args) {
  std::vector<std::uint64_t> ids;
  const auto it = args.find("ids");
  if (it != args.end() && std::holds_alternative<std::vector<double>>(it->second)) {
    for (const double value : std::get<std::vector<double>>(it->second)) {
      if (value > 0.0) {
        ids.push_back(static_cast<std::uint64_t>(value));
      }
    }
  }
  if (ids.empty()) {
    const auto single = static_cast<std::uint64_t>(arg_int(args, "entity_id", 0));
    if (single != 0) {
      ids.push_back(single);
    }
  }
  if (ids.empty()) {
    ids = doc.selected_ids();
  }
  return ids;
}

// 把「当前摆放 → 目标摆放」的变换列表打包成一条可撤销命令。
std::unique_ptr<Command> make_transform(Document& doc, const std::vector<std::uint64_t>& ids,
                                       const Mat4& placement) {
  std::vector<EntityTransform> items;
  items.reserve(ids.size());
  for (const std::uint64_t id : ids) {
    const Entity* entity = doc.entity(id);
    if (entity == nullptr) {
      continue;
    }
    EntityTransform item;
    item.id = id;
    item.from = entity_world_transform(*entity);
    item.to = placement * item.from;
    items.push_back(item);
  }
  return std::make_unique<TransformEntitiesCommand>(doc, std::move(items));
}

// 阵列的摆放序列：linear 用 direction + spacing，polar 用 center + step_angle。
Result<std::vector<Mat4>> array_placements(const CommandArgs& args) {
  const int count = static_cast<int>(arg_int(args, "count", 3));
  if (arg_string(args, "mode", "linear") == "polar") {
    const Vec3 center = arg_vec3(args, "center").value_or(Vec3{});
    double step = arg_double(args, "step_angle", 0.0);
    if (std::fabs(step) < 1e-9) {
      // 没给每份夹角就按总角度推。count 含原件：整圈（360°）按 count 均分，否则
      // 最后一份会和原件叠在一起；非整圈按 count-1 份均分（首末都要）。
      const double total = arg_double(args, "total_angle", 360.0);
      const bool full_circle = std::fabs(total) >= 359.999 || std::fabs(total) <= 1e-9;
      const int pieces = full_circle ? count : count - 1;
      step = pieces > 0 ? total / static_cast<double>(pieces) : 0.0;
    }
    return polar_array_placements(center, step, count);
  }
  const Vec3 direction = arg_vec3(args, "direction").value_or(Vec3{1.f, 0.f, 0.f});
  return linear_array_placements(direction, arg_double(args, "spacing", 1.0), count);
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
    const double thickness = arg_double(args, "thickness", kDefaultWallThickness);
    const double height = arg_double(args, "height", kDefaultWallHeight);
    const double leaf = arg_double(args, "leaf", 0.0);  // >0 → 空心墙
    auto points = arg_points(args, "points");
    if (leaf > 0.0) {
      return std::make_unique<CreateWallCommand>(doc, thickness, height, leaf);
    }
    if (points.size() >= 2) {
      return std::make_unique<CreateWallCommand>(doc, thickness, height, points[0], points[1]);
    }
    return std::make_unique<CreateWallCommand>(doc, thickness, height);
  });

  registry.register_command("create_beam", [](Document& doc, const CommandArgs& args) {
    const std::string sub = arg_string(args, "sub_type", "rect");
    auto points = arg_points(args, "points");
    if (sub == "tee" || sub == "i") {
      const BeamShape shape = sub == "tee" ? BeamShape::Tee : BeamShape::IBeam;
      const double flange_width = arg_double(args, "flange_width", 0.4);
      const double web_thickness = arg_double(args, "web_thickness", 0.2);
      const double height = arg_double(args, "height", 0.5);
      const double flange_thickness = arg_double(args, "flange_thickness", 0.1);
      if (points.size() >= 2) {
        auto cmd = std::make_unique<CreateBeamCommand>(doc, shape, flange_width, web_thickness,
                                                       height, flange_thickness);
        // 脚本式暂不支持 T/I 带点构造，回退到交互式。
        return cmd;
      }
      return std::make_unique<CreateBeamCommand>(doc, shape, flange_width, web_thickness,
                                                  height, flange_thickness);
    }
    const double width = arg_double(args, "width", 0.3);
    const double depth = arg_double(args, "depth", 0.5);
    if (points.size() >= 2) {
      return std::make_unique<CreateBeamCommand>(doc, width, depth, points[0], points[1]);
    }
    return std::make_unique<CreateBeamCommand>(doc, width, depth);
  });

  registry.register_command("create_column", [](Document& doc, const CommandArgs& args) {
    auto points = placement_points(args);
    const auto host_id = static_cast<std::uint64_t>(arg_int(args, "host_id", 0));
    // 子类型：rect（矩形，默认）/ circle（圆柱）。
    const std::string shape = arg_string(args, "sub_type", "rect");
    const ColumnShape col_shape =
        shape == "circle" ? ColumnShape::Circular : ColumnShape::Rectangular;
    const double width = arg_double(args, "width", 0.4);
    const double depth = arg_double(args, "depth", 0.4);
    const double diameter = arg_double(args, "diameter", 0.4);
    const double height = arg_double(args, "height", 3.0);
    if (!points.empty()) {
      return std::make_unique<CreatePrimitiveCommand>(doc, PrimitiveKind::Column, points[0],
                                                       col_shape,
                                                       col_shape == ColumnShape::Circular ? diameter : width,
                                                       depth, height);
    }
    // 交互式：武装后等视口喂点。
    return std::make_unique<CreatePrimitiveCommand>(doc, col_shape,
                                                      col_shape == ColumnShape::Circular ? diameter : width,
                                                      depth, height);
  });

  registry.register_command("create_structural_wall", [](Document& doc, const CommandArgs& args) {
    const double thickness = arg_double(args, "thickness", 0.3);
    const double height = arg_double(args, "height", kDefaultWallHeight);
    auto points = arg_points(args, "points");
    if (points.size() >= 2) {
      return std::make_unique<CreateStructuralWallCommand>(doc, thickness, height, points[0], points[1]);
    }
    return std::make_unique<CreateStructuralWallCommand>(doc, thickness, height);
  });

  registry.register_command("create_foundation", [](Document& doc, const CommandArgs& args) {
    const std::string sub = arg_string(args, "sub_type", "isolated");
    auto points = placement_points(args);
    if (sub == "pile") {
      const double diameter = arg_double(args, "diameter", 0.6);
      const double height = arg_double(args, "height", 3.0);
      return std::make_unique<CreateFoundationCommand>(doc, diameter, height);
    }
    const double length = arg_double(args, "length", sub == "strip" ? 3.0 : 1.5);
    const double width = arg_double(args, "width", sub == "raft" ? 6.0 : 1.5);
    const double height = arg_double(args, "height", sub == "raft" ? 0.3 : 0.5);
    if (!points.empty()) {
      return std::make_unique<CreateFoundationCommand>(doc, length, width, height, points[0]);
    }
    return std::make_unique<CreateFoundationCommand>(doc, length, width, height);
  });

  registry.register_command("create_curtain_wall", [](Document& doc, const CommandArgs& args) {
    const double thickness = arg_double(args, "thickness", 0.15);
    const double height = arg_double(args, "height", kDefaultWallHeight);
    auto points = arg_points(args, "points");
    if (points.size() >= 2) {
      return std::make_unique<CreateCurtainWallCommand>(doc, thickness, height, points[0], points[1]);
    }
    return std::make_unique<CreateCurtainWallCommand>(doc, thickness, height);
  });

  registry.register_command("create_slab", [](Document& doc, const CommandArgs& args) {
    // 偏移相对当前楼层标高：没显式给就默认画**本层顶板**——本层顶的标高就是层高。
    // 没有当前楼层（楼层表为空，或选了"未指定"）就没有"本层顶"，落在地面 0 上。
    const Storey* active = doc.bim().find_storey(doc.bim().active_storey_id());
    const double default_offset = active != nullptr && active->height > 0.0 ? active->height : 0.0;
    const double thickness = arg_double(args, "thickness", 0.2);
    const double elevation = arg_double(args, "elevation", default_offset);
    auto points = arg_points(args, "points");
    if (points.size() >= 2) {
      return std::make_unique<CreateSlabCommand>(doc, thickness, elevation, points[0], points[1]);
    }
    return std::make_unique<CreateSlabCommand>(doc, thickness, elevation);
  });

  registry.register_command("create_door", [](Document& doc, const CommandArgs& args) {
    const double width = arg_double(args, "width", 1.0);
    const double height = arg_double(args, "height", 2.1);
    const double thickness = arg_double(args, "thickness", 0.05);
    const double sill = arg_double(args, "sill", 0.0);
    auto points = placement_points(args);
    const auto host_id = static_cast<std::uint64_t>(arg_int(args, "host_id", 0));
    if (!points.empty()) {
      return std::make_unique<CreatePrimitiveCommand>(
          doc, PrimitiveKind::Door, points[0], host_id, width, height, thickness, sill);
    }
    return std::make_unique<CreatePrimitiveCommand>(doc, PrimitiveKind::Door, width, height,
                                                     thickness, sill);
  });

  registry.register_command("create_window", [](Document& doc, const CommandArgs& args) {
    const double width = arg_double(args, "width", 1.2);
    const double height = arg_double(args, "height", 1.2);
    const double thickness = arg_double(args, "thickness", 0.08);
    const double sill = arg_double(args, "sill", 0.9);
    auto points = placement_points(args);
    const auto host_id = static_cast<std::uint64_t>(arg_int(args, "host_id", 0));
    if (!points.empty()) {
      return std::make_unique<CreatePrimitiveCommand>(
          doc, PrimitiveKind::Window, points[0], host_id, width, height, thickness, sill);
    }
    return std::make_unique<CreatePrimitiveCommand>(doc, PrimitiveKind::Window, width, height,
                                                     thickness, sill);
  });

  registry.register_command("create_line", [](Document& doc, const CommandArgs& args) {
    return make_sketch(doc, SketchKind::Line, args);
  });
  registry.register_command("create_polyline", [](Document& doc, const CommandArgs& args) {
    return make_sketch(doc, SketchKind::Polyline, args);
  });
  registry.register_command("create_circle", [](Document& doc, const CommandArgs& args) {
    return make_sketch(doc, SketchKind::Circle, args);
  });
  registry.register_command("create_arc", [](Document& doc, const CommandArgs& args) {
    return make_sketch(doc, SketchKind::Arc, args);
  });
  registry.register_command("create_bezier", [](Document& doc, const CommandArgs& args) {
    return make_sketch(doc, SketchKind::Bezier, args);
  });
  registry.register_command("create_rectangle", [](Document& doc, const CommandArgs& args) {
    return make_sketch(doc, SketchKind::Rectangle, args);
  });
  registry.register_command("create_bspline", [](Document& doc, const CommandArgs& args) {
    return make_sketch(doc, SketchKind::BSpline, args);
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

  // ---- 通用编辑：移动 / 复制 / 旋转 / 镜像 / 阵列 ----
  // 全部按「选择集 → 一组摆放 → 一条可撤销命令」走；给了点或 delta 就直接做，
  // 没给就武装成交互式工具（视口里点基点 / 目标点，见 transform_tool_command.h）。
  registry.register_command("move_entities", [](Document& doc, const CommandArgs& args) {
    const std::vector<std::uint64_t> ids = arg_target_ids(doc, args);
    if (std::optional<Vec3> delta = arg_vec3(args, "delta")) {
      return make_transform(doc, ids, translation_transform(*delta));
    }
    const std::vector<Vec3> points = arg_points(args, "points");
    if (points.size() >= 2) {
      return make_transform(doc, ids, translation_transform(points[1] - points[0]));
    }
    return std::unique_ptr<Command>{std::make_unique<TransformToolCommand>(
        doc, TransformToolCommand::Mode::Move, ids)};
  });

  registry.register_command("copy_entities", [](Document& doc, const CommandArgs& args) {
    const std::vector<std::uint64_t> ids = arg_target_ids(doc, args);
    std::vector<Mat4> placements;
    if (std::optional<Vec3> delta = arg_vec3(args, "delta")) {
      placements.push_back(translation_transform(*delta));
    } else {
      const std::vector<Vec3> points = arg_points(args, "points");
      if (points.size() >= 2) {
        placements.push_back(translation_transform(points[1] - points[0]));
      }
    }
    if (placements.empty()) {
      return std::unique_ptr<Command>{std::make_unique<TransformToolCommand>(
          doc, TransformToolCommand::Mode::Copy, ids)};
    }
    return std::unique_ptr<Command>{
        std::make_unique<CopyEntitiesCommand>(doc, ids, std::move(placements))};
  });

  registry.register_command("rotate_entities", [](Document& doc, const CommandArgs& args) {
    const std::vector<std::uint64_t> ids = arg_target_ids(doc, args);
    if (const auto angle = args.find("angle");
        angle != args.end() && std::holds_alternative<double>(angle->second)) {
      const Vec3 center = arg_vec3(args, "center").value_or(Vec3{});
      return make_transform(doc, ids,
                            yaw_rotation_about(center, std::get<double>(angle->second) * kDegToRad));
    }
    return std::unique_ptr<Command>{std::make_unique<TransformToolCommand>(
        doc, TransformToolCommand::Mode::Rotate, ids)};
  });

  registry.register_command("mirror_entities", [](Document& doc, const CommandArgs& args) {
    const std::vector<std::uint64_t> ids = arg_target_ids(doc, args);
    const std::vector<Vec3> points = arg_points(args, "points");
    if (points.size() >= 2) {
      return std::unique_ptr<Command>{
          std::make_unique<MirrorEntitiesCommand>(doc, ids, points[0], points[1])};
    }
    return std::unique_ptr<Command>{std::make_unique<TransformToolCommand>(
        doc, TransformToolCommand::Mode::Mirror, ids)};
  });

  // 阵列 = 先算出一串摆放，再走复制那条路（门窗 / 墙交接 / 网格 intern 全都复用）。
  registry.register_command("array_entities", [](Document& doc, const CommandArgs& args) {
    const std::vector<std::uint64_t> ids = arg_target_ids(doc, args);
    std::vector<Mat4> placements = array_placements(args).value_or(std::vector<Mat4>{});
    return std::unique_ptr<Command>{
        std::make_unique<CopyEntitiesCommand>(doc, ids, std::move(placements))};
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
    material.opacity = static_cast<float>(arg_double(args, "opacity", 1.0));
    material.albedo_texture_id =
        static_cast<std::uint64_t>(arg_int(args, "albedo_texture_id", 0));
    material.normal_texture_id =
        static_cast<std::uint64_t>(arg_int(args, "normal_texture_id", 0));
    material.orm_texture_id = static_cast<std::uint64_t>(arg_int(args, "orm_texture_id", 0));
    material.tex.scale.x = static_cast<float>(arg_double(args, "tex_scale_x", 1.0));
    material.tex.scale.y = static_cast<float>(arg_double(args, "tex_scale_y", 1.0));
    material.tex.offset.x = static_cast<float>(arg_double(args, "tex_offset_x", 0.0));
    material.tex.offset.y = static_cast<float>(arg_double(args, "tex_offset_y", 0.0));
    material.tex.rotation = static_cast<float>(arg_double(args, "tex_rotation", 0.0));
    material.tex.world_scale = static_cast<float>(arg_double(args, "tex_world_scale", 2.0));
    return std::make_unique<SetMaterialCommand>(
        doc, static_cast<std::uint64_t>(arg_int(args, "entity_id", 0)), std::move(material));
  });

  // ---- 轴网（定位参考，不是构件）----
  registry.register_command("create_grid_axis", [](Document& doc, const CommandArgs& args) {
    GridAxis axis;
    axis.name = arg_string(args, "name", "1");
    axis.direction = arg_string(args, "direction", "z") == "x"
                         ? GridAxisDirection::AlongX
                         : GridAxisDirection::AlongZ;
    axis.position = arg_double(args, "position", 0.0);
    axis.start = arg_double(args, "start", 0.0);
    axis.end = arg_double(args, "end", 0.0);
    return std::make_unique<CreateGridAxisCommand>(doc, std::move(axis));
  });

  // 按间距表一次生成正交轴网：x_spacings 是相邻编号轴间距，z_spacings 是相邻字母轴间距。
  registry.register_command("auto_grid", [](Document& doc, const CommandArgs& args) {
    std::vector<GridAxis> axes = make_orthogonal_grid(
        arg_double(args, "origin_x", 0.0), arg_double(args, "origin_z", 0.0),
        arg_doubles(args, "x_spacings"), arg_doubles(args, "z_spacings"),
        arg_double(args, "margin", 1.0));
    return std::make_unique<UpdateGridCommand>(doc, std::move(axes));
  });

  registry.register_command("delete_grid_axis", [](Document& doc, const CommandArgs& args) {
    return std::make_unique<DeleteGridAxisCommand>(
        doc, static_cast<std::uint64_t>(arg_int(args, "axis_id", 0)));
  });

  // ---- 文字注记（世界锚点 + 屏幕朝向；见 docs/TEXT.md §5）----
  registry.register_command("create_text", [](Document& doc, const CommandArgs& args) {
    TextAnnotation annotation;
    annotation.kind = text_kind_from_name(arg_string(args, "kind", "annotation"));
    annotation.text = arg_string(args, "text", "");
    if (auto position = arg_vec3(args, "position")) {
      annotation.anchor = *position;
    }
    annotation.size_px = static_cast<float>(arg_double(args, "size_px", 14.0));
    if (auto color = arg_vec3(args, "color")) {
      annotation.color = *color;
    }
    annotation.opacity = static_cast<float>(arg_double(args, "opacity", 1.0));
    annotation.align = text_align_from_name(arg_string(args, "align", "left"));
    return std::make_unique<CreateTextCommand>(doc, std::move(annotation));
  });

  // 只改传进来的字段：没给的保持原值（对齐属性面板 / 就地编辑的用法）。
  registry.register_command("update_text", [](Document& doc, const CommandArgs& args) {
    const auto text_id = static_cast<std::uint64_t>(arg_int(args, "text_id", 0));
    TextAnnotation updated;
    if (const TextAnnotation* current = doc.text_annotation(text_id)) {
      updated = *current;
    }
    if (args.find("text") != args.end()) {
      updated.text = arg_string(args, "text", updated.text);
    }
    if (auto position = arg_vec3(args, "position")) {
      updated.anchor = *position;
    }
    if (args.find("size_px") != args.end()) {
      updated.size_px = static_cast<float>(arg_double(args, "size_px", updated.size_px));
    }
    if (auto color = arg_vec3(args, "color")) {
      updated.color = *color;
    }
    if (args.find("opacity") != args.end()) {
      updated.opacity = static_cast<float>(arg_double(args, "opacity", updated.opacity));
    }
    if (args.find("align") != args.end()) {
      updated.align = text_align_from_name(arg_string(args, "align", "left"));
    }
    return std::make_unique<UpdateTextCommand>(doc, text_id, std::move(updated));
  });

  registry.register_command("delete_text", [](Document& doc, const CommandArgs& args) {
    return std::make_unique<DeleteTextCommand>(
        doc, static_cast<std::uint64_t>(arg_int(args, "text_id", 0)));
  });
}

}  // namespace tamias
