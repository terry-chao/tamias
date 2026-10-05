#pragma once

#include "ui/tac/platform/dialogs.h"
#include "ui/tac/platform/text.h"
#include "ui/tac/platform/theme.h"

namespace tac {

// 一套界面库必须提供的"非控件"能力。新后端（tac_native 等）实现同一份契约
// 即可让上层代码原样跑起来。
class PlatformServices {
 public:
  virtual ~PlatformServices() = default;

  [[nodiscard]] virtual DialogService& dialogs() = 0;
  [[nodiscard]] virtual Theme& theme() = 0;
  [[nodiscard]] virtual TextMeasurer& text() = 0;
};

}  // namespace tac
