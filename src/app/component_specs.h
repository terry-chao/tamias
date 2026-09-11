#pragma once

#include "entity/entity.h"
#include "host/tool_mode.h"
#include "param_spec.h"
#include "section_preview_spec.h"

#include <QString>
#include <unordered_map>
#include <vector>

namespace tamias {

// 绘制子类型（如柱：矩形/圆形；门：单扇/双扇/推拉）。
struct SubTypeSpec {
  QString id;     // "rect" / "circle"
  QString label;  // "矩形柱" / "圆柱"
};

// 一个构件的完整绘制规格：子类型 + 参数 + 拾取方式 + 对应命令。
struct ComponentSpec {
  ToolMode mode = ToolMode::None;
  EntityKind kind = EntityKind::Wall;
  Discipline discipline = Discipline::None;
  QString title;       // "柱"
  QString command;     // "create_column"
  std::vector<SubTypeSpec> sub_types;
  QString default_sub_type;  // sub_types[0].id 若留空
  std::vector<ParamSpec> params;            // 公共参数（所有子类型都显示）
  // 子类型专属参数（按 sub_type id 索引），与公共参数合并显示。
  // 用 std::string 做 key：QString 无 std::hash 特化，不能直接做 unordered_map 键。
  std::unordered_map<std::string, std::vector<ParamSpec>> sub_type_params;
  SectionPreviewSpec section;  // 无子类型或未单独指定时的截面
  std::unordered_map<std::string, SectionPreviewSpec> sub_type_section;
  int pick_points = 1;   // 1=点放置，2=线/对角
  QString pick_hint;      // "点击两点确定墙的起止"

  [[nodiscard]] bool has_sub_types() const { return !sub_types.empty(); }

  [[nodiscard]] SectionPreviewSpec section_for(const QString& sub_type) const {
    if (!sub_type.isEmpty()) {
      const auto it = sub_type_section.find(sub_type.toStdString());
      if (it != sub_type_section.end()) {
        return it->second;
      }
    }
    return section;
  }

  [[nodiscard]] bool is_section_key(const QString& key, const QString& sub_type) const {
    return section_for(sub_type).has_key(key);
  }

  [[nodiscard]] std::vector<ParamSpec> merged_params(const QString& sub_type) const {
    std::vector<ParamSpec> out = params;
    const auto it = sub_type_params.find(sub_type.toStdString());
    if (it != sub_type_params.end()) {
      out.insert(out.end(), it->second.begin(), it->second.end());
    }
    return out;
  }
};

// 所有可绘制构件的规格表（按 Ribbon 显示顺序）。
[[nodiscard]] const std::vector<ComponentSpec>& component_specs();

// 按 ToolMode 查规格；找不到返回 nullptr。
[[nodiscard]] const ComponentSpec* find_component_spec(ToolMode mode);

}  // namespace tamias
