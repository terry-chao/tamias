#pragma once

#include "command/core/command.h"
#include "engine/document/document.h"
#include "engine/document/text_annotation.h"

#include <cstdint>

namespace tamias {

// 放一段文字注记（世界锚点 + 屏幕朝向），一步撤销。
// 位置由 app 层换算好（点在世界工作平面上），命令本身不碰交互。
class CreateTextCommand final : public Command {
 public:
  CreateTextCommand(Document& document, TextAnnotation annotation);

  [[nodiscard]] Result<void> execute() override;
  void undo() override;
  void redo() override;

  [[nodiscard]] std::uint64_t text_id() const { return annotation_.id; }

 private:
  Document* document_ = nullptr;
  TextAnnotation annotation_;
  bool created_ = false;
};

}  // namespace tamias
