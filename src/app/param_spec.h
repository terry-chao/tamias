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
};

}  // namespace tamias
