#include "component_specs.h"

#include "bim/wall_size.h"

namespace tamias {
namespace {

// 通用尺寸参数工厂，减少重复。
ParamSpec param(const char* key, const char* label, double def, double min, double max,
                 double step, int decimals = 3) {
  ParamSpec p;
  p.key = QString::fromUtf8(key);
  p.label = QString::fromUtf8(label);
  p.def = def;
  p.min = min;
  p.max = max;
  p.step = step;
  p.decimals = decimals;
  return p;
}

SectionPreviewSpec rect_section(const char* horiz, const char* vert,
                                SectionPreviewKind kind = SectionPreviewKind::Rectangle) {
  SectionPreviewSpec s;
  s.kind = kind;
  s.horiz_key = QString::fromUtf8(horiz);
  s.vert_key = QString::fromUtf8(vert);
  return s;
}

SectionPreviewSpec circle_section(const char* diameter) {
  SectionPreviewSpec s;
  s.kind = SectionPreviewKind::Circle;
  s.diameter_key = QString::fromUtf8(diameter);
  return s;
}

SectionPreviewSpec tee_section(SectionPreviewKind kind) {
  SectionPreviewSpec s;
  s.kind = kind;
  return s;
}

ComponentSpec wall_spec() {
  ComponentSpec s;
  s.mode = ToolMode::Wall;
  s.kind = EntityKind::Wall;
  s.discipline = Discipline::Architectural;
  s.title = QStringLiteral("墙");
  s.command = QStringLiteral("create_wall");
  s.sub_types = {
    {QStringLiteral("solid"), QStringLiteral("实心墙")},
    {QStringLiteral("hollow"), QStringLiteral("空心墙")},
    {QStringLiteral("partition"), QStringLiteral("隔墙")},
  };
  s.default_sub_type = QStringLiteral("solid");
  s.sub_type_params = {
    {"solid",
     {param("thickness", "厚度", kDefaultWallThickness, 0.05, 2.0, 0.05),
      param("height", "高度", kDefaultWallHeight, 0.5, 30.0, 0.1)}},
    {"hollow",
     {param("thickness", "厚度", kDefaultWallThickness, 0.1, 2.0, 0.05),
      param("height", "高度", kDefaultWallHeight, 0.5, 30.0, 0.1),
      param("leaf", "单侧壁厚", 0.08, 0.03, 0.5, 0.01)}},
    {"partition",
     {param("thickness", "厚度", 0.12, 0.05, 0.5, 0.01),
      param("height", "高度", kDefaultWallHeight, 0.5, 30.0, 0.1)}},
  };
  s.pick_points = 2;
  s.pick_hint = QStringLiteral("点击两点确定墙的起止位置");
  s.sub_type_section = {
    {"solid", rect_section("thickness", "height")},
    {"hollow", [] {
       SectionPreviewSpec sec;
       sec.kind = SectionPreviewKind::HollowRect;
       sec.horiz_key = QStringLiteral("thickness");
       sec.vert_key = QStringLiteral("height");
       sec.leaf_key = QStringLiteral("leaf");
       return sec;
     }()},
    {"partition", rect_section("thickness", "height")},
  };
  return s;
}

ComponentSpec beam_spec() {
  ComponentSpec s;
  s.mode = ToolMode::Beam;
  s.kind = EntityKind::Beam;
  s.discipline = Discipline::Structural;
  s.title = QStringLiteral("梁");
  s.command = QStringLiteral("create_beam");
  s.sub_types = {
    {QStringLiteral("rect"), QStringLiteral("矩形梁")},
    {QStringLiteral("tee"), QStringLiteral("T 形梁")},
    {QStringLiteral("i"), QStringLiteral("工字梁")},
  };
  s.default_sub_type = QStringLiteral("rect");
  // 矩形梁：宽 × 高。
  s.sub_type_params = {
    {"rect",
     {param("width", "宽度", 0.3, 0.05, 3.0, 0.05),
      param("depth", "高度", 0.5, 0.05, 5.0, 0.05)}},
    {"tee",
     {param("flange_width", "翼缘宽度", 0.4, 0.1, 3.0, 0.05),
      param("web_thickness", "腹板厚度", 0.2, 0.05, 1.0, 0.02),
      param("height", "总高度", 0.5, 0.1, 5.0, 0.05),
      param("flange_thickness", "翼缘厚度", 0.1, 0.05, 1.0, 0.02)}},
    {"i",
     {param("flange_width", "翼缘宽度", 0.4, 0.1, 3.0, 0.05),
      param("web_thickness", "腹板厚度", 0.2, 0.05, 1.0, 0.02),
      param("height", "总高度", 0.5, 0.1, 5.0, 0.05),
      param("flange_thickness", "翼缘厚度", 0.1, 0.05, 1.0, 0.02)}},
  };
  s.pick_points = 2;
  s.pick_hint = QStringLiteral("点击两点确定梁的跨度与方向");
  s.sub_type_section = {
    {"rect", rect_section("width", "depth")},
    {"tee", tee_section(SectionPreviewKind::Tee)},
    {"i", tee_section(SectionPreviewKind::IBeam)},
  };
  return s;
}

ComponentSpec column_spec() {
  ComponentSpec s;
  s.mode = ToolMode::Column;
  s.kind = EntityKind::Column;
  s.discipline = Discipline::Structural;
  s.title = QStringLiteral("柱");
  s.command = QStringLiteral("create_column");
  s.sub_types = {
    {QStringLiteral("rect"), QStringLiteral("矩形柱")},
    {QStringLiteral("circle"), QStringLiteral("圆柱")},
  };
  s.default_sub_type = QStringLiteral("rect");
  s.params = {
    param("height", "高度", 3.0, 0.5, 30.0, 0.1),
  };
  // 矩形柱：宽 × 深；圆柱：直径。
  s.sub_type_params = {
    {"rect",
     {param("width", "宽度", 0.4, 0.05, 3.0, 0.05),
      param("depth", "深度", 0.4, 0.05, 3.0, 0.05)}},
    {"circle",
     {param("diameter", "直径", 0.4, 0.05, 3.0, 0.05)}},
  };
  s.pick_points = 1;
  s.pick_hint = QStringLiteral("点击放置柱的位置");
  s.sub_type_section = {
    {"rect", rect_section("width", "depth")},
    {"circle", circle_section("diameter")},
  };
  return s;
}

ComponentSpec slab_spec() {
  ComponentSpec s;
  s.mode = ToolMode::Slab;
  s.kind = EntityKind::Slab;
  s.discipline = Discipline::Structural;
  s.title = QStringLiteral("板");
  s.command = QStringLiteral("create_slab");
  s.sub_types = {
    {QStringLiteral("beamslab"), QStringLiteral("有梁板")},
    {QStringLiteral("flat"), QStringLiteral("无梁板")},
    {QStringLiteral("cantilever"), QStringLiteral("悬挑板")},
    {QStringLiteral("raft"), QStringLiteral("筏板")},
  };
  s.params = {
    param("thickness", "厚度", 0.2, 0.05, 3.0, 0.05),
    param("elevation", "标高偏移", kDefaultWallHeight, -10.0, 30.0, 0.1),
  };
  s.pick_points = 2;
  s.pick_hint = QStringLiteral("点击两点确定板的矩形范围（需在平面视图）");
  s.section = rect_section("", "thickness");  // 侧面示意，只标厚度
  return s;
}

ComponentSpec door_spec() {
  ComponentSpec s;
  s.mode = ToolMode::Door;
  s.kind = EntityKind::Door;
  s.discipline = Discipline::Architectural;
  s.title = QStringLiteral("门");
  s.command = QStringLiteral("create_door");
  s.sub_types = {
    {QStringLiteral("single"), QStringLiteral("单扇平开")},
    {QStringLiteral("double"), QStringLiteral("双扇平开")},
    {QStringLiteral("sliding"), QStringLiteral("推拉门")},
    {QStringLiteral("leaf"), QStringLiteral("子母门")},
  };
  s.params = {
    param("width", "宽度", 1.0, 0.3, 6.0, 0.05),
    param("height", "高度", 2.1, 1.0, 6.0, 0.05),
    param("thickness", "厚度", 0.05, 0.02, 0.5, 0.01),
    param("sill", "离地高度", 0.0, 0.0, 6.0, 0.05),
  };
  s.pick_points = 1;
  s.pick_hint = QStringLiteral("点击墙面放置（门必须开在墙上）");
  s.section = rect_section("width", "height", SectionPreviewKind::Door);
  return s;
}

ComponentSpec window_spec() {
  ComponentSpec s;
  s.mode = ToolMode::Window;
  s.kind = EntityKind::Window;
  s.discipline = Discipline::Architectural;
  s.title = QStringLiteral("窗");
  s.command = QStringLiteral("create_window");
  s.sub_types = {
    {QStringLiteral("fixed"), QStringLiteral("固定窗")},
    {QStringLiteral("casement"), QStringLiteral("平开窗")},
    {QStringLiteral("sliding"), QStringLiteral("推拉窗")},
    {QStringLiteral("awning"), QStringLiteral("悬窗")},
    {QStringLiteral("louver"), QStringLiteral("百叶")},
  };
  s.params = {
    param("width", "宽度", 1.2, 0.3, 6.0, 0.05),
    param("height", "高度", 1.2, 0.3, 6.0, 0.05),
    param("thickness", "厚度", 0.08, 0.02, 0.5, 0.01),
    param("sill", "离地高度", 0.9, 0.0, 6.0, 0.05),
  };
  s.pick_points = 1;
  s.pick_hint = QStringLiteral("点击墙面放置（窗必须开在墙上）");
  s.section = rect_section("width", "height", SectionPreviewKind::Window);
  return s;
}

ComponentSpec structural_wall_spec() {
  ComponentSpec s;
  s.mode = ToolMode::StructuralWall;
  s.kind = EntityKind::StructuralWall;
  s.discipline = Discipline::Structural;
  s.title = QStringLiteral("结构墙");
  s.command = QStringLiteral("create_structural_wall");
  s.sub_types = {
    {QStringLiteral("shear"), QStringLiteral("剪力墙")},
    {QStringLiteral("bearing"), QStringLiteral("承重墙")},
    {QStringLiteral("retaining"), QStringLiteral("挡土墙")},
  };
  s.params = {
    param("thickness", "厚度", 0.3, 0.1, 3.0, 0.05),
    param("height", "高度", kDefaultWallHeight, 0.5, 30.0, 0.1),
  };
  s.pick_points = 2;
  s.pick_hint = QStringLiteral("点击两点确定结构墙的起止位置");
  s.section = rect_section("thickness", "height");
  return s;
}

ComponentSpec foundation_spec() {
  ComponentSpec s;
  s.mode = ToolMode::Foundation;
  s.kind = EntityKind::Foundation;
  s.discipline = Discipline::Structural;
  s.title = QStringLiteral("基础");
  s.command = QStringLiteral("create_foundation");
  s.sub_types = {
    {QStringLiteral("isolated"), QStringLiteral("独立基础")},
    {QStringLiteral("strip"), QStringLiteral("条形基础")},
    {QStringLiteral("raft"), QStringLiteral("筏板基础")},
    {QStringLiteral("pile"), QStringLiteral("桩基础")},
  };
  s.default_sub_type = QStringLiteral("isolated");
  // 独立/条形/筏板：长 × 宽 × 高。
  s.sub_type_params = {
    {"isolated",
     {param("length", "长度", 1.5, 0.2, 30.0, 0.1),
      param("width", "宽度", 1.5, 0.2, 30.0, 0.1),
      param("height", "高度", 0.5, 0.1, 5.0, 0.05)}},
    {"strip",
     {param("length", "长度", 3.0, 0.2, 30.0, 0.1),
      param("width", "宽度", 1.0, 0.2, 30.0, 0.1),
      param("height", "高度", 0.5, 0.1, 5.0, 0.05)}},
    {"raft",
     {param("length", "长度", 6.0, 0.5, 50.0, 0.1),
      param("width", "宽度", 6.0, 0.5, 50.0, 0.1),
      param("height", "高度", 0.3, 0.1, 5.0, 0.05)}},
    {"pile",
     {param("diameter", "直径", 0.6, 0.2, 3.0, 0.05),
      param("height", "高度", 3.0, 0.5, 30.0, 0.1)}},
  };
  s.pick_points = 1;
  s.pick_hint = QStringLiteral("点击放置基础的中心位置");
  s.sub_type_section = {
    {"isolated", rect_section("length", "width")},
    {"strip", rect_section("length", "width")},
    {"raft", rect_section("length", "width")},
    {"pile", circle_section("diameter")},
  };
  return s;
}

ComponentSpec curtain_wall_spec() {
  ComponentSpec s;
  s.mode = ToolMode::CurtainWall;
  s.kind = EntityKind::CurtainWall;
  s.discipline = Discipline::Architectural;
  s.title = QStringLiteral("幕墙");
  s.command = QStringLiteral("create_curtain_wall");
  s.sub_types = {
    {QStringLiteral("exposed"), QStringLiteral("明框")},
    {QStringLiteral("hidden"), QStringLiteral("隐框")},
    {QStringLiteral("unitized"), QStringLiteral("单元式")},
  };
  s.params = {
    param("thickness", "厚度", 0.15, 0.05, 1.0, 0.01),
    param("height", "高度", kDefaultWallHeight, 0.5, 30.0, 0.1),
  };
  s.pick_points = 2;
  s.pick_hint = QStringLiteral("点击两点确定幕墙的起止位置");
  s.section = rect_section("thickness", "height", SectionPreviewKind::Curtain);
  return s;
}

}  // namespace

const std::vector<ComponentSpec>& component_specs() {
  static const std::vector<ComponentSpec> specs = {
    wall_spec(),        beam_spec(),      column_spec(),
    slab_spec(),        door_spec(),      window_spec(),
    structural_wall_spec(), foundation_spec(), curtain_wall_spec(),
  };
  return specs;
}

const ComponentSpec* find_component_spec(ToolMode mode) {
  for (const ComponentSpec& s : component_specs()) {
    if (s.mode == mode) {
      return &s;
    }
  }
  return nullptr;
}

}  // namespace tamias
