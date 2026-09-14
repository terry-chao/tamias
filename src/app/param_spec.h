#pragma once

#include <QString>

namespace tamias {

// 参数描述：驱动绘制面板表单与截面标注编辑器。
struct ParamSpec {
  QString key;    // 命令参数名，如 "width"
  QString label;  // 显示名，如 "宽度"
  double def = 0.0;
  double min = 0.0;
  double max = 1.0e6;
  double step = 0.05;
  int decimals = 3;
  // 默认值取"当前楼层的层高"（也就是本层顶），而不是写死的 def：
  // 板的标高偏移用它——在 1 楼默认画的是 1 楼顶板。层高在文档里，面板拿不到，
  // 由 MainWindow 注入的 provider 提供；拿不到时退回 def。
  bool default_is_storey_height = false;
};

}  // namespace tamias
